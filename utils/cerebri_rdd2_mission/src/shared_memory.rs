use std::fs::{self, File, OpenOptions};
use std::path::Path;
use std::ptr;
use std::sync::atomic::{AtomicU32, Ordering};
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, anyhow, bail};
use memmap2::{MmapMut, MmapOptions};
use object::{Object, ObjectSymbol};
use synapse_fbs::topic;

use crate::protocol::{self, FlightState, LockstepInputs, MotorCommand};

const RDD2_LOCKSTEP_MAGIC: u32 = 0x5244_4432;
const SHARED_SYMBOL: &str = "rdd2_fastdyn_lockstep_shared";
const RAM_START_SYMBOL: &str = "_image_ram_start";

#[repr(C)]
struct SharedLayout {
    magic: AtomicU32,
    input_sequence: AtomicU32,
    response_sequence: AtomicU32,
    terminate: AtomicU32,
    inertial_sample: topic::InertialSampleData,
    manual_control: topic::ManualControlData,
    pwm_signal_outputs: topic::PwmSignalOutputsData,
    vehicle_health: topic::VehicleHealthData,
    attitude_estimate: topic::AttitudeEstimateData,
    attitude_command: topic::AttitudeCommandData,
    control_loop_metrics: topic::ControlLoopMetricsData,
}

pub struct Transport {
    mapping: MmapMut,
    offset: usize,
    sequence: u32,
}

fn symbol_address(elf: &object::File<'_>, name: &str) -> Result<u64> {
    elf.symbol_by_name(name)
        .map(|symbol| symbol.address())
        .ok_or_else(|| anyhow!("firmware ELF has no {name} symbol"))
}

fn open_mapping(path: &Path, required_len: usize, deadline: Instant) -> Result<(File, MmapMut)> {
    loop {
        if let Ok(file) = OpenOptions::new().read(true).write(true).open(path)
            && file.metadata()?.len() >= required_len as u64
        {
            // SAFETY: FastDyn creates this file as QEMU's shared RAM backend.
            // The mapping stays owned by Transport for the complete exchange.
            let mapping = unsafe { MmapOptions::new().map_mut(&file) }
                .with_context(|| format!("cannot map FastDyn RAM file {}", path.display()))?;
            return Ok((file, mapping));
        }
        if Instant::now() >= deadline {
            bail!("FastDyn RAM file {} was not prepared", path.display());
        }
        thread::sleep(Duration::from_millis(1));
    }
}

impl Transport {
    pub fn open(memory_path: &Path, firmware_elf: &Path, timeout: Duration) -> Result<Self> {
        let elf_bytes = fs::read(firmware_elf)
            .with_context(|| format!("cannot read firmware ELF {}", firmware_elf.display()))?;
        let elf = object::File::parse(&*elf_bytes).context("cannot parse firmware ELF")?;
        let shared_address = symbol_address(&elf, SHARED_SYMBOL)?;
        let ram_start = symbol_address(&elf, RAM_START_SYMBOL)?;
        let offset: usize = shared_address
            .checked_sub(ram_start)
            .ok_or_else(|| anyhow!("{SHARED_SYMBOL} is outside the main firmware RAM"))?
            .try_into()
            .context("shared-memory offset does not fit the host address space")?;
        let required_len = offset
            .checked_add(size_of::<SharedLayout>())
            .context("shared-memory extent overflow")?;
        let deadline = Instant::now() + timeout;
        let (_file, mapping) = open_mapping(memory_path, required_len, deadline)?;
        let shared_base = mapping.as_ptr() as usize + offset;
        if !shared_base.is_multiple_of(align_of::<SharedLayout>()) {
            bail!(
                "{SHARED_SYMBOL} at RAM offset {offset:#x} is not {}-byte aligned",
                align_of::<SharedLayout>()
            );
        }
        let mut transport = Self {
            mapping,
            offset,
            sequence: 0,
        };

        while transport.shared().magic.load(Ordering::Acquire) != RDD2_LOCKSTEP_MAGIC {
            if Instant::now() >= deadline {
                bail!("RDD2 firmware did not initialize shared lockstep symbol {SHARED_SYMBOL}");
            }
            thread::yield_now();
        }
        transport.sequence = transport.shared().response_sequence.load(Ordering::Acquire);
        Ok(transport)
    }

    fn shared(&self) -> &SharedLayout {
        // SAFETY: offset was resolved from the ELF and bounds-checked against
        // the mapped RAM file. The C and Rust layouts are asserted to 320 B.
        unsafe {
            &*(self
                .mapping
                .as_ptr()
                .add(self.offset)
                .cast::<SharedLayout>())
        }
    }

    fn shared_mut_ptr(&mut self) -> *mut SharedLayout {
        // SAFETY: same invariant as shared(); exchanges are serialized through
        // &mut self, and payload publication is ordered by input_sequence.
        unsafe {
            self.mapping
                .as_mut_ptr()
                .add(self.offset)
                .cast::<SharedLayout>()
        }
    }

    pub fn exchange(
        &mut self,
        inputs: &LockstepInputs,
        timeout: Duration,
    ) -> Result<(MotorCommand, FlightState)> {
        let shared = self.shared_mut_ptr();
        // SAFETY: generated payload structs have the same fixed v0.6 wire
        // layout on both sides. The release store publishes both completed
        // copies to the firmware.
        unsafe {
            ptr::copy_nonoverlapping(
                &inputs.inertial_sample,
                ptr::addr_of_mut!((*shared).inertial_sample),
                1,
            );
            ptr::copy_nonoverlapping(
                &inputs.manual_control,
                ptr::addr_of_mut!((*shared).manual_control),
                1,
            );
        }
        self.sequence = self.sequence.wrapping_add(1);
        if self.sequence == 0 {
            self.sequence = 1;
        }
        // SAFETY: the symbol and field alignment are guaranteed by repr(C)
        // and the firmware's corresponding C struct.
        unsafe { &*ptr::addr_of!((*shared).input_sequence) }
            .store(self.sequence, Ordering::Release);

        let deadline = Instant::now() + timeout;
        let mut spins = 0_u32;
        while unsafe { &*ptr::addr_of!((*shared).response_sequence) }.load(Ordering::Acquire)
            != self.sequence
        {
            spins = spins.wrapping_add(1);
            if spins.is_multiple_of(65_536) {
                if Instant::now() >= deadline {
                    bail!(
                        "timed out waiting for shared-memory response {}",
                        self.sequence
                    );
                }
                // Mostly-hot spinning keeps exchange latency low; this
                // periodic yield stops a hung firmware from pinning the
                // core for the entire timeout window.
                thread::yield_now();
            } else {
                std::hint::spin_loop();
            }
        }

        let mut pwm = topic::PwmSignalOutputsData::default();
        let mut health = topic::VehicleHealthData::default();
        // SAFETY: the acquire load above makes the firmware's completed output
        // payload writes visible before these generated-struct copies.
        unsafe {
            ptr::copy_nonoverlapping(ptr::addr_of!((*shared).pwm_signal_outputs), &mut pwm, 1);
            ptr::copy_nonoverlapping(ptr::addr_of!((*shared).vehicle_health), &mut health, 1);
        }
        Ok((
            protocol::motor_output(&pwm.0)?,
            protocol::flight_state(&health.0)?,
        ))
    }
}

impl Drop for Transport {
    fn drop(&mut self) {
        self.shared().terminate.store(1, Ordering::Release);
    }
}

const _: () = assert!(size_of::<SharedLayout>() == 320);

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::offset_of;

    #[test]
    fn layout_matches_firmware_abi() {
        assert_eq!(size_of::<SharedLayout>(), 320);
        assert_eq!(align_of::<SharedLayout>(), 4);
        assert_eq!(offset_of!(SharedLayout, inertial_sample), 16);
        assert_eq!(offset_of!(SharedLayout, manual_control), 72);
        assert_eq!(offset_of!(SharedLayout, pwm_signal_outputs), 112);
        assert_eq!(offset_of!(SharedLayout, vehicle_health), 160);
        assert_eq!(offset_of!(SharedLayout, attitude_estimate), 208);
        assert_eq!(offset_of!(SharedLayout, attitude_command), 248);
        assert_eq!(offset_of!(SharedLayout, control_loop_metrics), 296);
    }

    #[test]
    fn exchange_round_trips_through_the_shared_layout() {
        let path = std::env::temp_dir().join(format!(
            "fastdyn-rdd2-transport-test-{}",
            std::process::id()
        ));
        let file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        file.set_len(size_of::<SharedLayout>() as u64).unwrap();
        let mapping = unsafe { MmapOptions::new().map_mut(&file) }.unwrap();

        // The rehosted firmware maps the same file from another process;
        // a second mapping in a thread exercises the same coherence rules.
        let fw_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        let mut fw_mapping = unsafe { MmapOptions::new().map_mut(&fw_file) }.unwrap();
        let firmware = thread::spawn(move || {
            let shared = fw_mapping.as_mut_ptr().cast::<SharedLayout>();
            let sequence = loop {
                let sequence =
                    unsafe { &*ptr::addr_of!((*shared).input_sequence) }.load(Ordering::Acquire);
                if sequence != 0 {
                    break sequence;
                }
                thread::yield_now();
            };
            let received_timestamp_us =
                unsafe { ptr::addr_of!((*shared).inertial_sample).read() }.timestamp_us();
            let mut pwm = topic::PwmSignalOutputsData::default();
            pwm.set_output0_us(1750);
            let mut health = topic::VehicleHealthData::default();
            health.set_flags(topic::VehicleHealthFlags::Armed.bits());
            health.set_sensors_health(
                (topic::SensorComponentFlags::Gyro
                    | topic::SensorComponentFlags::Accel
                    | topic::SensorComponentFlags::RadioControl)
                    .bits(),
            );
            unsafe {
                ptr::copy_nonoverlapping(&pwm, ptr::addr_of_mut!((*shared).pwm_signal_outputs), 1);
                ptr::copy_nonoverlapping(&health, ptr::addr_of_mut!((*shared).vehicle_health), 1);
                (*ptr::addr_of!((*shared).response_sequence)).store(sequence, Ordering::Release);
            }
            received_timestamp_us
        });

        let mut transport = Transport {
            mapping,
            offset: 0,
            sequence: 0,
        };
        transport
            .shared()
            .magic
            .store(RDD2_LOCKSTEP_MAGIC, Ordering::Release);
        let inputs =
            protocol::lockstep_inputs([0.1, 0.2, 0.3], [0.0, 0.0, 9.8], [1500; 16], 5_000_000);
        let (motor, state) = transport
            .exchange(&inputs, Duration::from_secs(10))
            .unwrap();
        assert_eq!(motor.values[0], 0.75);
        assert!(state.armed && state.rc_valid && state.imu_ok);
        assert_eq!(firmware.join().unwrap(), 5_000);
        assert_eq!(transport.shared().terminate.load(Ordering::Acquire), 0);
        drop(transport);

        let final_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        let final_mapping = unsafe { MmapOptions::new().map_mut(&final_file) }.unwrap();
        let terminate_offset = offset_of!(SharedLayout, terminate);
        assert_eq!(
            u32::from_le_bytes(
                final_mapping[terminate_offset..terminate_offset + 4]
                    .try_into()
                    .unwrap()
            ),
            1,
            "dropping the transport must request firmware termination"
        );
        drop(final_mapping);
        fs::remove_file(path).unwrap();
    }
}
