use std::env;
use std::fs::{self, OpenOptions};
use std::os::unix::fs::symlink;
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::ptr;
use std::sync::atomic::{AtomicU32, Ordering, fence};
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, anyhow, bail};
use memmap2::{MmapMut, MmapOptions};
use object::{Object, ObjectSymbol};
use synapse_fbs::topic;

const SHARED_MAGIC: u32 = 0x4355_4253;
const SHARED_SYMBOL: &str = "cubs2_fastdyn_lockstep_shared";
const RAM_START_SYMBOL: &str = "_image_ram_start";

const ODOMETRY_SIZE: usize = size_of::<topic::OdometryData>();
const PWM_SIZE: usize = size_of::<topic::PwmSignalOutputsData>();
const ATTITUDE_SIZE: usize = size_of::<topic::AttitudeCommandData>();

#[repr(C, align(8))]
struct SharedLayout {
    magic: AtomicU32,
    input_sequence: AtomicU32,
    response_sequence: AtomicU32,
    terminate: AtomicU32,
    odometry: [u8; ODOMETRY_SIZE],
    pwm: [u8; PWM_SIZE],
    attitude: [u8; ATTITUDE_SIZE],
}

struct UpstreamTransport {
    mapping: MmapMut,
}

impl UpstreamTransport {
    fn open(path: &Path) -> Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path)
            .with_context(|| format!("cannot open upstream lockstep memory {}", path.display()))?;
        if file.metadata()?.len() < size_of::<SharedLayout>() as u64 {
            bail!("upstream lockstep memory is smaller than the CUBS2 ABI");
        }
        let mapping = unsafe {
            MmapOptions::new()
                .len(size_of::<SharedLayout>())
                .map_mut(&file)?
        };
        let transport = Self { mapping };
        if transport.shared().magic.load(Ordering::Acquire) != SHARED_MAGIC {
            bail!("upstream lockstep memory has invalid magic");
        }
        Ok(transport)
    }

    fn shared(&self) -> &SharedLayout {
        unsafe { &*self.mapping.as_ptr().cast::<SharedLayout>() }
    }

    fn shared_mut_ptr(&mut self) -> *mut SharedLayout {
        self.mapping.as_mut_ptr().cast::<SharedLayout>()
    }

    fn input_sequence(&self) -> u32 {
        self.shared().input_sequence.load(Ordering::Acquire)
    }

    fn terminated(&self) -> bool {
        self.shared().terminate.load(Ordering::Acquire) != 0
    }

    fn odometry(&self) -> [u8; ODOMETRY_SIZE] {
        self.shared().odometry
    }

    fn respond(&mut self, sequence: u32, pwm: &[u8; PWM_SIZE], attitude: &[u8; ATTITUDE_SIZE]) {
        let shared = self.shared_mut_ptr();
        unsafe {
            ptr::copy_nonoverlapping(pwm, ptr::addr_of_mut!((*shared).pwm), 1);
            ptr::copy_nonoverlapping(attitude, ptr::addr_of_mut!((*shared).attitude), 1);
        }
        fence(Ordering::Release);
        unsafe { &*ptr::addr_of!((*shared).response_sequence) }.store(sequence, Ordering::Release);
    }
}

struct FirmwareTransport {
    mapping: MmapMut,
    offset: usize,
}

fn symbol_address(elf: &object::File<'_>, name: &str) -> Result<u64> {
    elf.symbol_by_name(name)
        .map(|symbol| symbol.address())
        .ok_or_else(|| anyhow!("firmware ELF has no {name} symbol"))
}

fn open_mapping(path: &Path, required_len: usize, deadline: Instant) -> Result<MmapMut> {
    loop {
        if let Ok(file) = OpenOptions::new().read(true).write(true).open(path)
            && file.metadata()?.len() >= required_len as u64
        {
            return unsafe { MmapOptions::new().map_mut(&file) }
                .with_context(|| format!("cannot map FastDyn RAM file {}", path.display()));
        }
        if Instant::now() >= deadline {
            bail!("FastDyn RAM file {} was not prepared", path.display());
        }
        thread::sleep(Duration::from_millis(1));
    }
}

impl FirmwareTransport {
    fn open(memory_path: &Path, firmware_elf: &Path, timeout: Duration) -> Result<Self> {
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
        let mapping = open_mapping(memory_path, required_len, deadline)?;
        let shared_base = mapping.as_ptr() as usize + offset;
        if !shared_base.is_multiple_of(align_of::<SharedLayout>()) {
            bail!("{SHARED_SYMBOL} at RAM offset {offset:#x} is misaligned");
        }
        let transport = Self { mapping, offset };
        while transport.shared().magic.load(Ordering::Acquire) != SHARED_MAGIC {
            if Instant::now() >= deadline {
                bail!("CUBS2 firmware did not initialize {SHARED_SYMBOL}");
            }
            thread::yield_now();
        }
        Ok(transport)
    }

    fn shared(&self) -> &SharedLayout {
        unsafe {
            &*self
                .mapping
                .as_ptr()
                .add(self.offset)
                .cast::<SharedLayout>()
        }
    }

    fn shared_mut_ptr(&mut self) -> *mut SharedLayout {
        unsafe {
            self.mapping
                .as_mut_ptr()
                .add(self.offset)
                .cast::<SharedLayout>()
        }
    }

    fn exchange(
        &mut self,
        sequence: u32,
        odometry: &[u8; ODOMETRY_SIZE],
        timeout: Duration,
    ) -> Result<([u8; PWM_SIZE], [u8; ATTITUDE_SIZE])> {
        let shared = self.shared_mut_ptr();
        unsafe {
            ptr::copy_nonoverlapping(odometry, ptr::addr_of_mut!((*shared).odometry), 1);
        }
        unsafe { &*ptr::addr_of!((*shared).input_sequence) }.store(sequence, Ordering::Release);

        let deadline = Instant::now() + timeout;
        let mut spins = 0_u32;
        while unsafe { &*ptr::addr_of!((*shared).response_sequence) }.load(Ordering::Acquire)
            != sequence
        {
            spins = spins.wrapping_add(1);
            if spins.is_multiple_of(65_536) {
                if Instant::now() >= deadline {
                    bail!("timed out waiting for CUBS2 lockstep response {sequence}");
                }
                thread::yield_now();
            } else {
                std::hint::spin_loop();
            }
        }

        let mut pwm = [0_u8; PWM_SIZE];
        let mut attitude = [0_u8; ATTITUDE_SIZE];
        unsafe {
            ptr::copy_nonoverlapping(ptr::addr_of!((*shared).pwm), &mut pwm, 1);
            ptr::copy_nonoverlapping(ptr::addr_of!((*shared).attitude), &mut attitude, 1);
        }
        Ok((pwm, attitude))
    }
}

impl Drop for FirmwareTransport {
    fn drop(&mut self) {
        self.shared().terminate.store(1, Ordering::Release);
    }
}

#[derive(Debug)]
struct LaunchOptions {
    cubs2_root: PathBuf,
    cubs2_build_dir: PathBuf,
    artifacts: PathBuf,
    t_end: String,
    startup_timeout_s: String,
    sim_speed: String,
}

fn launch_options() -> Result<LaunchOptions> {
    let mut cubs2_root: Option<PathBuf> = None;
    let mut cubs2_build_dir: Option<PathBuf> = None;
    let mut artifacts: Option<PathBuf> = None;
    let mut t_end = "40".to_owned();
    let mut startup_timeout_s = "60".to_owned();
    let mut sim_speed = "1000".to_owned();
    let mut args = env::args().skip(2);
    while let Some(arg) = args.next() {
        let value = || anyhow!("{arg} requires a value");
        match arg.as_str() {
            "--cubs2-root" => cubs2_root = Some(args.next().ok_or_else(value)?.into()),
            "--cubs2-build-dir" => cubs2_build_dir = Some(args.next().ok_or_else(value)?.into()),
            "--artifacts" => artifacts = Some(args.next().ok_or_else(value)?.into()),
            "--t-end" => t_end = args.next().ok_or_else(value)?,
            "--startup-timeout-s" => startup_timeout_s = args.next().ok_or_else(value)?,
            "--sim-speed" => sim_speed = args.next().ok_or_else(value)?,
            _ => bail!("unknown launch argument: {arg}"),
        }
    }
    Ok(LaunchOptions {
        cubs2_root: fs::canonicalize(cubs2_root.context("--cubs2-root is required")?)?,
        cubs2_build_dir: fs::canonicalize(
            cubs2_build_dir.context("--cubs2-build-dir is required")?,
        )?,
        artifacts: artifacts.context("--artifacts is required")?,
        t_end,
        startup_timeout_s,
        sim_speed,
    })
}

fn replace_symlink(link: &Path, target: &Path) -> Result<()> {
    if link.symlink_metadata().is_ok() {
        fs::remove_file(link)
            .with_context(|| format!("cannot replace symlink {}", link.display()))?;
    }
    symlink(target, link)
        .with_context(|| format!("cannot link {} to {}", link.display(), target.display()))
}

fn launch_upstream() -> Result<()> {
    let options = launch_options()?;
    let source_headers = options
        .cubs2_build_dir
        .join("_deps/synapse_fbs_c-src/include");
    if !source_headers.is_dir() {
        bail!(
            "CUBS2 build has no generated synapse_fbs headers: {}",
            source_headers.display()
        );
    }

    let controller_build = options.artifacts.join(".fastdyn-controller-build");
    let simulated_executable = controller_build.join("zephyr/zephyr.exe");
    let header_link = controller_build.join("_deps/synapse_fbs_c-src/include");
    fs::create_dir_all(simulated_executable.parent().unwrap())?;
    fs::create_dir_all(header_link.parent().unwrap())?;
    replace_symlink(&simulated_executable, &env::current_exe()?)?;
    replace_symlink(&header_link, &source_headers)?;

    let error = Command::new("nix")
        .current_dir(&options.cubs2_root)
        .arg("run")
        .arg(format!(
            "{}#native-sim-sil-run",
            options.cubs2_root.display()
        ))
        .arg("--")
        .arg("--sim")
        .arg(simulated_executable)
        .arg("--plant-backend")
        .arg("fmi3")
        .arg("--artifacts")
        .arg(options.artifacts)
        .arg("--t-end")
        .arg(options.t_end)
        .arg("--startup-timeout-s")
        .arg(options.startup_timeout_s)
        .arg("--sim-speed")
        .arg(options.sim_speed)
        .exec();
    Err(error).context("cannot execute upstream CUBS2 FMI3 runner")
}

fn run_bridge() -> Result<()> {
    let upstream_path = env::var_os("CUBS2_NATIVE_SIL_SHM")
        .context("CUBS2_NATIVE_SIL_SHM was not supplied by the upstream runner")?;
    let memory_path = env::var_os("CUBS2_FASTDYN_SHARED_MEMORY")
        .context("CUBS2_FASTDYN_SHARED_MEMORY is required for direct lockstep")?;
    let firmware_elf = env::var_os("CUBS2_FASTDYN_FIRMWARE_ELF")
        .context("CUBS2_FASTDYN_FIRMWARE_ELF is required for direct lockstep")?;
    let startup_timeout = Duration::from_secs_f64(
        env::var("CUBS2_FASTDYN_STARTUP_TIMEOUT_S")
            .unwrap_or_else(|_| "60".to_owned())
            .parse()?,
    );
    let response_timeout = Duration::from_secs_f64(
        env::var("CUBS2_FASTDYN_RESPONSE_TIMEOUT_S")
            .unwrap_or_else(|_| "30".to_owned())
            .parse()?,
    );

    let mut upstream = UpstreamTransport::open(Path::new(&upstream_path))?;
    let mut firmware = FirmwareTransport::open(
        Path::new(&memory_path),
        Path::new(&firmware_elf),
        startup_timeout,
    )?;
    println!("FastDyn/CUBS2 direct shared-memory lockstep connected");

    let wall_start = Instant::now();
    let mut completed_sequence = upstream.shared().response_sequence.load(Ordering::Acquire);
    let mut idle_rounds = 0_u32;
    while !upstream.terminated() {
        let sequence = upstream.input_sequence();
        if sequence != completed_sequence {
            let odometry = upstream.odometry();
            let (pwm, attitude) = firmware.exchange(sequence, &odometry, response_timeout)?;
            upstream.respond(sequence, &pwm, &attitude);
            completed_sequence = sequence;
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
        "FastDyn/CUBS2 completed {completed_sequence} direct steps in {wall_s:.6}s ({:.1} steps/s)",
        f64::from(completed_sequence) / wall_s.max(f64::MIN_POSITIVE)
    );
    Ok(())
}

fn main() -> Result<()> {
    if env::args().nth(1).as_deref() == Some("--launch") {
        launch_upstream()
    } else {
        run_bridge()
    }
}

const _: () = assert!(size_of::<SharedLayout>() == 184);
const _: () = assert!(ODOMETRY_SIZE == 72);
const _: () = assert!(PWM_SIZE == 48);
const _: () = assert!(ATTITUDE_SIZE == 48);

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::offset_of;

    #[test]
    fn layout_matches_firmware_and_upstream_abi() {
        assert_eq!(size_of::<SharedLayout>(), 184);
        assert_eq!(align_of::<SharedLayout>(), 8);
        assert_eq!(offset_of!(SharedLayout, odometry), 16);
        assert_eq!(offset_of!(SharedLayout, pwm), 88);
        assert_eq!(offset_of!(SharedLayout, attitude), 136);
    }
}
