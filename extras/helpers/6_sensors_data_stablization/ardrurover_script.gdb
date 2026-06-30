set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

set $spid1 = &SPID1
set $spid2 = &SPID2
set $spid4 = &SPID4
set $probe_events = 0
set $spi1_events = 0
set $spi2_events = 0
set $spi4_events = 0
set $reg_read_events = 0
set $storage_events = 0
set $ramtron_events = 0
set $param_events = 0
set $scheduler_loop_events = 0
set $scheduler_run_events = 0
set $wait_sample_events = 0
set $wait_sample_return_events = 0
set $after_sample_events = 0
set $ahrs_events = 0
set $read_radio_events = 0
set $one_second_events = 0
set $ins_periodic_events = 0
set $inv_poll_events = 0
set $inv_fifo_events = 0
set $inv_fifo_count_events = 0
set $inv_accum_events = 0
set $accel_notify_events = 0
set $gyro_notify_events = 0
set $runtime_stop_at = 200

printf "\n[diag] FastDyn idle-starvation diagnostic script loaded\n"
printf "[diag] SPID1 = %p\n", $spid1
printf "[diag] SPID2 = %p\n", $spid2
printf "[diag] SPID4 = %p\n", $spid4

python
import gdb


def _safe_str(value):
    try:
        if value is None:
            return "<none>"
        return str(value)
    except Exception as exc:
        return "<unavailable:%s>" % exc


def _safe_int(value):
    try:
        return int(value)
    except Exception:
        try:
            return int(value.cast(gdb.lookup_type("unsigned long")))
        except Exception:
            return None


def _safe_eval(expr):
    try:
        return gdb.parse_and_eval(expr)
    except Exception:
        return None


def _safe_frame_var(frame, name):
    try:
        return frame.read_var(name)
    except Exception:
        return None


def _safe_cstr(value):
    try:
        if value is None:
            return "<unavailable>"
        addr = _safe_int(value)
        if addr == 0:
            return "<null>"
        return value.string(errors="replace")
    except Exception as exc:
        return "<unavailable:%s>" % exc


def _find_stack_var(name, max_depth=12):
    frame = gdb.selected_frame()
    depth = 0
    while frame is not None and depth < max_depth:
        value = _safe_frame_var(frame, name)
        if value is not None:
            return value
        frame = frame.older()
        depth += 1
    return None


def _read_bytes(addr_value, length_value, max_len=16):
    addr = _safe_int(addr_value)
    length = _safe_int(length_value)
    if addr is None or addr == 0 or length is None or length <= 0:
        return "<none>"
    n = min(length, max_len)
    try:
        mem = gdb.selected_inferior().read_memory(addr, n)
        return " ".join("%02x" % b for b in bytes(mem))
    except Exception as exc:
        return "<read-failed:%s>" % exc


def _bt(limit=8):
    try:
        gdb.execute("bt %d" % limit, to_string=False)
    except Exception as exc:
        print("[diag] backtrace failed: %s" % exc)


def _selected_func_name():
    try:
        block = gdb.selected_frame().block()
        if block and block.function:
            return block.function.print_name
    except Exception:
        pass
    return "<unknown>"


def _gdb_counter(name):
    return _safe_int(_safe_eval("$%s" % name)) or 0


def _maybe_print(count, first=5, every=50):
    return count <= first or (every > 0 and count % every == 0)


def _inc_counter(name):
    try:
        gdb.execute("set $%s = $%s + 1" % (name, name), to_string=True)
    except Exception as exc:
        print("[diag] counter increment failed %s: %s" % (name, exc))
    return _gdb_counter(name)


def _eval_at(type_name, addr, field):
    if addr is None:
        return None
    return _safe_eval("((%s*)0x%x)->%s" % (type_name, addr, field))


def _dump_ins_state(label, this_addr):
    if this_addr is None:
        print("[%s] INS state unavailable: missing this" % label)
        return
    fields = [
        "_backend_count",
        "_gyro_count",
        "_accel_count",
        "_gyro_wait_mask",
        "_accel_wait_mask",
        "_have_sample",
        "_sample_period_usec",
        "_last_sample_usec",
        "_next_sample_usec",
        "_delta_time",
    ]
    parts = []
    for field in fields:
        parts.append("%s=%s" % (field, _safe_str(_eval_at("AP_InertialSensor", this_addr, field))))
    print("[%s] INS %s" % (label, " ".join(parts)))
    for i in range(3):
        print(
            "[%s] INS[%u] new_accel=%s new_gyro=%s accel_healthy=%s gyro_healthy=%s accel_err=%s gyro_err=%s"
            % (
                label,
                i,
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_new_accel_data[%u]" % i)),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_new_gyro_data[%u]" % i)),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_accel_healthy[%u]" % i)),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_healthy[%u]" % i)),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_accel_error_count[%u]" % i)),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_error_count[%u]" % i)),
            )
        )


def _runtime_summary(reason):
    print("\n[summary] stopping diagnostic: %s" % reason)
    print(
        "[summary] scheduler_loop=%u scheduler_run=%u wait_sample_enter=%u wait_sample_return=%u after_sample=%u"
        % (
            _gdb_counter("scheduler_loop_events"),
            _gdb_counter("scheduler_run_events"),
            _gdb_counter("wait_sample_events"),
            _gdb_counter("wait_sample_return_events"),
            _gdb_counter("after_sample_events"),
        )
    )
    print(
        "[summary] rover_tasks ahrs=%u read_radio=%u one_second=%u ins_periodic=%u"
        % (
            _gdb_counter("ahrs_events"),
            _gdb_counter("read_radio_events"),
            _gdb_counter("one_second_events"),
            _gdb_counter("ins_periodic_events"),
        )
    )
    print(
        "[summary] invensense poll=%u fifo=%u fifo_count=%u accum=%u accel_notify=%u gyro_notify=%u"
        % (
            _gdb_counter("inv_poll_events"),
            _gdb_counter("inv_fifo_events"),
            _gdb_counter("inv_fifo_count_events"),
            _gdb_counter("inv_accum_events"),
            _gdb_counter("accel_notify_events"),
            _gdb_counter("gyro_notify_events"),
        )
    )
    _bt(12)


class PrintReturn(gdb.FinishBreakpoint):
    def __init__(self, label, context=None, recv_ptr=None, recv_len=None):
        super(PrintReturn, self).__init__(gdb.selected_frame(), True)
        self.label = label
        self.context = context or {}
        self.recv_ptr = recv_ptr
        self.recv_len = recv_len

    def stop(self):
        ret = _safe_str(getattr(self, "return_value", None))
        if self.context:
            ctx = " ".join("%s=%s" % (k, v) for k, v in self.context.items())
            print("[return] %s %s ret=%s" % (self.label, ctx, ret))
        else:
            print("[return] %s ret=%s" % (self.label, ret))
        if self.recv_ptr is not None and self.recv_len is not None:
            print("[return] %s recv_bytes=%s" % (self.label, _read_bytes(self.recv_ptr, self.recv_len, 16)))
        return False


class InspectReturn(gdb.FinishBreakpoint):
    def __init__(self, label, callback=None):
        super(InspectReturn, self).__init__(gdb.selected_frame(), True)
        self.label = label
        self.callback = callback

    def stop(self):
        ret = _safe_str(getattr(self, "return_value", None))
        print("[return] %s ret=%s" % (self.label, ret))
        if self.callback is not None:
            try:
                self.callback(ret)
            except Exception as exc:
                print("[return] %s callback_failed=%s" % (self.label, exc))
        return False


class FatalBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(FatalBreakpoint, self).__init__("AP_BoardConfig::throw_error", internal=False)

    def stop(self):
        err_type = _safe_cstr(_safe_frame_var(gdb.selected_frame(), "err_type"))
        fmt = _safe_cstr(_safe_frame_var(gdb.selected_frame(), "fmt"))
        print("\n[FATAL] AP_BoardConfig::throw_error")
        print("[FATAL] err_type=%s fmt=%s" % (err_type, fmt))
        _bt(14)
        print("[FATAL] stopped for inspection")
        return True


class BoardValidationFailBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(BoardValidationFailBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_BoardConfig/board_drivers.cpp:306",
            internal=False,
        )

    def stop(self):
        errored = _safe_cstr(_safe_frame_var(gdb.selected_frame(), "errored_check"))
        print("\n[board] HAL_VALIDATE_BOARD failure candidate errored_check=%s" % errored)
        _bt(8)
        return False


class CheckMS5611Breakpoint(gdb.Breakpoint):
    def __init__(self):
        super(CheckMS5611Breakpoint, self).__init__("AP_BoardConfig::check_ms5611", internal=False)

    def stop(self):
        devname_val = _safe_frame_var(gdb.selected_frame(), "devname")
        devname = _safe_cstr(devname_val)
        gdb.execute("set $probe_events = $probe_events + 1", to_string=True)
        print("\n[probe] check_ms5611 enter dev=%s" % devname)
        PrintReturn("check_ms5611", {"dev": devname})
        return False


class SpiCheckRegisterBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label):
        super(SpiCheckRegisterBreakpoint, self).__init__(symbol, internal=False)
        self.label = label

    def stop(self):
        frame = gdb.selected_frame()
        devname = _safe_cstr(_safe_frame_var(frame, "devname"))
        regnum = _safe_int(_safe_frame_var(frame, "regnum"))
        expected = _safe_int(_safe_frame_var(frame, "value"))
        read_flag = _safe_int(_safe_frame_var(frame, "read_flag"))
        gdb.execute("set $probe_events = $probe_events + 1", to_string=True)
        print(
            "\n[probe] %s enter dev=%s reg=0x%02x expected=0x%02x read_flag=0x%02x"
            % (
                self.label,
                devname,
                regnum if regnum is not None else 0,
                expected if expected is not None else 0,
                read_flag if read_flag is not None else 0,
            )
        )
        PrintReturn(self.label, {"dev": devname, "reg": "0x%02x" % (regnum if regnum is not None else 0), "expected": "0x%02x" % (expected if expected is not None else 0)})
        return False


class DeviceReadRegistersBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(DeviceReadRegistersBreakpoint, self).__init__("AP_HAL::Device::read_registers", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        first_reg = _safe_int(_safe_frame_var(frame, "first_reg"))
        recv = _safe_frame_var(frame, "recv")
        recv_len = _safe_frame_var(frame, "recv_len")
        recv_len_i = _safe_int(recv_len)
        devname = _safe_cstr(_find_stack_var("devname"))
        gdb.execute("set $reg_read_events = $reg_read_events + 1", to_string=True)
        print(
            "[reg-read] enter caller_dev=%s first_reg=0x%02x recv=%s len=%s"
            % (
                devname,
                first_reg if first_reg is not None else 0,
                _safe_str(recv),
                recv_len_i if recv_len_i is not None else "<unavailable>",
            )
        )
        PrintReturn(
            "AP_HAL::Device::read_registers",
            {"caller_dev": devname, "first_reg": "0x%02x" % (first_reg if first_reg is not None else 0)},
            recv,
            recv_len,
        )
        return False


class SPIDoTransferBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SPIDoTransferBreakpoint, self).__init__("ChibiOS::SPIDevice::do_transfer", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        send = _safe_frame_var(frame, "send")
        recv = _safe_frame_var(frame, "recv")
        length = _safe_frame_var(frame, "len")
        bus = _safe_int(_safe_eval("this->device_desc.bus"))
        device = _safe_int(_safe_eval("this->device_desc.device"))
        name = _safe_cstr(_safe_eval("this->device_desc.name"))
        pal = _safe_int(_safe_eval("this->device_desc.pal_line"))
        if bus not in (0, 1, 2):
            return False
        if bus == 0:
            gdb.execute("set $spi1_events = $spi1_events + 1", to_string=True)
            bus_label = "spi1"
        elif bus == 1:
            gdb.execute("set $spi2_events = $spi2_events + 1", to_string=True)
            bus_label = "spi2"
            spi2_count = _safe_int(_safe_eval("$spi2_events")) or 0
            if spi2_count > 16:
                return False
        else:
            gdb.execute("set $spi4_events = $spi4_events + 1", to_string=True)
            bus_label = "spi4"
        port = None
        port_idx = None
        pad = None
        signal_id = None
        if pal is not None:
            port = pal & 0xFFFFFFF0
            pad = pal & 0xF
            port_idx = int((port - 0x40020000) / 0x400)
            signal_id = (port_idx * 16) + pad
        print(
            "\n[%s] do_transfer dev=%s bus=%s device=%s len=%s send=%s recv=%s pal_line=%s port_base=%s port_idx=%s pad=%s signal_id=%s"
            % (
                bus_label,
                name,
                bus,
                device,
                _safe_int(length),
                _safe_str(send),
                _safe_str(recv),
                "0x%08x" % pal if pal is not None else "<unavailable>",
                "0x%08x" % port if port is not None else "<unavailable>",
                port_idx if port_idx is not None else "<unavailable>",
                pad if pad is not None else "<unavailable>",
                signal_id if signal_id is not None else "<unavailable>",
            )
        )
        print("[%s] tx_bytes=%s" % (bus_label, _read_bytes(send, length, 16)))
        PrintReturn("ChibiOS::SPIDevice::do_transfer", {"dev": name, "bus": bus_label, "device": device}, recv, length)
        return False


class GetDeviceBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GetDeviceBreakpoint, self).__init__("ChibiOS::SPIDeviceManager::get_device", internal=False)

    def stop(self):
        name = _safe_cstr(_safe_frame_var(gdb.selected_frame(), "name"))
        print("\n[spi] get_device name=%s" % name)
        return False


class SpiLldStartBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SpiLldStartBreakpoint, self).__init__("spi_lld_start", internal=False)

    def stop(self):
        spip = _safe_frame_var(gdb.selected_frame(), "spip")
        spip_i = _safe_int(spip)
        spid1_i = _safe_int(_safe_eval("&SPID1"))
        spid2_i = _safe_int(_safe_eval("&SPID2"))
        spid4_i = _safe_int(_safe_eval("&SPID4"))
        if spip_i == spid1_i:
            print("\n[spi1] spi_lld_start spip=%s" % _safe_str(spip))
            _bt(6)
        elif spip_i == spid2_i:
            count = _safe_int(_safe_eval("$spi2_events")) or 0
            if count <= 4:
                print("\n[spi2] spi_lld_start spip=%s" % _safe_str(spip))
                _bt(6)
        elif spip_i == spid4_i:
            print("\n[spi4] spi_lld_start spip=%s" % _safe_str(spip))
            _bt(6)
        return False


class StorageOpenBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(StorageOpenBreakpoint, self).__init__("ChibiOS::Storage::_storage_open", internal=False)

    def stop(self):
        gdb.execute("set $storage_events = $storage_events + 1", to_string=True)
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        initialised = _safe_str(_safe_eval("this->_initialisedType"))
        print("\n[storage] _storage_open enter this=%s initialisedType=%s" % (_safe_str(this), initialised))

        def on_return(_ret):
            if this_addr is None:
                print("[storage] _storage_open exit initialisedType=<unknown-this>")
                return
            print("[storage] _storage_open exit initialisedType=%s" % _safe_str(_safe_eval("((ChibiOS::Storage*)0x%x)->_initialisedType" % this_addr)))

        InspectReturn("ChibiOS::Storage::_storage_open", on_return)
        return False


class StorageReadBlockBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(StorageReadBlockBreakpoint, self).__init__("ChibiOS::Storage::read_block", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        dst = _safe_frame_var(frame, "dst")
        loc = _safe_frame_var(frame, "loc")
        n = _safe_frame_var(frame, "n")
        loc_i = _safe_int(loc)
        n_i = _safe_int(n)
        gdb.execute("set $storage_events = $storage_events + 1", to_string=True)
        if _safe_int(_safe_eval("$storage_events")) <= 20:
            print("\n[storage] read_block loc=%s n=%s dst=%s" % (loc_i, n_i, _safe_str(dst)))

            def on_return(_ret):
                print("[storage] read_block data=%s" % _read_bytes(dst, n, 16))

            InspectReturn("ChibiOS::Storage::read_block", on_return)
        return False


class StorageWriteBlockBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(StorageWriteBlockBreakpoint, self).__init__("ChibiOS::Storage::write_block", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        loc = _safe_frame_var(frame, "loc")
        src = _safe_frame_var(frame, "src")
        n = _safe_frame_var(frame, "n")
        gdb.execute("set $storage_events = $storage_events + 1", to_string=True)
        print("\n[storage] write_block loc=%s n=%s src=%s bytes=%s" % (_safe_int(loc), _safe_int(n), _safe_str(src), _read_bytes(src, n, 16)))
        return False


class StorageTimerTickBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(StorageTimerTickBreakpoint, self).__init__("ChibiOS::Storage::_timer_tick", internal=False)

    def stop(self):
        # Disabled by default: this fires constantly while other threads wait
        # and makes storage diagnostics crawl under GDB.
        return False


class RamtronInitBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(RamtronInitBreakpoint, self).__init__("AP_RAMTRON::init", internal=False)

    def stop(self):
        gdb.execute("set $ramtron_events = $ramtron_events + 1", to_string=True)
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        print("\n[ramtron] init enter")

        def on_return(_ret):
            if this_addr is None:
                print("[ramtron] init exit id=<unknown-this>")
                return
            print("[ramtron] init exit id=%s" % _safe_str(_safe_eval("((AP_RAMTRON*)0x%x)->id" % this_addr)))

        InspectReturn("AP_RAMTRON::init", on_return)
        return False


class RamtronReadBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(RamtronReadBreakpoint, self).__init__("AP_RAMTRON::read", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        offset = _safe_frame_var(frame, "offset")
        buf = _safe_frame_var(frame, "buf")
        size = _safe_frame_var(frame, "size")
        size_i = _safe_int(size)
        if size_i is not None and size_i < 1024:
            return False
        gdb.execute("set $ramtron_events = $ramtron_events + 1", to_string=True)
        print("\n[ramtron] read offset=%s size=%s buf=%s" % (_safe_int(offset), size_i, _safe_str(buf)))

        def on_return(_ret):
            print("[ramtron] read data=%s" % _read_bytes(buf, size, 32))

        InspectReturn("AP_RAMTRON::read", on_return)
        return False


class RamtronWriteBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(RamtronWriteBreakpoint, self).__init__("AP_RAMTRON::write", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        offset = _safe_frame_var(frame, "offset")
        buf = _safe_frame_var(frame, "buf")
        size = _safe_frame_var(frame, "size")
        gdb.execute("set $ramtron_events = $ramtron_events + 1", to_string=True)
        print("\n[ramtron] write offset=%s size=%s buf=%s bytes=%s" % (_safe_int(offset), _safe_int(size), _safe_str(buf), _read_bytes(buf, size, 32)))
        InspectReturn("AP_RAMTRON::write")
        return False


class APParamLoadAllBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(APParamLoadAllBreakpoint, self).__init__("AP_Param::load_all", internal=False)

    def stop(self):
        gdb.execute("set $param_events = $param_events + 1", to_string=True)
        print("\n[param] load_all enter")
        InspectReturn("AP_Param::load_all")
        return False


class APParamSaveIoHandlerBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(APParamSaveIoHandlerBreakpoint, self).__init__("AP_Param::save_io_handler", internal=False)

    def stop(self):
        gdb.execute("set $param_events = $param_events + 1", to_string=True)
        count = _safe_int(_safe_eval("$param_events")) or 0
        if count <= 80 or count % 100 == 0:
            print("\n[param] save_io_handler hit=%s" % count)
            InspectReturn("AP_Param::save_io_handler")
        return False


class APParamSaveSyncBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(APParamSaveSyncBreakpoint, self).__init__("AP_Param::save_sync", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        force_save = _safe_int(_safe_frame_var(frame, "force_save"))
        send_to_gcs = _safe_int(_safe_frame_var(frame, "send_to_gcs"))
        print("\n[param] save_sync this=%s force_save=%s send_to_gcs=%s" % (_safe_str(_safe_frame_var(frame, "this")), force_save, send_to_gcs))
        InspectReturn("AP_Param::save_sync")
        return False


class APParamScanBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(APParamScanBreakpoint, self).__init__("AP_Param::scan", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        target = _safe_frame_var(frame, "target")
        pofs = _safe_frame_var(frame, "pofs")
        gdb.execute("set $param_events = $param_events + 1", to_string=True)
        count = _safe_int(_safe_eval("$param_events")) or 0
        if count > 40:
            return False
        print("\n[param] scan target=%s target_bytes=%s pofs=%s" % (_safe_str(target), _read_bytes(target, 4, 4), _safe_str(pofs)))

        def on_return(_ret):
            pofs_addr = _safe_int(pofs)
            if pofs_addr:
                try:
                    raw = gdb.selected_inferior().read_memory(pofs_addr, 2)
                    data = bytes(raw)
                    val = data[0] | (data[1] << 8)
                    print("[param] scan pofs_value=0x%04x" % val)
                except Exception as exc:
                    print("[param] scan pofs_read_failed=%s" % exc)

        InspectReturn("AP_Param::scan", on_return)
        return False


class APParamEepromWriteCheckBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(APParamEepromWriteCheckBreakpoint, self).__init__("AP_Param::eeprom_write_check", internal=False)

    def stop(self):
        frame = gdb.selected_frame()
        ptr = _safe_frame_var(frame, "ptr")
        ofs = _safe_frame_var(frame, "ofs")
        size = _safe_frame_var(frame, "size")
        print("\n[param] eeprom_write_check ofs=%s size=%s ptr=%s bytes=%s" % (_safe_int(ofs), _safe_int(size), _safe_str(ptr), _read_bytes(ptr, size, 16)))
        return False


class HalPanicBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(HalPanicBreakpoint, self).__init__("AP_HAL::panic", internal=False)

    def stop(self):
        print("\n[PANIC] AP_HAL::panic")
        _bt(14)
        return True


class BootProgressBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label):
        super(BootProgressBreakpoint, self).__init__(symbol, internal=False)
        self.label = label
        self.silent = False

    def stop(self):
        print("\n[progress] %s enter" % self.label)
        InspectReturn(self.label)
        return False


class SpiLldAbortBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SpiLldAbortBreakpoint, self).__init__("spi_lld_abort", internal=False)

    def stop(self):
        spip = _safe_frame_var(gdb.selected_frame(), "spip")
        print("\n[spi] spi_lld_abort spip=%s" % _safe_str(spip))
        _bt(12)
        print("[spi] stopped on abort for inspection")
        return True


class SchedulerLoopBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SchedulerLoopBreakpoint, self).__init__("AP_Scheduler::loop", internal=False)

    def stop(self):
        count = _inc_counter("scheduler_loop_events")
        if _maybe_print(count, first=8, every=50):
            print("\n[runtime] AP_Scheduler::loop hit=%u" % count)
        stop_at = _gdb_counter("runtime_stop_at")
        if stop_at > 0 and count >= stop_at:
            _runtime_summary("sampled %u scheduler loops" % count)
            return True
        return False


class SchedulerAfterSampleBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SchedulerAfterSampleBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_Scheduler/AP_Scheduler.cpp:348",
            internal=False,
        )

    def stop(self):
        count = _inc_counter("after_sample_events")
        if _maybe_print(count, first=8, every=50):
            print("[runtime] AP_Scheduler::loop passed wait_for_sample hit=%u" % count)
        return False


class SchedulerRunBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SchedulerRunBreakpoint, self).__init__("AP_Scheduler::run", internal=False)

    def stop(self):
        count = _inc_counter("scheduler_run_events")
        frame = gdb.selected_frame()
        time_available = _safe_int(_safe_frame_var(frame, "time_available"))
        if _maybe_print(count, first=8, every=50):
            print("[runtime] AP_Scheduler::run hit=%u time_available=%s" % (count, time_available if time_available is not None else "<unavailable>"))
        return False


class WaitForSampleBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(WaitForSampleBreakpoint, self).__init__("AP_InertialSensor::wait_for_sample", internal=False)

    def stop(self):
        count = _inc_counter("wait_sample_events")
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        if _maybe_print(count, first=8, every=50):
            print("\n[ins] wait_for_sample enter hit=%u this=%s" % (count, _safe_str(this)))
            _dump_ins_state("ins-enter", this_addr)

            def on_return(_ret):
                _inc_counter("wait_sample_return_events")
                print("[ins] wait_for_sample returned hit=%u" % _gdb_counter("wait_sample_return_events"))
                _dump_ins_state("ins-return", this_addr)

            InspectReturn("AP_InertialSensor::wait_for_sample", on_return)
        return False


class RuntimeCounterBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label, counter_name, first=8, every=50):
        super(RuntimeCounterBreakpoint, self).__init__(symbol, internal=False)
        self.label = label
        self.counter_name = counter_name
        self.first = first
        self.every = every

    def stop(self):
        count = _inc_counter(self.counter_name)
        if _maybe_print(count, first=self.first, every=self.every):
            print("[runtime] %s hit=%u" % (self.label, count))
        return False


class InvensensePollBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensensePollBreakpoint, self).__init__("AP_InertialSensor_Invensense::_poll_data", internal=False)

    def stop(self):
        count = _inc_counter("inv_poll_events")
        if count > 120:
            self.enabled = False
            print("[imu] disabling Invensense::_poll_data breakpoint after %u hits" % count)
            return False
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        if _maybe_print(count, first=10, every=100):
            print(
                "\n[imu] Invensense::_poll_data hit=%u this=%s mpu_type=%s accel_instance=%s gyro_instance=%s fast_sampling=%s raw_temp=%s"
                % (
                    count,
                    _safe_str(this),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_mpu_type")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "accel_instance")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "gyro_instance")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_fast_sampling")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_raw_temp")),
                )
            )
        return False


class InvensenseReadFifoBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseReadFifoBreakpoint, self).__init__("AP_InertialSensor_Invensense::_read_fifo", internal=False)

    def stop(self):
        count = _inc_counter("inv_fifo_events")
        if count > 120:
            self.enabled = False
            print("[imu] disabling Invensense::_read_fifo breakpoint after %u hits" % count)
            return False
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        if _maybe_print(count, first=10, every=100):
            print(
                "[imu] Invensense::_read_fifo enter hit=%u this=%s mpu_type=%s fast_sampling=%s"
                % (
                    count,
                    _safe_str(this),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_mpu_type")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_fast_sampling")),
                )
            )
        return False


class InvensenseFifoCountBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseFifoCountBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_InertialSensor/AP_InertialSensor_Invensense.cpp:737",
            internal=False,
        )

    def stop(self):
        count = _inc_counter("inv_fifo_count_events")
        if count > 160:
            self.enabled = False
            print("[imu] disabling fifo_count breakpoint after %u hits" % count)
            return False
        frame = gdb.selected_frame()
        bytes_read = _safe_int(_safe_frame_var(frame, "bytes_read"))
        n_samples = _safe_int(_safe_frame_var(frame, "n_samples"))
        rx = _safe_frame_var(frame, "rx")
        if _maybe_print(count, first=20, every=100):
            print(
                "[imu] fifo_count hit=%u bytes_read=%s n_samples=%s rx=%s"
                % (
                    count,
                    bytes_read if bytes_read is not None else "<unavailable>",
                    n_samples if n_samples is not None else "<unavailable>",
                    _read_bytes(rx, 16, 16),
                )
            )
        return False


class InvensenseAccumulateBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label):
        super(InvensenseAccumulateBreakpoint, self).__init__(symbol, internal=False)
        self.label = label

    def stop(self):
        count = _inc_counter("inv_accum_events")
        if count > 80:
            self.enabled = False
            print("[imu] disabling %s breakpoint after %u hits" % (self.label, count))
            return False
        frame = gdb.selected_frame()
        samples = _safe_frame_var(frame, "samples")
        n_samples = _safe_int(_safe_frame_var(frame, "n_samples"))
        if _maybe_print(count, first=12, every=100):
            print(
                "[imu] %s hit=%u n_samples=%s sample_bytes=%s"
                % (
                    self.label,
                    count,
                    n_samples if n_samples is not None else "<unavailable>",
                    _read_bytes(samples, 16, 16),
                )
            )
        return False


class SampleNotifyBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label, counter_name):
        super(SampleNotifyBreakpoint, self).__init__(symbol, internal=False)
        self.label = label
        self.counter_name = counter_name

    def stop(self):
        count = _inc_counter(self.counter_name)
        if count > 40:
            self.enabled = False
            print("[imu] disabling %s breakpoint after %u hits" % (self.label, count))
            return False
        frame = gdb.selected_frame()
        instance = _safe_int(_safe_frame_var(frame, "instance"))
        if _maybe_print(count, first=12, every=200):
            print("[imu] %s hit=%u instance=%s" % (self.label, count, instance if instance is not None else "<unavailable>"))
        return False


def install(bp_cls, *args):
    try:
        bp = bp_cls(*args)
        print("[diag] installed breakpoint: %s" % bp.location)
    except Exception as exc:
        print("[diag] could not install %s: %s" % (bp_cls.__name__, exc))


install(FatalBreakpoint)
install(BoardValidationFailBreakpoint)
install(CheckMS5611Breakpoint)
install(SpiCheckRegisterBreakpoint, "AP_BoardConfig::spi_check_register", "spi_check_register")
install(SpiCheckRegisterBreakpoint, "AP_BoardConfig::spi_check_register_inv2", "spi_check_register_inv2")
install(GetDeviceBreakpoint)
# Low-level SPI/storage/AP_Param breakpoints are intentionally not installed by
# default. They fire thousands of times after FRAM comes up and make GDB too slow
# for idle-starvation triage. Re-enable individual installs only after the broad
# progress hooks identify the blocked subsystem.
# Do not install StorageTimerTickBreakpoint unless debugging dirty-line flushes;
# it fires too often for idle-starvation triage.
install(RamtronInitBreakpoint)
install(APParamLoadAllBreakpoint)
install(HalPanicBreakpoint)
install(BootProgressBreakpoint, "AP_BoardConfig::init", "AP_BoardConfig::init")
install(BootProgressBreakpoint, "AP_BoardConfig::board_setup", "AP_BoardConfig::board_setup")
install(BootProgressBreakpoint, "AP_Vehicle::setup", "AP_Vehicle::setup")
install(BootProgressBreakpoint, "Rover::init_ardupilot", "Rover::init_ardupilot")
install(BootProgressBreakpoint, "Rover::startup_INS", "Rover::startup_INS")
install(SpiLldAbortBreakpoint)
install(SchedulerLoopBreakpoint)
install(SchedulerAfterSampleBreakpoint)
install(SchedulerRunBreakpoint)
install(WaitForSampleBreakpoint)
install(RuntimeCounterBreakpoint, "Rover::ahrs_update", "Rover::ahrs_update", "ahrs_events", 8, 50)
install(RuntimeCounterBreakpoint, "Rover::read_radio", "Rover::read_radio", "read_radio_events", 8, 50)
install(RuntimeCounterBreakpoint, "Rover::one_second_loop", "Rover::one_second_loop", "one_second_events", 4, 1)
install(RuntimeCounterBreakpoint, "AP_InertialSensor::periodic", "AP_InertialSensor::periodic", "ins_periodic_events", 8, 50)
install(InvensensePollBreakpoint)
install(InvensenseReadFifoBreakpoint)
install(InvensenseFifoCountBreakpoint)
install(InvensenseAccumulateBreakpoint, "AP_InertialSensor_Invensense::_accumulate_sensor_rate_sampling", "Invensense::_accumulate_sensor_rate_sampling")
install(InvensenseAccumulateBreakpoint, "AP_InertialSensor_Invensense::_accumulate", "Invensense::_accumulate")
install(SampleNotifyBreakpoint, "AP_InertialSensor_Backend::_notify_new_accel_raw_sample", "accel_raw_sample", "accel_notify_events")
install(SampleNotifyBreakpoint, "AP_InertialSensor_Backend::_notify_new_gyro_raw_sample", "gyro_raw_sample", "gyro_notify_events")

print("[diag] breakpoints installed; continuing target")
end

continue
