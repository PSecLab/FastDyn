use std::env;
use std::fs::OpenOptions;
use std::path::Path;
use std::sync::atomic::{AtomicU32, Ordering, fence};
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, anyhow, bail};
use memmap2::{MmapMut, MmapOptions};
use zenoh::{Wait, config::Config};

const SHARED_MAGIC: u32 = 0x4355_4253;
const SHARED_SIZE: usize = 176;
const MAGIC_OFFSET: usize = 0;
const ODOMETRY_SEQUENCE_OFFSET: usize = 4;
const RESPONSE_SEQUENCE_OFFSET: usize = 8;
const TERMINATE_OFFSET: usize = 12;
const ODOMETRY_OFFSET: usize = 16;
const ODOMETRY_SIZE: usize = 64;
const PWM_OFFSET: usize = 80;
const PWM_SIZE: usize = 48;
const ATTITUDE_OFFSET: usize = 128;
const ATTITUDE_SIZE: usize = 48;

const EXTERNAL_ODOMETRY_TOPIC: &str = "synapse/v1/topic/external_odometry";
const PWM_TOPIC: &str = "synapse/v1/topic/pwm_signal_outputs";
const ATTITUDE_TOPIC: &str = "synapse/v1/topic/attitude_command";

struct SharedTransport {
    mapping: MmapMut,
}

impl SharedTransport {
    fn open(path: &Path) -> Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path)
            .with_context(|| format!("cannot open shared transport {}", path.display()))?;
        if file.metadata()?.len() < SHARED_SIZE as u64 {
            bail!("shared transport is smaller than {SHARED_SIZE} bytes");
        }
        let mapping = unsafe { MmapOptions::new().len(SHARED_SIZE).map_mut(&file)? };
        let transport = Self { mapping };
        if transport.read_u32(MAGIC_OFFSET) != SHARED_MAGIC {
            bail!("shared transport has invalid magic");
        }
        Ok(transport)
    }

    fn read_u32(&self, offset: usize) -> u32 {
        u32::from_le_bytes(self.mapping[offset..offset + 4].try_into().unwrap())
    }

    fn atomic_u32(&self, offset: usize) -> &AtomicU32 {
        assert_eq!(
            (self.mapping.as_ptr() as usize + offset) % align_of::<AtomicU32>(),
            0
        );
        unsafe { &*(self.mapping.as_ptr().add(offset).cast::<AtomicU32>()) }
    }

    fn odometry_sequence(&self) -> u32 {
        self.atomic_u32(ODOMETRY_SEQUENCE_OFFSET)
            .load(Ordering::Acquire)
    }

    fn terminated(&self) -> bool {
        self.atomic_u32(TERMINATE_OFFSET).load(Ordering::Acquire) != 0
    }

    fn odometry(&self) -> [u8; ODOMETRY_SIZE] {
        self.mapping[ODOMETRY_OFFSET..ODOMETRY_OFFSET + ODOMETRY_SIZE]
            .try_into()
            .unwrap()
    }

    fn respond(
        &mut self,
        sequence: u32,
        mut pwm: [u8; PWM_SIZE],
        mut attitude: [u8; ATTITUDE_SIZE],
        timestamp: [u8; 8],
    ) {
        pwm[..8].copy_from_slice(&timestamp);
        attitude[..8].copy_from_slice(&timestamp);
        self.mapping[PWM_OFFSET..PWM_OFFSET + PWM_SIZE].copy_from_slice(&pwm);
        self.mapping[ATTITUDE_OFFSET..ATTITUDE_OFFSET + ATTITUDE_SIZE].copy_from_slice(&attitude);
        fence(Ordering::Release);
        self.atomic_u32(RESPONSE_SEQUENCE_OFFSET)
            .store(sequence, Ordering::Release);
    }
}

fn zenoh_config(locator: &str) -> Result<Config> {
    let mut config = Config::default();
    config
        .insert_json5("mode", "\"client\"")
        .map_err(|error| anyhow!("failed to configure Zenoh mode: {error}"))?;
    config
        .insert_json5("connect/endpoints", &format!("[\"{locator}\"]"))
        .map_err(|error| anyhow!("failed to configure Zenoh endpoint: {error}"))?;
    // Lockstep has at most one in-flight step, so batching cannot coalesce
    // useful work and only adds response latency.
    config
        .insert_json5("transport/link/tx/queue/batching/enabled", "false")
        .map_err(|error| anyhow!("failed to disable Zenoh batching: {error}"))?;
    Ok(config)
}

fn copy_payload<const N: usize>(sample: &zenoh::sample::Sample) -> Option<[u8; N]> {
    let bytes = sample.payload().to_bytes();
    if bytes.len() != N {
        return None;
    }
    Some(bytes.as_ref().try_into().unwrap())
}

fn main() -> Result<()> {
    let shared_path = env::var_os("CUBS2_NATIVE_SIL_SHM")
        .context("CUBS2_NATIVE_SIL_SHM was not supplied by the upstream FMI3 runner")?;
    let locator =
        env::var("CUBS2_FASTDYN_LOCATOR").unwrap_or_else(|_| "udp/192.0.2.2:7447".to_owned());
    let republish = Duration::from_secs_f64(
        env::var("CUBS2_FASTDYN_REPUBLISH_S")
            .unwrap_or_else(|_| "0.02".to_owned())
            .parse::<f64>()?,
    );
    if republish.is_zero() {
        bail!("CUBS2_FASTDYN_REPUBLISH_S must be positive");
    }

    let mut transport = SharedTransport::open(Path::new(&shared_path))?;
    let session = zenoh::open(zenoh_config(&locator)?)
        .wait()
        .map_err(|error| anyhow!("cannot connect to Zenoh router {locator}: {error}"))?;
    let pwm_subscriber = session
        .declare_subscriber(PWM_TOPIC)
        .wait()
        .map_err(|error| anyhow!("cannot subscribe to {PWM_TOPIC}: {error}"))?;
    let attitude_subscriber = session
        .declare_subscriber(ATTITUDE_TOPIC)
        .wait()
        .map_err(|error| anyhow!("cannot subscribe to {ATTITUDE_TOPIC}: {error}"))?;
    let odometry_publisher = session
        .declare_publisher(EXTERNAL_ODOMETRY_TOPIC)
        .wait()
        .map_err(|error| anyhow!("cannot publish to {EXTERNAL_ODOMETRY_TOPIC}: {error}"))?;
    println!("FastDyn/CUBS2 native FMI3 bridge connected to {locator}");

    let mut completed_sequence = 0_u32;
    let mut pending_sequence = 0_u32;
    let mut odometry = [0_u8; ODOMETRY_SIZE];
    let mut timestamp = [0_u8; 8];
    let mut pwm: Option<[u8; PWM_SIZE]> = None;
    let mut latest_attitude = [0_u8; ATTITUDE_SIZE];
    latest_attitude[8..12].copy_from_slice(&1.0_f32.to_le_bytes());
    let mut next_publish = Instant::now();
    let mut idle_rounds = 0_u32;
    let wall_start = Instant::now();

    while !transport.terminated() {
        let sequence = transport.odometry_sequence();
        if sequence != completed_sequence && sequence != pending_sequence {
            pending_sequence = sequence;
            odometry = transport.odometry();
            timestamp.copy_from_slice(&odometry[..8]);
            pwm = None;
            while pwm_subscriber
                .try_recv()
                .map_err(|error| anyhow!("failed to drain {PWM_TOPIC}: {error}"))?
                .is_some()
            {}
            while attitude_subscriber
                .try_recv()
                .map_err(|error| anyhow!("failed to drain {ATTITUDE_TOPIC}: {error}"))?
                .is_some()
            {}
            next_publish = Instant::now();
        }

        let mut did_work = false;
        if pending_sequence != 0 {
            let now = Instant::now();
            if now >= next_publish {
                odometry_publisher
                    .put(odometry)
                    .wait()
                    .map_err(|error| anyhow!("failed to publish odometry: {error}"))?;
                next_publish = now + republish;
                did_work = true;
            }
            while let Some(sample) = pwm_subscriber
                .try_recv()
                .map_err(|error| anyhow!("failed to receive {PWM_TOPIC}: {error}"))?
            {
                if let Some(payload) = copy_payload(&sample) {
                    pwm = Some(payload);
                }
                did_work = true;
            }
            while let Some(sample) = attitude_subscriber
                .try_recv()
                .map_err(|error| anyhow!("failed to receive {ATTITUDE_TOPIC}: {error}"))?
            {
                if let Some(payload) = copy_payload(&sample) {
                    latest_attitude = payload;
                }
                did_work = true;
            }
            if let Some(pwm) = pwm.take() {
                /* PWM is the plant-driving response. Attitude command is
                 * telemetry, so retain its newest sample rather than
                 * stalling lockstep when the emulated UDP link drops that
                 * independent topic. The shared response timestamps both
                 * payloads with the current plant step. */
                transport.respond(pending_sequence, pwm, latest_attitude, timestamp);
                completed_sequence = pending_sequence;
                pending_sequence = 0;
                if completed_sequence == 1 || completed_sequence.is_multiple_of(10_000) {
                    let time_us = u64::from_le_bytes(timestamp);
                    println!(
                        "FastDyn/CUBS2 FMI3 lockstep sequence={completed_sequence} time={:.3}s",
                        time_us as f64 / 1.0e6
                    );
                }
                continue;
            }
        }

        if did_work {
            idle_rounds = 0;
        } else {
            idle_rounds = idle_rounds.wrapping_add(1);
            if idle_rounds.is_multiple_of(256) {
                thread::yield_now();
            } else {
                std::hint::spin_loop();
            }
        }
    }
    let wall_s = wall_start.elapsed().as_secs_f64();
    println!(
        "FastDyn/CUBS2 FMI3 bridge completed {completed_sequence} steps in {wall_s:.6}s ({:.1} steps/s)",
        f64::from(completed_sequence) / wall_s.max(f64::MIN_POSITIVE)
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::{Seek, SeekFrom, Write};

    #[test]
    fn shared_layout_matches_upstream_synapse_fbs_contract() {
        assert_eq!(ODOMETRY_OFFSET + ODOMETRY_SIZE, PWM_OFFSET);
        assert_eq!(PWM_OFFSET + PWM_SIZE, ATTITUDE_OFFSET);
        assert_eq!(ATTITUDE_OFFSET + ATTITUDE_SIZE, SHARED_SIZE);
    }

    #[test]
    fn response_uses_synapse_payloads_and_plant_timestamp() {
        let path = env::temp_dir().join(format!(
            "fastdyn-cubs2-bridge-test-{}-{}",
            std::process::id(),
            std::thread::current().name().unwrap_or("response")
        ));
        let mut file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        file.set_len(SHARED_SIZE as u64).unwrap();
        file.seek(SeekFrom::Start(MAGIC_OFFSET as u64)).unwrap();
        file.write_all(&SHARED_MAGIC.to_le_bytes()).unwrap();
        drop(file);

        let mut transport = SharedTransport::open(&path).unwrap();
        let timestamp = 1_250_000_u64.to_le_bytes();
        let mut pwm = [0_u8; PWM_SIZE];
        let mut attitude = [0_u8; ATTITUDE_SIZE];
        pwm[..8].copy_from_slice(&9_000_000_u64.to_le_bytes());
        attitude[..8].copy_from_slice(&9_000_000_u64.to_le_bytes());
        transport.respond(7, pwm, attitude, timestamp);

        assert_eq!(transport.read_u32(RESPONSE_SEQUENCE_OFFSET), 7);
        assert_eq!(&transport.mapping[PWM_OFFSET..PWM_OFFSET + 8], &timestamp);
        assert_eq!(
            &transport.mapping[ATTITUDE_OFFSET..ATTITUDE_OFFSET + 8],
            &timestamp
        );
        drop(transport);
        fs::remove_file(path).unwrap();
    }
}
