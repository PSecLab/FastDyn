set pagination off
set confirm off
set print thread-events off
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
        initialised = _safe_str(_safe_eval("this->_initialisedType"))
        print("\n[storage] _storage_open enter this=%s initialisedType=%s" % (_safe_str(this), initialised))

        def on_return(_ret):
            print("[storage] _storage_open exit initialisedType=%s" % _safe_str(_safe_eval("this->_initialisedType")))

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
        if _safe_int(_safe_eval("$storage_events")) <= 120:
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
        gdb.execute("set $storage_events = $storage_events + 1", to_string=True)
        count = _safe_int(_safe_eval("$storage_events")) or 0
        if count <= 40 or count % 100 == 0:
            print("\n[storage] _timer_tick hit=%s initialisedType=%s" % (count, _safe_str(_safe_eval("this->_initialisedType"))))
            if count <= 5:
                _bt(6)
        return False


class RamtronInitBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(RamtronInitBreakpoint, self).__init__("AP_RAMTRON::init", internal=False)

    def stop(self):
        gdb.execute("set $ramtron_events = $ramtron_events + 1", to_string=True)
        print("\n[ramtron] init enter")

        def on_return(_ret):
            print("[ramtron] init exit id=%s" % _safe_str(_safe_eval("this->id")))

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
        gdb.execute("set $ramtron_events = $ramtron_events + 1", to_string=True)
        print("\n[ramtron] read offset=%s size=%s buf=%s" % (_safe_int(offset), _safe_int(size), _safe_str(buf)))

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


class SpiLldAbortBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SpiLldAbortBreakpoint, self).__init__("spi_lld_abort", internal=False)

    def stop(self):
        spip = _safe_frame_var(gdb.selected_frame(), "spip")
        print("\n[spi] spi_lld_abort spip=%s" % _safe_str(spip))
        _bt(12)
        print("[spi] stopped on abort for inspection")
        return True


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
install(DeviceReadRegistersBreakpoint)
install(SPIDoTransferBreakpoint)
install(GetDeviceBreakpoint)
install(SpiLldStartBreakpoint)
install(StorageOpenBreakpoint)
install(StorageReadBlockBreakpoint)
install(StorageWriteBlockBreakpoint)
install(StorageTimerTickBreakpoint)
install(RamtronInitBreakpoint)
install(RamtronReadBreakpoint)
install(RamtronWriteBreakpoint)
install(APParamLoadAllBreakpoint)
install(APParamSaveIoHandlerBreakpoint)
install(APParamSaveSyncBreakpoint)
install(APParamScanBreakpoint)
install(APParamEepromWriteCheckBreakpoint)
install(HalPanicBreakpoint)
install(SpiLldAbortBreakpoint)

print("[diag] breakpoints installed; continuing target")
end

continue
