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
set $ins_init_events = 0
set $backend_start_events = 0
set $backend_loop_events = 0
set $register_gyro_events = 0
set $register_accel_events = 0
set $gyro_init_events = 0
set $gyro_init_iter_events = 0
set $gyro_init_collect_events = 0
set $gyro_init_eval_events = 0
set $gyro_init_converge_events = 0
set $gyro_init_finish_events = 0
set $inv_start_events = 0
set $inv_init_events = 0
set $inv_hw_init_events = 0
set $inv_start_checkpoint_events = 0
set $inv_start_sem_release_events = 0
set $bus_callback_events = 0
set $bus_delay_events = 0
set $post_inv_periodic = 0
set $post_delay_events = 0
set $post_bus_register_events = 0
set $post_spi_exchange_events = 0
set $post_spi_transfer_events = 0
set $post_sem_give_events = 0
set $post_event_wait_events = 0
set $post_evt_signal_i_events = 0
set $post_evt_wait_timeout_wakeup_events = 0
set $post_sch_ready_events = 0
set $post_sch_sleep_timeout_events = 0
set $post_sleep_events = 0
set $no_new_pc_events = 0
set $timer_irq_events = 0
set $timer_counter_events = 0
set $tim5_start_alarm_events = 0
set $tim5_stop_alarm_events = 0
set $tim5_set_alarm_events = 0
set $tim5_irq_ack_events = 0
set $tim5_spurious_irq_events = 0
set $vt_insert_first_events = 0
set $vt_set_alarm_events = 0
set $vt_callback_events = 0
set $vector108_entry_events = 0
set $vector108_after_irq_events = 0
set $vector108_pop_events = 0
set $port_irq_entry_events = 0
set $port_irq_decision_events = 0
set $port_irq_return_events = 0
set $svc_return_events = 0
set $vtick_events = 0
set $vtick_loop_events = 0
set $preempt_events = 0
set $diagnostic_stop_events = 0
set $post_shared_dma_lock_events = 0
set $post_shared_dma_lock_stream_events = 0
set $post_shared_dma_unlock_events = 0
set $post_uart_wait_events = 0
set $post_uart_rxbuff_events = 0
set $post_uart_signal_site_events = 0
set $post_dma2_stream1_irq_events = 0
set $post_withsem_release_events = 0
set $post_withsem_return_events = 0
set $inv2_start_events = 0
set $inv2_poll_events = 0
set $inv2_fifo_events = 0
set $inv2_fifo_count_events = 0
set $inv2_accum_events = 0
set $runtime_stop_at = 200

printf "\n[diag] FastDyn idle-starvation diagnostic script loaded\n"
printf "[diag] SPID1 = %p\n", $spid1
printf "[diag] SPID2 = %p\n", $spid2
printf "[diag] SPID4 = %p\n", $spid4

python
import gdb

POST_BREAKPOINTS_INSTALLED = False
WITHSEM_BREAKPOINTS_INSTALLED = False
VT_FUNC_COUNTS = {}
VT_FUNC_SYMBOLS = {}
THREAD_NAME_BY_ADDR = {
    0x20019DDC: "main",
    0x20019D60: "idle",
    0x2002C488: "UART_RX",
    0x2002B980: "OTG1",
    0x200162B0: "monitor",
    0x20018460: "timer",
    0x20017C70: "rcout",
    0x20017880: "rcin",
    0x20017290: "io",
    0x200168A0: "storage",
    0x20029DE8: "UART2",
    0x200295C8: "UART3",
    0x20028D68: "UART4",
    0x20028578: "UART8",
    0x200272F0: "UART6",
    0x2001D180: "IOMCU",
    0x20021E18: "log_io",
    0x200225F0: "SPI4",
    0x20022CE0: "SPI1",
}
WATCH_THREAD_ADDRS = {0x20019DDC, 0x200162B0, 0x2001D180}
INTERESTING_EVENT_MASK = 0x00010400


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


def _read_u32(addr):
    try:
        return int(gdb.parse_and_eval("*(unsigned int*)0x%08x" % addr)) & 0xFFFFFFFF
    except Exception:
        return None


def _read_u8(addr):
    try:
        mem = gdb.selected_inferior().read_memory(addr, 1)
        return bytes(mem)[0]
    except Exception:
        return None


def _fmt_u32(value):
    if value is None:
        return "<unavailable>"
    return "0x%08x" % (value & 0xFFFFFFFF)


def _fmt_u32_delta(target, current):
    if target is None or current is None:
        return "<unavailable>"
    return "%u" % ((target - current) & 0xFFFFFFFF)


def _thumb_addr(addr):
    if addr is None:
        return None
    return addr & ~1


def _symbol_at(addr):
    addr_i = _thumb_addr(addr)
    if addr_i is None:
        return "<unavailable>"
    try:
        return gdb.execute("info symbol 0x%x" % addr_i, to_string=True).strip()
    except Exception as exc:
        return "<symbol-failed:%s>" % exc


def _read_vtimer(vtp):
    vtp_i = _safe_int(vtp)
    if vtp_i is None or vtp_i == 0:
        return {
            "addr": vtp_i,
            "next": None,
            "prev": None,
            "delta": None,
            "func": None,
            "par": None,
            "reload": None,
        }
    return {
        "addr": vtp_i,
        "next": _read_u32(vtp_i + 0x00),
        "prev": _read_u32(vtp_i + 0x04),
        "delta": _read_u32(vtp_i + 0x08),
        "func": _read_u32(vtp_i + 0x0C),
        "par": _read_u32(vtp_i + 0x10),
        "reload": _read_u32(vtp_i + 0x14),
    }


def _record_vt_callback(func):
    func_i = _thumb_addr(_safe_int(func))
    if func_i is None:
        func_i = 0
    VT_FUNC_COUNTS[func_i] = VT_FUNC_COUNTS.get(func_i, 0) + 1
    if func_i not in VT_FUNC_SYMBOLS:
        VT_FUNC_SYMBOLS[func_i] = _symbol_at(func_i)
    return VT_FUNC_COUNTS[func_i], VT_FUNC_SYMBOLS[func_i]


def _print_vt_callback_summary(limit=10):
    if not VT_FUNC_COUNTS:
        print("[vt-callback-summary] no virtual timer callbacks recorded")
        return
    print("[vt-callback-summary] top callback functions:")
    for func, count in sorted(VT_FUNC_COUNTS.items(), key=lambda kv: kv[1], reverse=True)[:limit]:
        print(
            "[vt-callback-summary] count=%u func=0x%08x symbol=%s"
            % (count, func, VT_FUNC_SYMBOLS.get(func, "<unknown>"))
        )


def _dump_dma2_stream(label, stream_id):
    base = 0x40026400 + 0x10 + (stream_id * 0x18)
    lisr = _read_u32(0x40026400)
    hisr = _read_u32(0x40026404)
    cr = _read_u32(base + 0x00)
    ndtr = _read_u32(base + 0x04)
    par = _read_u32(base + 0x08)
    m0ar = _read_u32(base + 0x0c)
    fcr = _read_u32(base + 0x14)
    print(
        "[dma2] %s stream=%u CR=%s NDTR=%s PAR=%s M0AR=%s FCR=%s LISR=%s HISR=%s"
        % (
            label,
            stream_id,
            _fmt_u32(cr),
            _fmt_u32(ndtr),
            _fmt_u32(par),
            _fmt_u32(m0ar),
            _fmt_u32(fcr),
            _fmt_u32(lisr),
            _fmt_u32(hisr),
        )
    )


def _dump_usart6_state(label):
    base = 0x40011400
    # Do not read DR here; on real USARTs that can consume pending RX data.
    fields = [
        ("SR", 0x00),
        ("BRR", 0x08),
        ("CR1", 0x0C),
        ("CR2", 0x10),
        ("CR3", 0x14),
        ("GTPR", 0x18),
    ]
    values = []
    for name, off in fields:
        values.append("%s=%s" % (name, _fmt_u32(_read_u32(base + off))))
    print("[usart6] %s %s DR=<skipped>" % (label, " ".join(values)))


def _bytebuffer_state(expr):
    return (
        "buf=%s size=%s head=%s tail=%s"
        % (
            _safe_str(_safe_eval("%s.buf" % expr)),
            _safe_str(_safe_eval("%s.size" % expr)),
            _safe_str(_safe_eval("%s.head" % expr)),
            _safe_str(_safe_eval("%s.tail" % expr)),
        )
    )


def _dump_uart_driver_state(label, uart_addr):
    uart_i = _safe_int(uart_addr)
    if uart_i is None or uart_i == 0:
        print("[uart] %s uart=<unavailable>" % label)
        return
    expr = "((ChibiOS::UARTDriver*)0x%x)" % uart_i
    bounce_idx = _safe_int(_safe_eval("%s->rx_bounce_idx" % expr))
    bounce_ptr = None
    if bounce_idx is not None:
        bounce_ptr = _safe_eval("%s->rx_bounce_buf[%u]" % (expr, bounce_idx & 1))
    ndtr = _safe_int(_safe_eval("%s->rxdma->stream->NDTR" % expr))
    rx_len = None
    if ndtr is not None:
        rx_len = max(0, 64 - ndtr)
    print(
        "[uart] %s driver=0x%08x serial_num=%s baud=%s rx_dma=%s tx_dma=%s wait_thread=%s wait_n=%s rx_stats=%s tx_stats=%s rx_bounce_idx=%s rx_ndtr=%s rx_len=%s"
        % (
            label,
            uart_i & 0xFFFFFFFF,
            _safe_str(_safe_eval("%s->serial_num" % expr)),
            _safe_str(_safe_eval("%s->_baudrate" % expr)),
            _safe_str(_safe_eval("%s->rx_dma_enabled" % expr)),
            _safe_str(_safe_eval("%s->tx_dma_enabled" % expr)),
            _thread_summary(_safe_int(_safe_eval("%s->_wait.thread_ctx" % expr))),
            _safe_str(_safe_eval("%s->_wait.n" % expr)),
            _safe_str(_safe_eval("%s->_rx_stats_bytes" % expr)),
            _safe_str(_safe_eval("%s->_tx_stats_bytes" % expr)),
            bounce_idx if bounce_idx is not None else "<unavailable>",
            ndtr if ndtr is not None else "<unavailable>",
            rx_len if rx_len is not None else "<unavailable>",
        )
    )
    print("[uart] %s readbuf %s writebuf %s" % (label, _bytebuffer_state("%s->_readbuf" % expr), _bytebuffer_state("%s->_writebuf" % expr)))
    print(
        "[uart] %s sdef serial=%s instance=%s rx_stream=%s rx_chan=%s tx_stream=%s tx_chan=%s rxdma=%s txdma=%s dma_handle=%s"
        % (
            label,
            _safe_str(_safe_eval("%s->sdef.serial" % expr)),
            _safe_str(_safe_eval("%s->sdef.instance" % expr)),
            _safe_str(_safe_eval("%s->sdef.dma_rx_stream_id" % expr)),
            _safe_str(_safe_eval("%s->sdef.dma_rx_channel_id" % expr)),
            _safe_str(_safe_eval("%s->sdef.dma_tx_stream_id" % expr)),
            _safe_str(_safe_eval("%s->sdef.dma_tx_channel_id" % expr)),
            _safe_str(_safe_eval("%s->rxdma" % expr)),
            _safe_str(_safe_eval("%s->txdma" % expr)),
            _safe_str(_safe_eval("%s->dma_handle" % expr)),
        )
    )
    if bounce_ptr is not None:
        print("[uart] %s rx_bounce[%s]=%s bytes=%s" % (label, bounce_idx, _safe_str(bounce_ptr), _read_bytes(bounce_ptr, rx_len, 32)))
    _dump_usart6_state(label)
    _dump_dma2_stream("%s USART6-RX" % label, 1)
    _dump_dma2_stream("%s USART6-TX" % label, 7)


def _dump_spi_dma_pair(label, spip=None):
    spip_i = _safe_int(spip)
    spid1_i = _safe_int(_safe_eval("&SPID1"))
    spid4_i = _safe_int(_safe_eval("&SPID4"))
    if spip_i == spid1_i:
        _dump_dma2_stream("%s SPID1 RX" % label, 2)
        _dump_dma2_stream("%s SPID1 TX" % label, 5)
    elif spip_i == spid4_i:
        _dump_dma2_stream("%s SPID4 RX" % label, 3)
        _dump_dma2_stream("%s SPID4 TX" % label, 4)


def _dump_tim5_state(label):
    # CubeBlack hwdef uses STM32_ST_USE_TIMER=5, so ChibiOS system time is TIM5.
    base = 0x40000C00
    fields = [
        ("CR1", 0x00),
        ("DIER", 0x0C),
        ("SR", 0x10),
        ("EGR", 0x14),
        ("CCMR1", 0x18),
        ("CCER", 0x20),
        ("CNT", 0x24),
        ("PSC", 0x28),
        ("ARR", 0x2C),
        ("CCR1", 0x34),
        ("CCR2", 0x38),
        ("CCR3", 0x3C),
        ("CCR4", 0x40),
    ]
    values = []
    for name, off in fields:
        values.append("%s=%s" % (name, _fmt_u32(_read_u32(base + off))))
    cnt = _read_u32(base + 0x24)
    ccr1 = _read_u32(base + 0x34)
    sr = _read_u32(base + 0x10)
    dier = _read_u32(base + 0x0C)
    request = None
    if sr is not None and dier is not None:
        request = sr & dier & 0x3
    print(
        "[tim5] %s %s forward_to_ccr1=%s irq_request=%s"
        % (label, " ".join(values), _fmt_u32_delta(ccr1, cnt), _fmt_u32(request))
    )


def _dump_irq_state(label):
    irq = 50  # STM32 TIM5 global interrupt.
    word = irq // 32
    bit = irq % 32
    iser = _read_u32(0xE000E100 + word * 4)
    ispr = _read_u32(0xE000E200 + word * 4)
    iabr = _read_u32(0xE000E300 + word * 4)
    icsr = _read_u32(0xE000ED04)
    aircr = _read_u32(0xE000ED0C)
    print(
        "[irq] %s TIM5 irq=%u bit=%u ICSR=%s AIRCR=%s ISER=%s ISPR=%s IABR=%s enabled=%s pending=%s active=%s"
        % (
            label,
            irq,
            bit,
            _fmt_u32(icsr),
            _fmt_u32(aircr),
            _fmt_u32(iser),
            _fmt_u32(ispr),
            _fmt_u32(iabr),
            ((iser >> bit) & 1) if iser is not None else "<unavailable>",
            ((ispr >> bit) & 1) if ispr is not None else "<unavailable>",
            ((iabr >> bit) & 1) if iabr is not None else "<unavailable>",
        )
    )


def _exc_return_info(value):
    exc = _safe_int(value)
    if exc is None:
        return "exc_return=<unavailable>"
    exc &= 0xFFFFFFFF
    if (exc & 0xFFFFFF00) != 0xFFFFFF00:
        return "value=%s not_exc_return" % _fmt_u32(exc)
    return (
        "exc_return=%s mode=%s stack=%s frame=%s fpca=%u"
        % (
            _fmt_u32(exc),
            "thread" if (exc & 0x8) else "handler",
            "psp" if (exc & 0x4) else "msp",
            "basic" if (exc & 0x10) else "extended-fp",
            (exc >> 6) & 1,
        )
    )


def _is_exc_return(value):
    value_i = _safe_int(value)
    if value_i is None:
        return False
    return (value_i & 0xFFFFFF00) == 0xFFFFFF00


def _is_dumpable_addr(addr, count=1):
    addr_i = _safe_int(addr)
    if addr_i is None:
        return False
    end = addr_i + count * 4
    ranges = (
        (0x08000000, 0x08200000),  # firmware flash
        (0x10000000, 0x10010000),  # CCM SRAM on STM32F4
        (0x20000000, 0x20040000),  # SRAM window used by this target
    )
    return any(addr_i >= start and end <= stop for start, stop in ranges)


def _read_dumpable_u32(addr):
    if not _is_dumpable_addr(addr, 1):
        return None
    return _read_u32(addr)


def _read_cstr_addr(addr, max_len=48):
    addr_i = _safe_int(addr)
    if addr_i is None or addr_i == 0 or not _is_dumpable_addr(addr_i, 1):
        return None
    try:
        mem = bytes(gdb.selected_inferior().read_memory(addr_i, max_len))
        return mem.split(b"\x00", 1)[0].decode("ascii", errors="replace")
    except Exception:
        return None


def _dump_words(label, addr, count=12):
    addr_i = _safe_int(addr)
    if addr_i is None or addr_i == 0:
        print("[mem] %s addr=<unavailable>" % label)
        return
    if not _is_dumpable_addr(addr_i, count):
        print("[mem] %s base=0x%08x skipped-nonram-nonflash" % (label, addr_i & 0xFFFFFFFF))
        return
    parts = []
    for i in range(count):
        parts.append("+0x%02x=%s" % (i * 4, _fmt_u32(_read_u32(addr_i + i * 4))))
    print("[mem] %s base=0x%08x %s" % (label, addr_i & 0xFFFFFFFF, " ".join(parts)))


def _dump_exception_frame(label, exc_return=None):
    exc = _safe_int(exc_return)
    if exc is None:
        exc = _safe_int(_safe_eval("$lr"))
    exc = 0 if exc is None else (exc & 0xFFFFFFFF)
    msp = _safe_int(_safe_eval("$msp"))
    psp = _safe_int(_safe_eval("$psp"))
    sp = _safe_int(_safe_eval("$sp"))
    stack = psp if (exc & 0x4) else msp
    if stack is None:
        stack = sp
    print(
        "[exc] %s %s sp=%s msp=%s psp=%s pc=%s lr=%s xpsr=%s ipsr=%s control=%s basepri=%s"
        % (
            label,
            _exc_return_info(exc),
            _fmt_u32(sp),
            _fmt_u32(msp),
            _fmt_u32(psp),
            _safe_str(_safe_eval("$pc")),
            _safe_str(_safe_eval("$lr")),
            _safe_str(_safe_eval("$xpsr")),
            _safe_str(_safe_eval("$ipsr")),
            _safe_str(_safe_eval("$control")),
            _safe_str(_safe_eval("$basepri")),
        )
    )
    _dump_words("%s active-stack" % label, stack, 12)
    if exc != 0 and (exc & 0x10) == 0 and stack is not None:
        _dump_words("%s extended-core-frame" % label, stack + 0x48, 8)


def _dump_current_thread(label):
    current = _current_thread_ptr()
    print("[thread] %s current=%s pc=%s lr=%s" % (label, _thread_summary(current), _safe_str(_safe_eval("$pc")), _safe_str(_safe_eval("$lr"))))


def _dump_scheduler_state(label):
    _dump_current_thread(label)
    print(
        "[sched] %s rlist.current=%s rlist.pqueue.next=%s rlist.pqueue.prev=%s rlist.pqueue.prio=%s"
        % (
            label,
            _safe_str(_safe_eval("ch.rlist.current")),
            _safe_str(_safe_eval("ch.rlist.pqueue.next")),
            _safe_str(_safe_eval("ch.rlist.pqueue.prev")),
            _safe_str(_safe_eval("ch.rlist.pqueue.prio")),
        )
    )


def _no_control_progress_after_post():
    return (
        _post_inv_active()
        and _gdb_counter("scheduler_run_events") == 0
        and _gdb_counter("scheduler_loop_events") == 0
        and _gdb_counter("wait_sample_events") == 0
        and _gdb_counter("gyro_init_events") == 0
    )


def _diagnostic_stop(reason):
    _inc_counter("diagnostic_stop_events")
    print("\n[diagnostic-stop] %s" % reason)
    _dump_tim5_state("diagnostic-stop")
    _dump_irq_state("diagnostic-stop")
    print(
        "[diagnostic-stop] counters scheduler_loop=%u scheduler_run=%u wait_sample=%u gyro_init=%u timer_irq=%u counter_reads=%u vtick=%u vtick_loop=%u preempt=%u event_wait=%u sleep=%u"
        % (
            _gdb_counter("scheduler_loop_events"),
            _gdb_counter("scheduler_run_events"),
            _gdb_counter("wait_sample_events"),
            _gdb_counter("gyro_init_events"),
            _gdb_counter("timer_irq_events"),
            _gdb_counter("timer_counter_events"),
            _gdb_counter("vtick_events"),
            _gdb_counter("vtick_loop_events"),
            _gdb_counter("preempt_events"),
            _gdb_counter("post_event_wait_events"),
            _gdb_counter("post_sleep_events"),
        )
    )
    print(
        "[diagnostic-stop] timer-writers start_alarm=%u stop_alarm=%u set_alarm=%u irq_ack=%u spurious_irq_ack=%u vt_insert_first=%u vt_set_alarm=%u vt_callback=%u"
        % (
            _gdb_counter("tim5_start_alarm_events"),
            _gdb_counter("tim5_stop_alarm_events"),
            _gdb_counter("tim5_set_alarm_events"),
            _gdb_counter("tim5_irq_ack_events"),
            _gdb_counter("tim5_spurious_irq_events"),
            _gdb_counter("vt_insert_first_events"),
            _gdb_counter("vt_set_alarm_events"),
            _gdb_counter("vt_callback_events"),
        )
    )
    print(
        "[diagnostic-stop] exception-path svc_return=%u vector_entry=%u vector_after_irq=%u vector_pop=%u port_entry=%u port_decision=%u port_return=%u"
        % (
            _gdb_counter("svc_return_events"),
            _gdb_counter("vector108_entry_events"),
            _gdb_counter("vector108_after_irq_events"),
            _gdb_counter("vector108_pop_events"),
            _gdb_counter("port_irq_entry_events"),
            _gdb_counter("port_irq_decision_events"),
            _gdb_counter("port_irq_return_events"),
        )
    )
    print(
        "[diagnostic-stop] event-focus wait=%u signal_i=%u timeout_wakeup=%u ready=%u sleep_timeout=%u"
        % (
            _gdb_counter("post_event_wait_events"),
            _gdb_counter("post_evt_signal_i_events"),
            _gdb_counter("post_evt_wait_timeout_wakeup_events"),
            _gdb_counter("post_sch_ready_events"),
            _gdb_counter("post_sch_sleep_timeout_events"),
        )
    )
    print(
        "[diagnostic-stop] uart-focus wait=%u rxbuff=%u signal_site=%u dma2_stream1_irq=%u"
        % (
            _gdb_counter("post_uart_wait_events"),
            _gdb_counter("post_uart_rxbuff_events"),
            _gdb_counter("post_uart_signal_site_events"),
            _gdb_counter("post_dma2_stream1_irq_events"),
        )
    )
    _print_vt_callback_summary()


def _thread_name_from_addr(tp):
    tp_i = _safe_int(tp)
    if tp_i is None or tp_i == 0:
        return "<none>"
    mapped = THREAD_NAME_BY_ADDR.get(tp_i)
    name_ptr = _read_u32(tp_i + 0x1C)
    decoded = _read_cstr_addr(name_ptr)
    if decoded:
        mapped = decoded
    if mapped:
        return "%s@0x%08x" % (mapped, tp_i & 0xFFFFFFFF)
    return "thread@0x%08x" % (tp_i & 0xFFFFFFFF)


def _current_thread_ptr():
    current = _safe_int(_safe_eval("ch.rlist.current"))
    if current is not None and current != 0:
        return current
    return _read_u32(0x20019DA0 + 0x0C)


def _thread_is_watched(tp):
    tp_i = _safe_int(tp)
    return tp_i in WATCH_THREAD_ADDRS


def _event_is_interesting(events):
    events_i = _safe_int(events)
    return events_i is not None and (events_i & INTERESTING_EVENT_MASK) != 0


def _thread_state_fields(tp):
    tp_i = _safe_int(tp)
    if tp_i is None or tp_i == 0:
        return {
            "state": None,
            "prio": None,
            "realprio": None,
            "wait": None,
            "epending": None,
        }
    return {
        "state": _read_u8(tp_i + 0x24),
        "prio": _read_u32(tp_i + 0x08),
        "realprio": _read_u32(tp_i + 0x38),
        "wait": _read_u32(tp_i + 0x28),
        "epending": _read_u32(tp_i + 0x30),
    }


def _thread_summary(tp):
    tp_i = _safe_int(tp)
    fields = _thread_state_fields(tp_i)
    return (
        "%s state=%s prio=%s realprio=%s wait/rdy=%s epending=%s"
        % (
            _thread_name_from_addr(tp_i),
            fields["state"] if fields["state"] is not None else "<unavailable>",
            fields["prio"] if fields["prio"] is not None else "<unavailable>",
            fields["realprio"] if fields["realprio"] is not None else "<unavailable>",
            _fmt_u32(fields["wait"]),
            _fmt_u32(fields["epending"]),
        )
    )


def _signal_will_ready(tp, events):
    fields = _thread_state_fields(tp)
    events_i = _safe_int(events)
    if events_i is None:
        return "<unavailable>"
    state = fields["state"]
    wait_mask = fields["wait"] or 0
    pending = fields["epending"] or 0
    new_pending = pending | events_i
    if state == 10:
        return "yes-OR" if (new_pending & wait_mask) != 0 else "no-OR"
    if state == 11:
        return "yes-AND" if (new_pending & wait_mask) == wait_mask else "no-AND"
    return "no-state-%s" % (state if state is not None else "<unavailable>")


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


def _stack_contains(pattern, max_depth=10):
    frame = gdb.selected_frame()
    depth = 0
    while frame is not None and depth < max_depth:
        try:
            block = frame.block()
            if block and block.function and pattern in block.function.print_name:
                return True
        except Exception:
            pass
        frame = frame.older()
        depth += 1
    return False


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


def _fmt_vec(expr):
    x = _safe_eval("(%s).x" % expr)
    y = _safe_eval("(%s).y" % expr)
    z = _safe_eval("(%s).z" % expr)
    if x is None or y is None or z is None:
        return _safe_str(_safe_eval(expr))
    return "(%s,%s,%s)" % (_safe_str(x), _safe_str(y), _safe_str(z))


def _fmt_array(expr, n=3):
    parts = []
    for i in range(n):
        parts.append("%u:%s" % (i, _safe_str(_safe_eval("%s[%u]" % (expr, i)))))
    return "[" + " ".join(parts) + "]"


def _fmt_vec_array(expr, n=3):
    parts = []
    for i in range(n):
        parts.append("%u:%s" % (i, _fmt_vec("%s[%u]" % (expr, i))))
    return "[" + " ".join(parts) + "]"


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
    print(
        "[summary] ins_init=%u gyro_init=%u gyro_iter=%u gyro_collect=%u gyro_eval=%u gyro_converge=%u gyro_finish=%u"
        % (
            _gdb_counter("ins_init_events"),
            _gdb_counter("gyro_init_events"),
            _gdb_counter("gyro_init_iter_events"),
            _gdb_counter("gyro_init_collect_events"),
            _gdb_counter("gyro_init_eval_events"),
            _gdb_counter("gyro_init_converge_events"),
            _gdb_counter("gyro_init_finish_events"),
        )
    )
    print(
        "[summary] backend_start=%u backend_loop=%u register_gyro=%u register_accel=%u inv_start=%u inv_init=%u inv_hw_init=%u inv_start_checkpoint=%u inv_start_sem_release=%u bus_callback=%u bus_delay=%u inv2_start=%u inv2_poll=%u inv2_fifo=%u inv2_fifo_count=%u inv2_accum=%u"
        % (
            _gdb_counter("backend_start_events"),
            _gdb_counter("backend_loop_events"),
            _gdb_counter("register_gyro_events"),
            _gdb_counter("register_accel_events"),
            _gdb_counter("inv_start_events"),
            _gdb_counter("inv_init_events"),
            _gdb_counter("inv_hw_init_events"),
            _gdb_counter("inv_start_checkpoint_events"),
            _gdb_counter("inv_start_sem_release_events"),
            _gdb_counter("bus_callback_events"),
            _gdb_counter("bus_delay_events"),
            _gdb_counter("inv2_start_events"),
            _gdb_counter("inv2_poll_events"),
            _gdb_counter("inv2_fifo_events"),
            _gdb_counter("inv2_fifo_count_events"),
            _gdb_counter("inv2_accum_events"),
        )
    )
    print(
        "[summary] post_callback active=%u register_periodic=%u delay=%u bus_callback=%u bus_delay=%u spi_transfer=%u spi_exchange=%u sem_give=%u event_wait=%u evt_signal_i=%u vt_wakeup=%u ready=%u sleep_timeout=%u"
        % (
            _gdb_counter("post_inv_periodic"),
            _gdb_counter("post_bus_register_events"),
            _gdb_counter("post_delay_events"),
            _gdb_counter("bus_callback_events"),
            _gdb_counter("bus_delay_events"),
            _gdb_counter("post_spi_transfer_events"),
            _gdb_counter("post_spi_exchange_events"),
            _gdb_counter("post_sem_give_events"),
            _gdb_counter("post_event_wait_events"),
            _gdb_counter("post_evt_signal_i_events"),
            _gdb_counter("post_evt_wait_timeout_wakeup_events"),
            _gdb_counter("post_sch_ready_events"),
            _gdb_counter("post_sch_sleep_timeout_events"),
        )
    )
    print(
        "[summary] uart_focus wait=%u rxbuff=%u signal_site=%u dma2_stream1_irq=%u"
        % (
            _gdb_counter("post_uart_wait_events"),
            _gdb_counter("post_uart_rxbuff_events"),
            _gdb_counter("post_uart_signal_site_events"),
            _gdb_counter("post_dma2_stream1_irq_events"),
        )
    )
    print(
        "[summary] timer irq=%u counter_reads=%u vtick=%u vtick_loop=%u preempt=%u sleep=%u diagnostic_stop=%u"
        % (
            _gdb_counter("timer_irq_events"),
            _gdb_counter("timer_counter_events"),
            _gdb_counter("vtick_events"),
            _gdb_counter("vtick_loop_events"),
            _gdb_counter("preempt_events"),
            _gdb_counter("post_sleep_events"),
            _gdb_counter("diagnostic_stop_events"),
        )
    )
    print(
        "[summary] exception_path svc_return=%u vector_entry=%u vector_after_irq=%u vector_pop=%u port_entry=%u port_decision=%u port_return=%u"
        % (
            _gdb_counter("svc_return_events"),
            _gdb_counter("vector108_entry_events"),
            _gdb_counter("vector108_after_irq_events"),
            _gdb_counter("vector108_pop_events"),
            _gdb_counter("port_irq_entry_events"),
            _gdb_counter("port_irq_decision_events"),
            _gdb_counter("port_irq_return_events"),
        )
    )
    _bt(12)


def _post_inv_active():
    return (_safe_int(_safe_eval("$post_inv_periodic")) or 0) != 0


def _selected_thread_name():
    return _thread_name_from_addr(_current_thread_ptr())


def _install_post_callback_breakpoints():
    global POST_BREAKPOINTS_INSTALLED
    if POST_BREAKPOINTS_INSTALLED:
        return
    POST_BREAKPOINTS_INSTALLED = True
    for bp_cls in (
        PostBusThreadCallbackBreakpoint,
        PostBusThreadDelayBreakpoint,
        PostDelayMicrosecondsBreakpoint,
        PostUARTWaitTimeoutBreakpoint,
        PostDMA2Stream1IRQBreakpoint,
        PostUARTRxBuffFullBreakpoint,
        PostUARTRxSignalSiteBreakpoint,
        PostEventWaitBreakpoint,
        PostNoNewCoveragePCBreakpoint,
        PostSharedDMALockBreakpoint,
        PostSharedDMALockStreamBreakpoint,
        PostSharedDMAUnlockBreakpoint,
        PostSleepSnapshotBreakpoint,
        PostSchWakeupBreakpoint,
        PostSchReadyIBreakpoint,
        PostSchSleepTimeoutBreakpoint,
    ):
        try:
            bp = bp_cls()
            print("[post-callback] installed breakpoint: %s" % bp.location)
        except Exception as exc:
            print("[post-callback] could not install %s: %s" % (bp_cls.__name__, exc))


def _install_withsem_breakpoints():
    global WITHSEM_BREAKPOINTS_INSTALLED
    if WITHSEM_BREAKPOINTS_INSTALLED:
        return
    WITHSEM_BREAKPOINTS_INSTALLED = True
    for bp_cls in (InvensenseStartSemaphoreReleaseBreakpoint, WithSemaphoreReturnPCBreakpoint, WithSemaphorePopReturnPCBreakpoint):
        try:
            bp = bp_cls()
            print("[withsem] installed breakpoint: %s" % bp.location)
        except Exception as exc:
            print("[withsem] could not install %s: %s" % (bp_cls.__name__, exc))


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


class INSInitBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(INSInitBreakpoint, self).__init__("AP_InertialSensor::init", internal=False)

    def stop(self):
        count = _inc_counter("ins_init_events")
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        loop_rate = _safe_int(_safe_frame_var(frame, "loop_rate"))
        print("\n[gyro-cal] AP_InertialSensor::init enter hit=%u loop_rate=%s this=%s" % (count, loop_rate if loop_rate is not None else "<unavailable>", _safe_str(this)))
        _dump_ins_state("gyro-cal-init-enter", this_addr)

        def on_return(_ret):
            print("[gyro-cal] AP_InertialSensor::init returned")
            _dump_ins_state("gyro-cal-init-return", this_addr)

        InspectReturn("AP_InertialSensor::init", on_return)
        return False


class BackendStartBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(BackendStartBreakpoint, self).__init__("AP_InertialSensor::_start_backends", internal=False)

    def stop(self):
        count = _inc_counter("backend_start_events")
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        print("\n[backend-start] AP_InertialSensor::_start_backends enter hit=%u this=%s" % (count, _safe_str(this)))
        _dump_ins_state("backend-start-enter", this_addr)

        def on_return(_ret):
            print("[backend-start] AP_InertialSensor::_start_backends returned")
            _dump_ins_state("backend-start-return", this_addr)

        InspectReturn("AP_InertialSensor::_start_backends", on_return)
        return False


class BackendLoopBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(BackendLoopBreakpoint, self).__init__("libraries/AP_InertialSensor/AP_InertialSensor.cpp:855", internal=False)

    def stop(self):
        count = _inc_counter("backend_loop_events")
        frame = gdb.selected_frame()
        i = _safe_int(_safe_frame_var(frame, "i"))
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        backend = _safe_eval("_backends[%u]" % i) if i is not None else None
        print(
            "[backend-start] about to start backend hit=%u i=%s backend=%s gyro_count=%s accel_count=%s"
            % (
                count,
                i if i is not None else "<unavailable>",
                _safe_str(backend),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_count")),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_accel_count")),
            )
        )
        return False


class RegisterSensorBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label, counter_name):
        super(RegisterSensorBreakpoint, self).__init__(symbol, internal=False)
        self.label = label
        self.counter_name = counter_name

    def stop(self):
        count = _inc_counter(self.counter_name)
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        instance_ref = _safe_frame_var(frame, "instance")
        rate = _safe_int(_safe_frame_var(frame, "raw_sample_rate_hz"))
        devid = _safe_frame_var(frame, "id")
        print(
            "[backend-start] %s enter hit=%u instance_ref=%s rate=%s id=%s before gyro_count=%s accel_count=%s"
            % (
                self.label,
                count,
                _safe_str(instance_ref),
                rate if rate is not None else "<unavailable>",
                _safe_str(devid),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_count")),
                _safe_str(_eval_at("AP_InertialSensor", this_addr, "_accel_count")),
            )
        )

        def on_return(ret):
            print(
                "[backend-start] %s returned ret=%s instance_ref=%s after gyro_count=%s accel_count=%s"
                % (
                    self.label,
                    _safe_str(ret),
                    _safe_str(instance_ref),
                    _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_count")),
                    _safe_str(_eval_at("AP_InertialSensor", this_addr, "_accel_count")),
                )
            )

        InspectReturn(self.label, on_return)
        return False


class InvensenseStartBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseStartBreakpoint, self).__init__("AP_InertialSensor_Invensense::start", internal=False)

    def stop(self):
        count = _inc_counter("inv_start_events")
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        print(
            "\n[backend-start] Invensense::start enter hit=%u this=%s type=%s gyro_instance=%s accel_instance=%s"
            % (
                count,
                _safe_str(this),
                _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_mpu_type")),
                _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "gyro_instance")),
                _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "accel_instance")),
            )
        )

        def on_return(_ret):
            print(
                "[backend-start] Invensense::start returned this=0x%x type=%s gyro_instance=%s accel_instance=%s"
                % (
                    this_addr or 0,
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_mpu_type")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "gyro_instance")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "accel_instance")),
                )
            )

        InspectReturn("AP_InertialSensor_Invensense::start", on_return)
        return False


def _invensense_this_addr():
    this = _safe_frame_var(gdb.selected_frame(), "this")
    this_addr = _safe_int(this)
    if this_addr is None:
        this_addr = _safe_int(_safe_eval("$r4"))
    return this_addr


def _print_invensense_state(prefix, this_addr):
    print(
        "%s this=0x%x type=%s gyro_instance=%s accel_instance=%s fast_sampling=%s offset_check=%s fifo_buffer=%s gyro_rate=%s accel_rate=%s raw_temp=%s"
        % (
            prefix,
            this_addr or 0,
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_mpu_type")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "gyro_instance")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "accel_instance")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_fast_sampling")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_enable_offset_checking")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_fifo_buffer")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_gyro_backend_rate_hz")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_accel_backend_rate_hz")),
            _safe_str(_eval_at("AP_InertialSensor_Invensense", this_addr, "_raw_temp")),
        )
    )


class InvensenseInitBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseInitBreakpoint, self).__init__("AP_InertialSensor_Invensense::_init", internal=False)

    def stop(self):
        count = _inc_counter("inv_init_events")
        this_addr = _invensense_this_addr()
        _print_invensense_state("\n[backend-init] Invensense::_init enter hit=%u" % count, this_addr)

        def on_return(ret):
            print("[backend-init] Invensense::_init returned ret=%s" % _safe_str(ret))
            _print_invensense_state("[backend-init] Invensense::_init return", this_addr)

        InspectReturn("AP_InertialSensor_Invensense::_init", on_return)
        return False


class InvensenseHardwareInitBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseHardwareInitBreakpoint, self).__init__("AP_InertialSensor_Invensense::_hardware_init", internal=False)

    def stop(self):
        count = _inc_counter("inv_hw_init_events")
        this_addr = _invensense_this_addr()
        _print_invensense_state("\n[backend-init] Invensense::_hardware_init enter hit=%u" % count, this_addr)

        def on_return(ret):
            print("[backend-init] Invensense::_hardware_init returned ret=%s" % _safe_str(ret))
            _print_invensense_state("[backend-init] Invensense::_hardware_init return", this_addr)

        InspectReturn("AP_InertialSensor_Invensense::_hardware_init", on_return)
        return False


class InvensenseStartCheckpointBreakpoint(gdb.Breakpoint):
    def __init__(self, location, label, first=12, every=0):
        super(InvensenseStartCheckpointBreakpoint, self).__init__(location, internal=False)
        self.label = label
        self.first = first
        self.every = every

    def stop(self):
        count = _inc_counter("inv_start_checkpoint_events")
        always_print = self.label in ("after_periodic_callback", "before_withsem_destructor", "before_return")
        if not always_print and not _maybe_print(count, first=self.first, every=self.every):
            return False
        this_addr = _invensense_this_addr()
        pc = _safe_str(_safe_eval("$pc"))
        r0 = _safe_str(_safe_eval("$r0"))
        r4 = _safe_str(_safe_eval("$r4"))
        lr = _safe_str(_safe_eval("$lr"))
        print("\n[backend-start] Invensense::start checkpoint hit=%u label=%s pc=%s r0=%s r4=%s lr=%s" % (count, self.label, pc, r0, r4, lr))
        _print_invensense_state("[backend-start] checkpoint-state", this_addr)
        if self.label == "after_periodic_callback":
            gdb.execute("set $post_inv_periodic = 1", to_string=True)
            print("[post-callback] activated focused scheduler/SPI diagnostics")
            _install_post_callback_breakpoints()
            _bt(8)
        elif self.label == "before_withsem_destructor":
            print("[withsem] activating focused WithSemaphore release diagnostics")
            _install_withsem_breakpoints()
            _bt(8)
        elif self.label == "before_return":
            print("[post-callback] Invensense::start reached return path; starvation hypothesis is wrong or fixed")
            _bt(8)
        return False


class InvensenseStartSemaphoreReleaseBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseStartSemaphoreReleaseBreakpoint, self).__init__("WithSemaphore::~WithSemaphore", internal=False)

    def stop(self):
        if not _stack_contains("AP_InertialSensor_Invensense::start", 8):
            return False
        count = _inc_counter("inv_start_sem_release_events")
        _inc_counter("post_withsem_release_events")
        if count > 6:
            self.enabled = False
            print("[withsem] disabling WithSemaphore::~WithSemaphore after %u matching hits" % count)
            return False
        mtx = _safe_eval("this->_mtx")
        print("\n[backend-start] Invensense::start semaphore release enter hit=%u mtx=%s pc=%s" % (count, _safe_str(mtx), _safe_str(_safe_eval("$pc"))))
        if count <= 2:
            _bt(8)

        def on_return(ret):
            print("[backend-start] Invensense::start semaphore release returned ret=%s pc=%s" % (_safe_str(ret), _safe_str(_safe_eval("$pc"))))

        InspectReturn("WithSemaphore::~WithSemaphore from Invensense::start", on_return)
        return False


class WithSemaphoreReturnPCBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(WithSemaphoreReturnPCBreakpoint, self).__init__("*0x0803fbfc", internal=False)

    def stop(self):
        if not _stack_contains("AP_InertialSensor_Invensense::start", 10):
            return False
        count = _inc_counter("post_withsem_return_events")
        print(
            "[withsem] destructor post-give PC hit=%u pc=%s r0=%s lr=%s thread=%s"
            % (
                count,
                _safe_str(_safe_eval("$pc")),
                _safe_str(_safe_eval("$r0")),
                _safe_str(_safe_eval("$lr")),
                _selected_thread_name(),
            )
        )
        if count <= 2:
            _bt(8)
        if count > 6:
            self.enabled = False
        return False


class WithSemaphorePopReturnPCBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(WithSemaphorePopReturnPCBreakpoint, self).__init__("*0x0803fbfe", internal=False)

    def stop(self):
        if not _stack_contains("AP_InertialSensor_Invensense::start", 10):
            return False
        count = _inc_counter("post_withsem_return_events")
        print(
            "[withsem] destructor pop-return PC hit=%u pc=%s r0=%s lr=%s thread=%s"
            % (
                count,
                _safe_str(_safe_eval("$pc")),
                _safe_str(_safe_eval("$r0")),
                _safe_str(_safe_eval("$lr")),
                _selected_thread_name(),
            )
        )
        if count <= 2:
            _bt(8)
        if count > 6:
            self.enabled = False
        return False


class BusThreadCallbackBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(BusThreadCallbackBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_HAL_ChibiOS/Device.cpp:65",
            internal=False,
        )

    def stop(self):
        count = _inc_counter("bus_callback_events")
        if not _maybe_print(count, first=16, every=1000):
            return False
        frame = gdb.selected_frame()
        print(
            "[bus-thread] callback-due hit=%u binfo=%s callback=%s now=%s next=%s period=%s"
            % (
                count,
                _safe_str(_safe_frame_var(frame, "binfo")),
                _safe_str(_safe_frame_var(frame, "callback")),
                _safe_str(_safe_frame_var(frame, "now")),
                _safe_str(_safe_eval("callback->next_usec")),
                _safe_str(_safe_eval("callback->period_usec")),
            )
        )
        return False


class BusThreadDelayBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(BusThreadDelayBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_HAL_ChibiOS/Device.cpp:99",
            internal=False,
        )

    def stop(self):
        count = _inc_counter("bus_delay_events")
        if not _maybe_print(count, first=16, every=1000):
            return False
        frame = gdb.selected_frame()
        print(
            "[bus-thread] delay hit=%u binfo=%s now=%s next_needed=%s delay=%s"
            % (
                count,
                _safe_str(_safe_frame_var(frame, "binfo")),
                _safe_str(_safe_frame_var(frame, "now")),
                _safe_str(_safe_frame_var(frame, "next_needed")),
                _safe_str(_safe_frame_var(frame, "delay")),
            )
        )
        return False


class PeriodicRegisterBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PeriodicRegisterBreakpoint, self).__init__("ChibiOS::DeviceBus::register_periodic_callback", internal=False)

    def stop(self):
        count = _inc_counter("post_bus_register_events")
        frame = gdb.selected_frame()
        period = _safe_int(_safe_frame_var(frame, "period_usec"))
        hal_device = _safe_frame_var(frame, "_hal_device")
        this = _safe_frame_var(frame, "this")
        cb = _safe_frame_var(frame, "cb")
        if count <= 12:
            print(
                "\n[periodic-register] hit=%u this=%s period_usec=%s cb=%s hal_device=%s thread=%s"
                % (
                    count,
                    _safe_str(this),
                    period if period is not None else "<unavailable>",
                    _safe_str(cb),
                    _safe_str(hal_device),
                    _selected_thread_name(),
                )
            )
            _bt(8)
        return False


class PostDelayMicrosecondsBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostDelayMicrosecondsBreakpoint, self).__init__("ChibiOS::Scheduler::delay_microseconds", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("post_delay_events")
        if count > 80:
            self.enabled = False
            print("[post-delay] disabling after %u hits" % count)
            return False
        frame = gdb.selected_frame()
        usec = _safe_int(_safe_frame_var(frame, "usec"))
        context = "other"
        if _stack_contains("DeviceBus::bus_thread", 8):
            context = "DeviceBus::bus_thread"
        elif _stack_contains("UARTDriver::uart_thread", 8):
            context = "UARTDriver::uart_thread"
        elif _stack_contains("AP_InertialSensor_Invensense", 8):
            context = "Invensense"
        if _maybe_print(count, first=32, every=20):
            print(
                "[post-delay] enter hit=%u usec=%s context=%s thread=%s"
                % (count, usec if usec is not None else "<unavailable>", context, _selected_thread_name())
            )
            if count <= 6:
                _bt(7)

            def on_return(_ret):
                print("[post-delay] return hit=%u usec=%s context=%s thread=%s" % (count, usec if usec is not None else "<unavailable>", context, _selected_thread_name()))

            InspectReturn("ChibiOS::Scheduler::delay_microseconds post-callback", on_return)
        return False


class PostBusThreadCallbackBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostBusThreadCallbackBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_HAL_ChibiOS/Device.cpp:65",
            internal=False,
        )

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("bus_callback_events")
        if count > 80:
            self.enabled = False
            print("[post-bus] disabling callback-due breakpoint after %u hits" % count)
            return False
        if _maybe_print(count, first=32, every=20):
            frame = gdb.selected_frame()
            print(
                "[post-bus] callback-due hit=%u thread=%s binfo=%s callback=%s now=%s next=%s period=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _safe_str(_safe_frame_var(frame, "binfo")),
                    _safe_str(_safe_frame_var(frame, "callback")),
                    _safe_str(_safe_frame_var(frame, "now")),
                    _safe_str(_safe_eval("callback->next_usec")),
                    _safe_str(_safe_eval("callback->period_usec")),
                )
            )
        return False


class PostBusThreadDelayBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostBusThreadDelayBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_HAL_ChibiOS/Device.cpp:99",
            internal=False,
        )

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("bus_delay_events")
        if count > 80:
            self.enabled = False
            print("[post-bus] disabling delay breakpoint after %u hits" % count)
            return False
        if _maybe_print(count, first=32, every=20):
            frame = gdb.selected_frame()
            now = _safe_int(_safe_frame_var(frame, "now"))
            next_needed = _safe_int(_safe_frame_var(frame, "next_needed"))
            delay = _safe_int(_safe_frame_var(frame, "delay"))
            delta = None
            if now is not None and next_needed is not None:
                delta = next_needed - now
            print(
                "[post-bus] delay hit=%u thread=%s binfo=%s now=%s next_needed=%s delta=%s delay=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _safe_str(_safe_frame_var(frame, "binfo")),
                    now if now is not None else "<unavailable>",
                    next_needed if next_needed is not None else "<unavailable>",
                    delta if delta is not None else "<unavailable>",
                    delay if delay is not None else "<unavailable>",
                )
            )
        return False


class PostSPIDoTransferBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSPIDoTransferBreakpoint, self).__init__("ChibiOS::SPIDevice::do_transfer", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        frame = gdb.selected_frame()
        bus = _safe_int(_safe_eval("this->device_desc.bus"))
        if bus not in (0, 2):
            return False
        bus_label = "spi1" if bus == 0 else "spi4"
        count = _inc_counter("post_spi_transfer_events")
        if count > 80:
            self.enabled = False
            print("[post-spi] disabling SPIDevice::do_transfer after %u hits" % count)
            return False
        send = _safe_frame_var(frame, "send")
        recv = _safe_frame_var(frame, "recv")
        length = _safe_frame_var(frame, "len")
        device = _safe_int(_safe_eval("this->device_desc.device"))
        name = _safe_cstr(_safe_eval("this->device_desc.name"))
        if _maybe_print(count, first=32, every=20):
            print(
                "\n[post-spi] do_transfer hit=%u bus=%s thread=%s dev=%s device=%s len=%s tx=%s"
                % (
                    count,
                    bus_label,
                    _selected_thread_name(),
                    name,
                    device if device is not None else "<unavailable>",
                    _safe_int(length),
                    _read_bytes(send, length, 16),
                )
            )

            def on_return(_ret):
                print("[post-spi] do_transfer return hit=%u bus=%s dev=%s ret=%s rx=%s" % (count, bus_label, name, _safe_str(_ret), _read_bytes(recv, length, 16)))

            InspectReturn("ChibiOS::SPIDevice::do_transfer post-callback", on_return)
        return False


class PostSpiLldExchangeBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSpiLldExchangeBreakpoint, self).__init__("spi_lld_exchange", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        frame = gdb.selected_frame()
        spip = _safe_frame_var(frame, "spip")
        spip_i = _safe_int(spip)
        spid1_i = _safe_int(_safe_eval("&SPID1"))
        spid4_i = _safe_int(_safe_eval("&SPID4"))
        if spip_i == spid1_i:
            bus_label = "SPID1"
        elif spip_i == spid4_i:
            bus_label = "SPID4"
        else:
            return False
        count = _inc_counter("post_spi_exchange_events")
        if count > 80:
            self.enabled = False
            print("[post-spi] disabling spi_lld_exchange after %u hits" % count)
            return False
        n = _safe_frame_var(frame, "n")
        txbuf = _safe_frame_var(frame, "txbuf")
        rxbuf = _safe_frame_var(frame, "rxbuf")
        if _maybe_print(count, first=32, every=20):
            print(
                "[post-spi] spi_lld_exchange enter hit=%u bus=%s thread=%s spip=%s n=%s tx=%s rxbuf=%s dmarx=%s dmatx=%s"
                % (
                    count,
                    bus_label,
                    _selected_thread_name(),
                    _safe_str(spip),
                    _safe_int(n),
                    _read_bytes(txbuf, n, 16),
                    _safe_str(rxbuf),
                    _safe_str(_safe_eval("spip->dmarx")),
                    _safe_str(_safe_eval("spip->dmatx")),
                )
            )
            _dump_spi_dma_pair("exchange-enter", spip)
            if count <= 4:
                _bt(7)

            def on_return(_ret):
                print("[post-spi] spi_lld_exchange return hit=%u bus=%s rx=%s thread=%s" % (count, bus_label, _read_bytes(rxbuf, n, 16), _selected_thread_name()))
                _dump_spi_dma_pair("exchange-return", spip)

            InspectReturn("spi_lld_exchange post-callback", on_return)
        return False


class PostEventWaitBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostEventWaitBreakpoint, self).__init__("chEvtWaitAnyTimeout", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("post_event_wait_events")
        if count > 220:
            self.enabled = False
            print("[post-event] disabling chEvtWaitAnyTimeout after %u hits" % count)
            return False
        frame = gdb.selected_frame()
        thread_ptr = _current_thread_ptr()
        events = _safe_int(_safe_frame_var(frame, "events"))
        timeout = _safe_int(_safe_frame_var(frame, "timeout"))
        interesting = _thread_is_watched(thread_ptr) or _event_is_interesting(events) or _maybe_print(count, first=16, every=50)
        if interesting:
            print(
                "[post-event] wait enter hit=%u thread=%s events=%s timeout=%s caller=%s"
                % (
                    count,
                    _thread_summary(thread_ptr),
                    "0x%x" % events if events is not None else "<unavailable>",
                    timeout if timeout is not None else "<unavailable>",
                    _selected_func_name(),
                )
            )

            def on_return(ret):
                ret_i = _safe_int(ret)
                print(
                    "[post-event] wait return hit=%u ret=%s interesting=%s thread=%s"
                    % (
                        count,
                        _safe_str(ret),
                        _event_is_interesting(ret_i),
                        _thread_summary(thread_ptr),
                    )
                )

            InspectReturn("chEvtWaitAnyTimeout post-callback", on_return)
        return False


class PostUARTWaitTimeoutBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostUARTWaitTimeoutBreakpoint, self).__init__("ChibiOS::UARTDriver::wait_timeout", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        frame = gdb.selected_frame()
        this = _safe_int(_safe_frame_var(frame, "this"))
        uart_io = _safe_int(_safe_eval("&uart_io"))
        if this is None or uart_io is None or this != uart_io:
            return False
        count = _inc_counter("post_uart_wait_events")
        if count > 120:
            self.enabled = False
            print("[uart-wait] disabling uart_io wait_timeout after %u hits" % count)
            return False
        n = _safe_int(_safe_frame_var(frame, "n"))
        timeout_ms = _safe_int(_safe_frame_var(frame, "timeout_ms"))
        if _maybe_print(count, first=24, every=20):
            print(
                "\n[uart-wait] enter hit=%u thread=%s n=%s timeout_ms=%s"
                % (
                    count,
                    _selected_thread_name(),
                    n if n is not None else "<unavailable>",
                    timeout_ms if timeout_ms is not None else "<unavailable>",
                )
            )
            _dump_uart_driver_state("wait-enter", this)

            def on_return(ret):
                print("[uart-wait] return hit=%u ret=%s thread=%s" % (count, _safe_str(ret), _selected_thread_name()))
                _dump_uart_driver_state("wait-return", this)

            InspectReturn("uart_io wait_timeout post-callback", on_return)
        return False


class PostDMA2Stream1IRQBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostDMA2Stream1IRQBreakpoint, self).__init__("Vector124", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("post_dma2_stream1_irq_events")
        if count > 80:
            self.enabled = False
            print("[dma2-stream1-irq] disabling Vector124 after %u hits" % count)
            return False
        lisr = _read_u32(0x40026400)
        flags = ((lisr >> 6) & 0x3D) if lisr is not None else None
        # Vector124 serves DMA2 Stream1; on CubeBlack this is USART6 RX for uart_io.
        func = _read_u32(0x20018810 + 0x4C)
        param = _read_u32(0x20018810 + 0x50)
        if _maybe_print(count, first=24, every=10):
            print(
                "\n[dma2-stream1-irq] enter hit=%u thread=%s flags=%s func=%s symbol=%s param=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _fmt_u32(flags),
                    _fmt_u32(func),
                    _symbol_at(func),
                    _fmt_u32(param),
                )
            )
            _dump_dma2_stream("irq-enter USART6-RX", 1)
            if param is not None and param == _safe_int(_safe_eval("&uart_io")):
                _dump_uart_driver_state("irq-enter", param)
        return False


class PostUARTRxBuffFullBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostUARTRxBuffFullBreakpoint, self).__init__("ChibiOS::UARTDriver::rxbuff_full_irq", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        frame = gdb.selected_frame()
        self_ptr = _safe_int(_safe_frame_var(frame, "self"))
        if self_ptr is None:
            self_ptr = _safe_int(_safe_eval("$r0"))
        uart_io = _safe_int(_safe_eval("&uart_io"))
        if self_ptr is None or uart_io is None or self_ptr != uart_io:
            return False
        count = _inc_counter("post_uart_rxbuff_events")
        if count > 80:
            self.enabled = False
            print("[uart-rx-full] disabling uart_io rxbuff_full_irq after %u hits" % count)
            return False
        flags = _safe_int(_safe_frame_var(frame, "flags"))
        if flags is None:
            flags = _safe_int(_safe_eval("$r1"))
        if _maybe_print(count, first=24, every=10):
            print(
                "\n[uart-rx-full] enter hit=%u thread=%s self=%s flags=%s lr_symbol=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _fmt_u32(self_ptr),
                    _fmt_u32(flags),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
            _dump_uart_driver_state("rx-full-enter", self_ptr)
        return False


class PostUARTRxSignalSiteBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostUARTRxSignalSiteBreakpoint, self).__init__("*0x0813b610", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        uart_drv = _safe_int(_safe_eval("$r4"))
        uart_io = _safe_int(_safe_eval("&uart_io"))
        if uart_drv is None or uart_io is None or uart_drv != uart_io:
            return False
        count = _inc_counter("post_uart_signal_site_events")
        available = _safe_int(_safe_eval("$r0"))
        needed = _safe_int(_safe_eval("$r3"))
        print(
            "\n[uart-rx-signal-site] hit=%u thread=%s uart=0x%08x available=%s needed=%s pc=%s lr=%s"
            % (
                count,
                _selected_thread_name(),
                uart_drv & 0xFFFFFFFF,
                available if available is not None else "<unavailable>",
                needed if needed is not None else "<unavailable>",
                _safe_str(_safe_eval("$pc")),
                _safe_str(_safe_eval("$lr")),
            )
        )
        _dump_uart_driver_state("rx-signal-site", uart_drv)
        print("[uart-rx-signal-site] stopped before chEvtSignalI(uart_io._wait.thread_ctx, EVT_DATA)")
        return True


class PostEventSignalIBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostEventSignalIBreakpoint, self).__init__("chEvtSignalI", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        target = _safe_int(_safe_eval("$r0"))
        events = _safe_int(_safe_eval("$r1"))
        if not (_thread_is_watched(target) or _event_is_interesting(events)):
            return False
        count = _inc_counter("post_evt_signal_i_events")
        if count > 180:
            self.enabled = False
            print("[post-event-signal] disabling chEvtSignalI after %u focused hits" % count)
            return False
        if _maybe_print(count, first=80, every=20):
            print(
                "\n[post-event-signal] hit=%u src=%s target=%s events=%s will_ready=%s caller=%s lr_symbol=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _thread_summary(target),
                    _fmt_u32(events),
                    _signal_will_ready(target, events),
                    _selected_func_name(),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
            if count <= 3:
                print("[post-event-signal] backtrace intentionally skipped; use uart-rx-signal-site for this path")
        return False


class PostSchWakeupBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSchWakeupBreakpoint, self).__init__("*0x0814e3fc", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        target = _safe_int(_safe_eval("$r1"))
        if not _thread_is_watched(target):
            return False
        count = _inc_counter("post_evt_wait_timeout_wakeup_events")
        if count > 180:
            self.enabled = False
            print("[post-timeout-wakeup] disabling __sch_wakeup after %u focused hits" % count)
            return False
        if _maybe_print(count, first=80, every=20):
            print(
                "[post-timeout-wakeup] hit=%u src=%s target=%s vtp=%s lr_symbol=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _thread_summary(target),
                    _safe_str(_safe_eval("$r0")),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
        return False


class PostSchReadyIBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSchReadyIBreakpoint, self).__init__("chSchReadyI", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        target = _safe_int(_safe_eval("$r0"))
        if not _thread_is_watched(target):
            return False
        count = _inc_counter("post_sch_ready_events")
        if count > 180:
            self.enabled = False
            print("[post-ready] disabling chSchReadyI after %u focused hits" % count)
            return False
        if _maybe_print(count, first=80, every=20):
            print(
                "[post-ready] hit=%u src=%s target=%s caller=%s lr_symbol=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _thread_summary(target),
                    _selected_func_name(),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
        return False


class PostSchSleepTimeoutBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSchSleepTimeoutBreakpoint, self).__init__("chSchGoSleepTimeoutS", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        thread_ptr = _current_thread_ptr()
        if not _thread_is_watched(thread_ptr):
            return False
        count = _inc_counter("post_sch_sleep_timeout_events")
        if count > 180:
            self.enabled = False
            print("[post-sleep-timeout] disabling chSchGoSleepTimeoutS after %u focused hits" % count)
            return False
        state = _safe_int(_safe_eval("$r0"))
        timeout = _safe_int(_safe_eval("$r1"))
        if _maybe_print(count, first=80, every=20):
            print(
                "[post-sleep-timeout] enter hit=%u thread=%s newstate=%s timeout=%s caller=%s lr_symbol=%s"
                % (
                    count,
                    _thread_summary(thread_ptr),
                    state if state is not None else "<unavailable>",
                    timeout if timeout is not None else "<unavailable>",
                    _selected_func_name(),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
        return False


class PostSemaphoreGiveBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSemaphoreGiveBreakpoint, self).__init__("ChibiOS::Semaphore::give", internal=False)

    def stop(self):
        if not (_post_inv_active() or _stack_contains("AP_InertialSensor_Invensense::start", 10)):
            return False
        count = _inc_counter("post_sem_give_events")
        if count > 40:
            self.enabled = False
            print("[post-sem] disabling Semaphore::give after %u hits" % count)
            return False
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        context = "other"
        if _stack_contains("AP_InertialSensor_Invensense::start", 10):
            context = "Invensense::start"
        elif _stack_contains("DeviceBus::bus_thread", 10):
            context = "DeviceBus::bus_thread"
        if _maybe_print(count, first=16, every=10):
            print("[post-sem] give enter hit=%u context=%s thread=%s this=%s" % (count, context, _selected_thread_name(), _safe_str(this)))
            if count <= 4:
                _bt(8)

            def on_return(ret):
                print("[post-sem] give return hit=%u context=%s ret=%s thread=%s" % (count, context, _safe_str(ret), _selected_thread_name()))

            InspectReturn("ChibiOS::Semaphore::give post-callback", on_return)
        return False


class PostSharedDMALockBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSharedDMALockBreakpoint, self).__init__("ChibiOS::Shared_DMA::lock", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        if not _stack_contains("ChibiOS::SPIDevice::acquire_bus", 8):
            return False
        count = _inc_counter("post_shared_dma_lock_events")
        if count > 24:
            self.enabled = False
            print("[shared-dma] disabling Shared_DMA::lock after %u matching hits" % count)
            return False
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        if _maybe_print(count, first=16, every=4):
            print(
                "\n[shared-dma] lock enter hit=%u thread=%s this=%s stream1=%s stream2=%s have_lock=%s contention=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _safe_str(this),
                    _safe_str(_safe_eval("this->stream_id1")),
                    _safe_str(_safe_eval("this->stream_id2")),
                    _safe_str(_safe_eval("this->have_lock")),
                    _safe_str(_safe_eval("this->contention")),
                )
            )
            if count <= 4:
                _bt(7)

            def on_return(_ret):
                have_lock = "<unavailable>"
                contention = "<unavailable>"
                if this_addr is not None:
                    have_lock = _safe_str(_safe_eval("((ChibiOS::Shared_DMA*)0x%x)->have_lock" % this_addr))
                    contention = _safe_str(_safe_eval("((ChibiOS::Shared_DMA*)0x%x)->contention" % this_addr))
                print(
                    "[shared-dma] lock return hit=%u thread=%s have_lock=%s contention=%s"
                    % (
                        count,
                        _selected_thread_name(),
                        have_lock,
                        contention,
                    )
                )

            InspectReturn("ChibiOS::Shared_DMA::lock post-callback", on_return)
        return False


class PostSharedDMALockStreamBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSharedDMALockStreamBreakpoint, self).__init__("*0x0813caac", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("post_shared_dma_lock_stream_events")
        if count > 20:
            self.enabled = False
            print("[shared-dma] disabling lock_stream PC after %u hits" % count)
            return False
        stream_id = _safe_int(_safe_eval("$r0"))
        owner = "<unavailable>"
        if stream_id is not None:
            owner = _safe_str(_safe_eval("ChibiOS::Shared_DMA::locks[%u].mutex.owner" % stream_id))
        print(
            "[shared-dma] lock_stream hot-pc hit=%u pc=%s thread=%s stream_id=%s current_owner=%s r0=%s lr=%s"
            % (
                count,
                _safe_str(_safe_eval("$pc")),
                _selected_thread_name(),
                stream_id if stream_id is not None else "<unavailable>",
                owner,
                _safe_str(_safe_eval("$r0")),
                _safe_str(_safe_eval("$lr")),
            )
        )
        if count <= 6:
            _bt(8)
        return False


class PostSharedDMAUnlockBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSharedDMAUnlockBreakpoint, self).__init__("ChibiOS::Shared_DMA::unlock", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        if not _stack_contains("ChibiOS::SPIDevice::release_bus", 8):
            return False
        count = _inc_counter("post_shared_dma_unlock_events")
        if count > 24:
            self.enabled = False
            print("[shared-dma] disabling Shared_DMA::unlock after %u matching hits" % count)
            return False
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        if _maybe_print(count, first=16, every=4):
            print(
                "[shared-dma] unlock enter hit=%u thread=%s this=%s stream1=%s stream2=%s have_lock=%s contention=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _safe_str(this),
                    _safe_str(_safe_eval("this->stream_id1")),
                    _safe_str(_safe_eval("this->stream_id2")),
                    _safe_str(_safe_eval("this->have_lock")),
                    _safe_str(_safe_eval("this->contention")),
                )
            )
        return False


class PostNoNewCoveragePCBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostNoNewCoveragePCBreakpoint, self).__init__("*0x0814bc6e", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("no_new_pc_events")
        frame = gdb.selected_frame()
        spip = _safe_frame_var(frame, "spip")
        n = _safe_frame_var(frame, "n")
        txbuf = _safe_frame_var(frame, "txbuf")
        rxbuf = _safe_frame_var(frame, "rxbuf")
        print("\n[exchange-setup-pc] hit=%u pc=%s func=%s thread=%s" % (count, _safe_str(_safe_eval("$pc")), _selected_func_name(), _selected_thread_name()))
        print(
            "[exchange-setup-pc] spi_lld_exchange spip=%s n=%s txbuf=%s rxbuf=%s tx=%s rx=%s"
            % (
                _safe_str(spip),
                _safe_int(n) if _safe_int(n) is not None else "<unavailable>",
                _safe_str(txbuf),
                _safe_str(rxbuf),
                _read_bytes(txbuf, n, 16),
                _read_bytes(rxbuf, n, 16),
            )
        )
        print(
            "[exchange-setup-pc] spip_state state=%s config=%s dmarx=%s dmatx=%s rxdmamode=%s txdmamode=%s"
            % (
                _safe_str(_safe_eval("spip->state")),
                _safe_str(_safe_eval("spip->config")),
                _safe_str(_safe_eval("spip->dmarx")),
                _safe_str(_safe_eval("spip->dmatx")),
                _safe_str(_safe_eval("spip->rxdmamode")),
                _safe_str(_safe_eval("spip->txdmamode")),
            )
        )
        print(
            "[exchange-setup-pc] regs r0=%s r1=%s r2=%s r3=%s r4=%s lr=%s"
            % (
                _safe_str(_safe_eval("$r0")),
                _safe_str(_safe_eval("$r1")),
                _safe_str(_safe_eval("$r2")),
                _safe_str(_safe_eval("$r3")),
                _safe_str(_safe_eval("$r4")),
                _safe_str(_safe_eval("$lr")),
            )
        )
        _dump_spi_dma_pair("setup-pc", spip)

        def on_return(_ret):
            print("[exchange-setup-pc] spi_lld_exchange returned hit=%u rx=%s thread=%s" % (count, _read_bytes(rxbuf, n, 16), _selected_thread_name()))
            _dump_spi_dma_pair("setup-pc-return", spip)

        InspectReturn("spi_lld_exchange setup-pc probe", on_return)
        self.enabled = False
        print("[exchange-setup-pc] continuing after focused setup snapshot")
        return False


class PostSleepSnapshotBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PostSleepSnapshotBreakpoint, self).__init__("chSchGoSleepS", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("post_sleep_events")
        if count > 8:
            self.enabled = False
            print("[post-sleep] disabling chSchGoSleepS snapshot after %u hits" % count)
            return False
        frame = gdb.selected_frame()
        newstate = _safe_int(_safe_frame_var(frame, "newstate"))
        if count <= 4:
            print(
                "\n[post-sleep] hit=%u thread=%s newstate=%s pc=%s caller=%s"
                % (
                    count,
                    _selected_thread_name(),
                    newstate if newstate is not None else "<unavailable>",
                    _safe_str(_safe_eval("$pc")),
                    _selected_func_name(),
                )
            )
            _bt(6)
        return False


class VTimerInsertFirstBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(VTimerInsertFirstBreakpoint, self).__init__("*0x0814eaa0", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vt_insert_first_events")
        if count > 80:
            self.enabled = False
            print("[vt-arm] disabling vt_insert_first after %u hits" % count)
            return False
        vtp = _safe_int(_safe_eval("$r1"))
        now = _safe_int(_safe_eval("$r2"))
        delay = _safe_int(_safe_eval("$r3"))
        vt = _read_vtimer(vtp)
        effective = delay
        if effective is not None and effective <= 9:
            effective = 10
        target = None
        if now is not None and effective is not None:
            target = (now + effective) & 0xFFFFFFFF
        if _maybe_print(count, first=24, every=20):
            print(
                "[vt-arm] insert_first hit=%u thread=%s vtp=0x%08x now=%s delay=%s effective=%s target=%s func=0x%08x symbol=%s par=%s reload=%s"
                % (
                    count,
                    _selected_thread_name(),
                    vtp or 0,
                    now if now is not None else "<unavailable>",
                    delay if delay is not None else "<unavailable>",
                    effective if effective is not None else "<unavailable>",
                    "0x%08x" % target if target is not None else "<unavailable>",
                    vt["func"] or 0,
                    _symbol_at(vt["func"]),
                    _fmt_u32(vt["par"]),
                    _fmt_u32(vt["reload"]),
                )
            )
            _dump_tim5_state("vt_insert_first")
        return False


class VTimerSetAlarmBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(VTimerSetAlarmBreakpoint, self).__init__("*0x0814eaec", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vt_set_alarm_events")
        if count > 120:
            self.enabled = False
            print("[vt-arm] disabling vt_set_alarm after %u hits" % count)
            return False
        now = _safe_int(_safe_eval("$r0"))
        delay = _safe_int(_safe_eval("$r1"))
        effective = delay
        if effective is not None and effective <= 9:
            effective = 10
        target = None
        if now is not None and effective is not None:
            target = (now + effective) & 0xFFFFFFFF
        if _maybe_print(count, first=32, every=20):
            print(
                "[vt-arm] set_alarm hit=%u thread=%s now=%s delay=%s effective=%s target=%s"
                % (
                    count,
                    _selected_thread_name(),
                    now if now is not None else "<unavailable>",
                    delay if delay is not None else "<unavailable>",
                    effective if effective is not None else "<unavailable>",
                    "0x%08x" % target if target is not None else "<unavailable>",
                )
            )
            _dump_tim5_state("vt_set_alarm")
        return False


class VTimerCallbackInvokeBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(VTimerCallbackInvokeBreakpoint, self).__init__("*0x0814ec04", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vt_callback_events")
        if count > 160:
            self.enabled = False
            print("[vt-callback] disabling callback invoke breakpoint after %u hits" % count)
            return False
        vtp = _safe_int(_safe_eval("$r4"))
        func = _safe_int(_safe_eval("$r3"))
        par = _safe_int(_safe_eval("$r1"))
        vt = _read_vtimer(vtp)
        func_count, symbol = _record_vt_callback(func)
        if _maybe_print(count, first=40, every=20):
            print(
                "[vt-callback] invoke hit=%u func_count=%u thread=%s vtp=0x%08x func=0x%08x symbol=%s par=%s mem_func=%s mem_par=%s delta=%s reload=%s next=%s prev=%s"
                % (
                    count,
                    func_count,
                    _selected_thread_name(),
                    vtp or 0,
                    _thumb_addr(func) or 0,
                    symbol,
                    _fmt_u32(par),
                    _fmt_u32(vt["func"]),
                    _fmt_u32(vt["par"]),
                    _fmt_u32(vt["delta"]),
                    _fmt_u32(vt["reload"]),
                    _fmt_u32(vt["next"]),
                    _fmt_u32(vt["prev"]),
                )
            )
            _dump_tim5_state("vt_callback")
        return False


class Vector108EntryBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(Vector108EntryBreakpoint, self).__init__("*0x08150ed4", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vector108_entry_events")
        lr = _safe_eval("$lr")
        if _maybe_print(count, first=12, every=100):
            print(
                "\n[exc-vector] Vector108 entry hit=%u pc=%s lr=%s %s"
                % (count, _safe_str(_safe_eval("$pc")), _safe_str(lr), _exc_return_info(lr))
            )
            _dump_exception_frame("Vector108-entry", lr)
            _dump_irq_state("Vector108-entry")
        return False


class Vector108AfterIRQBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(Vector108AfterIRQBreakpoint, self).__init__("*0x08150eda", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vector108_after_irq_events")
        sp = _safe_int(_safe_eval("$sp"))
        saved_lr = _read_dumpable_u32(sp + 4) if sp is not None else None
        if _maybe_print(count, first=12, every=100):
            print(
                "[exc-vector] Vector108 after st_lld hit=%u pc=%s lr=%s saved_exc_return=%s %s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                    _fmt_u32(saved_lr),
                    _exc_return_info(saved_lr),
                )
            )
            _dump_words("Vector108-wrapper-stack-after-st_lld", sp, 6)
        return False


class Vector108BeforePopBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(Vector108BeforePopBreakpoint, self).__init__("*0x08150ede", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vector108_pop_events")
        sp = _safe_int(_safe_eval("$sp"))
        saved_r3 = _read_dumpable_u32(sp) if sp is not None else None
        saved_pc = _read_dumpable_u32(sp + 4) if sp is not None else None
        if _maybe_print(count, first=12, every=100) or not _is_exc_return(saved_pc):
            print(
                "[exc-vector] Vector108 before pop hit=%u pc=%s sp=%s saved_r3=%s saved_pc=%s %s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _fmt_u32(sp),
                    _fmt_u32(saved_r3),
                    _fmt_u32(saved_pc),
                    _exc_return_info(saved_pc),
                )
            )
            _dump_words("Vector108-wrapper-stack-before-pop", sp, 6)
        return False


class PortIRQEpilogueEntryBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PortIRQEpilogueEntryBreakpoint, self).__init__("*0x08149a84", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("port_irq_entry_events")
        if _maybe_print(count, first=12, every=100):
            print(
                "[exc-port] __port_irq_epilogue entry hit=%u pc=%s lr=%s psp=%s msp=%s basepri=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                    _safe_str(_safe_eval("$psp")),
                    _safe_str(_safe_eval("$msp")),
                    _safe_str(_safe_eval("$basepri")),
                )
            )
            _dump_words("port-epilogue-entry-psp", _safe_int(_safe_eval("$psp")), 12)
        return False


class PortIRQEpilogueDecisionBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PortIRQEpilogueDecisionBreakpoint, self).__init__("*0x08149abc", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("port_irq_decision_events")
        if _maybe_print(count, first=12, every=100):
            print(
                "[exc-port] __port_irq_epilogue decision hit=%u preempt_required=%s r4_frame=%s psp=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$r0")),
                    _safe_str(_safe_eval("$r4")),
                    _safe_str(_safe_eval("$psp")),
                )
            )
            _dump_words("port-epilogue-r4-frame", _safe_int(_safe_eval("$r4")), 30)
        return False


class PortIRQEpilogueReturnBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PortIRQEpilogueReturnBreakpoint, self).__init__("*0x08149ac2", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("port_irq_return_events")
        sp = _safe_int(_safe_eval("$sp"))
        saved_r4 = _read_dumpable_u32(sp) if sp is not None else None
        saved_pc = _read_dumpable_u32(sp + 4) if sp is not None else None
        if _maybe_print(count, first=12, every=100):
            print(
                "[exc-port] __port_irq_epilogue return-pop hit=%u pc=%s lr=%s sp=%s saved_r4=%s saved_pc=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                    _fmt_u32(sp),
                    _fmt_u32(saved_r4),
                    _fmt_u32(saved_pc),
                )
            )
            _dump_words("port-epilogue-return-stack", sp, 6)
            _dump_words("port-epilogue-return-psp", _safe_int(_safe_eval("$psp")), 30)
        return False


class SVCHandlerReturnBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(SVCHandlerReturnBreakpoint, self).__init__("*0x08149a36", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("svc_return_events")
        lr = _safe_eval("$lr")
        if _maybe_print(count, first=16, every=100):
            print(
                "\n[exc-svc] SVC_Handler return hit=%u pc=%s lr=%s %s psp=%s msp=%s control=%s basepri=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(lr),
                    _exc_return_info(lr),
                    _safe_str(_safe_eval("$psp")),
                    _safe_str(_safe_eval("$msp")),
                    _safe_str(_safe_eval("$control")),
                    _safe_str(_safe_eval("$basepri")),
                )
            )
            _dump_exception_frame("SVC-return", lr)
        return False


class TimerStartAlarmBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(TimerStartAlarmBreakpoint, self).__init__("stStartAlarm", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("tim5_start_alarm_events")
        if count > 120:
            self.enabled = False
            print("[tim5-write] disabling stStartAlarm after %u hits" % count)
            return False
        target = _safe_int(_safe_eval("$r0"))
        cnt = _read_u32(0x40000C24)
        if _maybe_print(count, first=32, every=20):
            print(
                "[tim5-write] stStartAlarm hit=%u thread=%s target=%s cnt=%s delta=%s lr=%s caller=%s"
                % (
                    count,
                    _selected_thread_name(),
                    "0x%08x" % target if target is not None else "<unavailable>",
                    _fmt_u32(cnt),
                    _fmt_u32_delta(target, cnt),
                    _safe_str(_safe_eval("$lr")),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
            _dump_tim5_state("before-stStartAlarm")
        return False


class TimerStopAlarmBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(TimerStopAlarmBreakpoint, self).__init__("stStopAlarm", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("tim5_stop_alarm_events")
        if count > 120:
            self.enabled = False
            print("[tim5-write] disabling stStopAlarm after %u hits" % count)
            return False
        if _maybe_print(count, first=32, every=20):
            print(
                "[tim5-write] stStopAlarm hit=%u thread=%s lr=%s caller=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _safe_str(_safe_eval("$lr")),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
            _dump_tim5_state("before-stStopAlarm")
        return False


class TimerSetAlarmBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(TimerSetAlarmBreakpoint, self).__init__("stSetAlarm", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("tim5_set_alarm_events")
        if count > 160:
            self.enabled = False
            print("[tim5-write] disabling stSetAlarm after %u hits" % count)
            return False
        target = _safe_int(_safe_eval("$r0"))
        cnt = _read_u32(0x40000C24)
        if _maybe_print(count, first=40, every=20):
            print(
                "[tim5-write] stSetAlarm hit=%u thread=%s target=%s cnt=%s delta=%s lr=%s caller=%s"
                % (
                    count,
                    _selected_thread_name(),
                    "0x%08x" % target if target is not None else "<unavailable>",
                    _fmt_u32(cnt),
                    _fmt_u32_delta(target, cnt),
                    _safe_str(_safe_eval("$lr")),
                    _symbol_at(_safe_int(_safe_eval("$lr"))),
                )
            )
            _dump_tim5_state("before-stSetAlarm")
        return False


class TimerIRQAckBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(TimerIRQAckBreakpoint, self).__init__("*0x0815166e", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("tim5_irq_ack_events")
        masked_sr = _safe_int(_safe_eval("$r3"))
        write_val = _safe_int(_safe_eval("$r1"))
        if masked_sr == 0:
            _inc_counter("tim5_spurious_irq_events")
        if count > 160:
            self.enabled = False
            print("[tim5-ack] disabling SR ack breakpoint after %u hits" % count)
            return False
        if masked_sr == 0 or _maybe_print(count, first=40, every=20):
            print(
                "[tim5-ack] hit=%u thread=%s masked_sr=%s sr_write=%s pc=%s lr=%s spurious_count=%u"
                % (
                    count,
                    _selected_thread_name(),
                    _fmt_u32(masked_sr),
                    _fmt_u32(write_val),
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                    _gdb_counter("tim5_spurious_irq_events"),
                )
            )
            _dump_tim5_state("before-irq-sr-ack")
        return False


class TimerCounterBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(TimerCounterBreakpoint, self).__init__("stGetCounter", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("timer_counter_events")
        if count > 64:
            self.enabled = False
            print("[timer-counter] disabling stGetCounter after %u hits" % count)
            return False
        if _maybe_print(count, first=12, every=16):
            print(
                "[timer-counter] hit=%u ret-will-read TIM5 CNT pc=%s lr=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                )
            )
            _dump_tim5_state("stGetCounter")
        return False


class TimerIRQBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(TimerIRQBreakpoint, self).__init__("st_lld_serve_interrupt", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("timer_irq_events")
        if _maybe_print(count, first=16, every=25):
            print(
                "\n[timer-irq] enter hit=%u pc=%s lr=%s xpsr=%s basepri=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                    _safe_str(_safe_eval("$xpsr")),
                    _safe_str(_safe_eval("$basepri")),
                )
            )
            _dump_tim5_state("irq-enter")
            _dump_irq_state("irq-enter")

        if count == 40 and _no_control_progress_after_post():
            _diagnostic_stop("TIM5 system-timer IRQ churn after IMU periodic callback; continuing to exception-return trace")

        if count > 180:
            self.enabled = False
            print("[timer-irq] disabling st_lld_serve_interrupt after %u hits" % count)
        return False


class VTTickBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(VTTickBreakpoint, self).__init__("chVTDoTickI", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vtick_events")
        if count > 120:
            self.enabled = False
            print("[vtick] disabling chVTDoTickI after %u hits" % count)
            return False
        if _maybe_print(count, first=16, every=25):
            print(
                "[vtick] enter hit=%u pc=%s lr=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                )
            )
            _dump_tim5_state("vtick-enter")
        return False


class VTTickLoopBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(VTTickLoopBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/modules/ChibiOS/os/rt/src/chvt.c:508",
            internal=False,
        )

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("vtick_loop_events")
        if count > 80:
            self.enabled = False
            print("[vtick-loop] disabling chVTDoTickI loop snapshot after %u hits" % count)
            return False
        if _maybe_print(count, first=20, every=20):
            frame = gdb.selected_frame()
            print(
                "[vtick-loop] hit=%u thread=%s vtp=%s lasttime=%s now=%s nowdelta=%s head_delta=%s"
                % (
                    count,
                    _selected_thread_name(),
                    _safe_str(_safe_frame_var(frame, "vtp")),
                    _safe_str(_safe_frame_var(frame, "lasttime")),
                    _safe_str(_safe_frame_var(frame, "now")),
                    _safe_str(_safe_frame_var(frame, "nowdelta")),
                    _safe_str(_safe_eval("vtp->dlist.delta")),
                )
            )
            _dump_tim5_state("vtick-loop")
        return False


class PreemptionDecisionBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(PreemptionDecisionBreakpoint, self).__init__("chSchIsPreemptionRequired", internal=False)

    def stop(self):
        if not _post_inv_active():
            return False
        count = _inc_counter("preempt_events")
        if count > 220:
            self.enabled = False
            print("[preempt] disabling chSchIsPreemptionRequired after %u hits" % count)
            return False
        if _maybe_print(count, first=24, every=25):
            print(
                "[preempt] enter hit=%u pc=%s lr=%s"
                % (
                    count,
                    _safe_str(_safe_eval("$pc")),
                    _safe_str(_safe_eval("$lr")),
                )
            )

        if count == 80 and _no_control_progress_after_post():
            _diagnostic_stop("scheduler preemption churn after IMU periodic callback; continuing to exception-return trace")
        return False


class InvensenseV2StartBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseV2StartBreakpoint, self).__init__("AP_InertialSensor_Invensensev2::start", internal=False)

    def stop(self):
        count = _inc_counter("inv2_start_events")
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        print(
            "\n[backend-start] Invensensev2::start enter hit=%u this=%s type=%s gyro_instance=%s accel_instance=%s"
            % (
                count,
                _safe_str(this),
                _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_inv2_type")),
                _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "gyro_instance")),
                _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "accel_instance")),
            )
        )

        def on_return(_ret):
            print(
                "[backend-start] Invensensev2::start returned this=0x%x type=%s gyro_instance=%s accel_instance=%s"
                % (
                    this_addr or 0,
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_inv2_type")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "gyro_instance")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "accel_instance")),
                )
            )

        InspectReturn("AP_InertialSensor_Invensensev2::start", on_return)
        return False


class GyroInitEntryBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GyroInitEntryBreakpoint, self).__init__("AP_InertialSensor::_init_gyro", internal=False)

    def stop(self):
        count = _inc_counter("gyro_init_events")
        frame = gdb.selected_frame()
        this = _safe_frame_var(frame, "this")
        this_addr = _safe_int(this)
        print("\n[gyro-cal] _init_gyro enter hit=%u this=%s" % (count, _safe_str(this)))
        _dump_ins_state("gyro-cal-entry", this_addr)

        def on_return(_ret):
            print("[gyro-cal] _init_gyro returned")
            _dump_ins_state("gyro-cal-return", this_addr)
            for i in range(3):
                print(
                    "[gyro-cal] return gyro[%u] cal_ok=%s offset=%s id=%s"
                    % (
                        i,
                        _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_cal_ok[%u]" % i)),
                        _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_offset[%u]" % i)),
                        _safe_str(_eval_at("AP_InertialSensor", this_addr, "_gyro_id[%u]" % i)),
                    )
                )

        InspectReturn("AP_InertialSensor::_init_gyro", on_return)
        return False


class GyroInitOuterLoopBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GyroInitOuterLoopBreakpoint, self).__init__("libraries/AP_InertialSensor/AP_InertialSensor.cpp:1768", internal=False)

    def stop(self):
        count = _inc_counter("gyro_init_iter_events")
        frame = gdb.selected_frame()
        j = _safe_int(_safe_frame_var(frame, "j"))
        num_gyros = _safe_int(_safe_frame_var(frame, "num_gyros"))
        num_converged = _safe_int(_safe_frame_var(frame, "num_converged"))
        if _maybe_print(count, first=12, every=10):
            print(
                "\n[gyro-cal] outer hit=%u j=%s num_gyros=%s num_converged=%s converged=%s best_diff=%s"
                % (
                    count,
                    j if j is not None else "<unavailable>",
                    num_gyros if num_gyros is not None else "<unavailable>",
                    num_converged if num_converged is not None else "<unavailable>",
                    _fmt_array("converged", 3),
                    _fmt_array("best_diff", 3),
                )
            )
        return False


class GyroInitCollectBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GyroInitCollectBreakpoint, self).__init__("libraries/AP_InertialSensor/AP_InertialSensor.cpp:1784", internal=False)

    def stop(self):
        count = _inc_counter("gyro_init_collect_events")
        if _maybe_print(count, first=8, every=50):
            frame = gdb.selected_frame()
            j = _safe_int(_safe_frame_var(frame, "j"))
            i = _safe_int(_safe_frame_var(frame, "i"))
            print(
                "[gyro-cal] collect hit=%u j=%s i=%s gyro_sum=%s"
                % (
                    count,
                    j if j is not None else "<unavailable>",
                    i if i is not None else "<unavailable>",
                    _fmt_vec_array("gyro_sum", 3),
                )
            )
        return False


class GyroInitEvalBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GyroInitEvalBreakpoint, self).__init__("libraries/AP_InertialSensor/AP_InertialSensor.cpp:1807", internal=False)

    def stop(self):
        count = _inc_counter("gyro_init_eval_events")
        frame = gdb.selected_frame()
        j = _safe_int(_safe_frame_var(frame, "j"))
        k = _safe_int(_safe_frame_var(frame, "k"))
        num_converged = _safe_int(_safe_frame_var(frame, "num_converged"))
        if _maybe_print(count, first=36, every=30):
            idx = k if k is not None else 0
            print(
                "[gyro-cal] eval hit=%u j=%s k=%s num_converged=%s accel_diff=%s gyro_avg=%s gyro_diff=%s diff_norm=%s best_diff=%s converged=%s"
                % (
                    count,
                    j if j is not None else "<unavailable>",
                    k if k is not None else "<unavailable>",
                    num_converged if num_converged is not None else "<unavailable>",
                    _fmt_vec("accel_diff"),
                    _fmt_vec("gyro_avg[%u]" % idx),
                    _fmt_vec("gyro_diff[%u]" % idx),
                    _safe_str(_safe_eval("diff_norm[%u]" % idx)),
                    _safe_str(_safe_eval("best_diff[%u]" % idx)),
                    _safe_str(_safe_eval("converged[%u]" % idx)),
                )
            )
        return False


class GyroInitConvergedBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GyroInitConvergedBreakpoint, self).__init__("libraries/AP_InertialSensor/AP_InertialSensor.cpp:1817", internal=False)

    def stop(self):
        count = _inc_counter("gyro_init_converge_events")
        frame = gdb.selected_frame()
        j = _safe_int(_safe_frame_var(frame, "j"))
        k = _safe_int(_safe_frame_var(frame, "k"))
        idx = k if k is not None else 0
        print(
            "[gyro-cal] convergence-branch hit=%u j=%s k=%s diff_norm=%s gyro_avg=%s last_average=%s num_converged=%s"
            % (
                count,
                j if j is not None else "<unavailable>",
                k if k is not None else "<unavailable>",
                _safe_str(_safe_eval("diff_norm[%u]" % idx)),
                _fmt_vec("gyro_avg[%u]" % idx),
                _fmt_vec("last_average[%u]" % idx),
                _safe_str(_safe_frame_var(frame, "num_converged")),
            )
        )
        return False


class GyroInitFinishBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(GyroInitFinishBreakpoint, self).__init__("libraries/AP_InertialSensor/AP_InertialSensor.cpp:1832", internal=False)

    def stop(self):
        count = _inc_counter("gyro_init_finish_events")
        frame = gdb.selected_frame()
        print(
            "\n[gyro-cal] finish-loop hit=%u num_gyros=%s num_converged=%s converged=%s best_diff=%s best_avg=%s new_offsets=%s"
            % (
                count,
                _safe_str(_safe_frame_var(frame, "num_gyros")),
                _safe_str(_safe_frame_var(frame, "num_converged")),
                _fmt_array("converged", 3),
                _fmt_array("best_diff", 3),
                _fmt_vec_array("best_avg", 3),
                _fmt_vec_array("new_gyro_offset", 3),
            )
        )
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
        if count > 40:
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
        if count > 40:
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
        if count > 60:
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
        if count > 40:
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


class InvensenseV2PollBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseV2PollBreakpoint, self).__init__("AP_InertialSensor_Invensensev2::_poll_data", internal=False)

    def stop(self):
        count = _inc_counter("inv2_poll_events")
        if count > 40:
            self.enabled = False
            print("[imu] disabling Invensensev2::_poll_data breakpoint after %u hits" % count)
            return False
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        if _maybe_print(count, first=10, every=100):
            print(
                "\n[imu] Invensensev2::_poll_data hit=%u this=%s type=%s accel_instance=%s gyro_instance=%s fast_sampling=%s raw_temp=%s"
                % (
                    count,
                    _safe_str(this),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_inv2_type")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "accel_instance")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "gyro_instance")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_fast_sampling")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_raw_temp")),
                )
            )
        return False


class InvensenseV2ReadFifoBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseV2ReadFifoBreakpoint, self).__init__("AP_InertialSensor_Invensensev2::_read_fifo", internal=False)

    def stop(self):
        count = _inc_counter("inv2_fifo_events")
        if count > 40:
            self.enabled = False
            print("[imu] disabling Invensensev2::_read_fifo breakpoint after %u hits" % count)
            return False
        this = _safe_frame_var(gdb.selected_frame(), "this")
        this_addr = _safe_int(this)
        if _maybe_print(count, first=10, every=100):
            print(
                "[imu] Invensensev2::_read_fifo enter hit=%u this=%s type=%s fast_sampling=%s"
                % (
                    count,
                    _safe_str(this),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_inv2_type")),
                    _safe_str(_eval_at("AP_InertialSensor_Invensensev2", this_addr, "_fast_sampling")),
                )
            )
        return False


class InvensenseV2FifoCountBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(InvensenseV2FifoCountBreakpoint, self).__init__(
            "/scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_InertialSensor/AP_InertialSensor_Invensensev2.cpp:520",
            internal=False,
        )

    def stop(self):
        count = _inc_counter("inv2_fifo_count_events")
        if count > 60:
            self.enabled = False
            print("[imu] disabling inv2 fifo_count breakpoint after %u hits" % count)
            return False
        frame = gdb.selected_frame()
        bytes_read = _safe_int(_safe_frame_var(frame, "bytes_read"))
        n_samples = _safe_int(_safe_frame_var(frame, "n_samples"))
        n = _safe_int(_safe_frame_var(frame, "n"))
        rx = _safe_frame_var(frame, "rx")
        if _maybe_print(count, first=20, every=100):
            print(
                "[imu] inv2 fifo_count hit=%u bytes_read=%s n_samples=%s n=%s rx=%s"
                % (
                    count,
                    bytes_read if bytes_read is not None else "<unavailable>",
                    n_samples if n_samples is not None else "<unavailable>",
                    n if n is not None else "<unavailable>",
                    _read_bytes(rx, 16, 16),
                )
            )
        return False


class InvensenseV2AccumulateBreakpoint(gdb.Breakpoint):
    def __init__(self, symbol, label):
        super(InvensenseV2AccumulateBreakpoint, self).__init__(symbol, internal=False)
        self.label = label

    def stop(self):
        count = _inc_counter("inv2_accum_events")
        if count > 40:
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
        if count > 24:
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
install(INSInitBreakpoint)
install(BackendStartBreakpoint)
install(BackendLoopBreakpoint)
install(RegisterSensorBreakpoint, "AP_InertialSensor::register_gyro", "register_gyro", "register_gyro_events")
install(RegisterSensorBreakpoint, "AP_InertialSensor::register_accel", "register_accel", "register_accel_events")
install(InvensenseStartBreakpoint)
install(InvensenseInitBreakpoint)
install(InvensenseHardwareInitBreakpoint)
install(InvensenseStartCheckpointBreakpoint, "*0x0811a630", "after_registers_before_filter")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a6b0", "before_product_id_read")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a7c0", "offset_check_loop")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a846", "before_fifo_alloc")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a858", "before_periodic_callback")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a878", "after_periodic_callback")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a87a", "before_withsem_destructor")
install(InvensenseStartCheckpointBreakpoint, "*0x0811a87e", "before_return")
# Expensive post-callback hooks are installed dynamically at
# after_periodic_callback. Installing them from startup makes GDB stop on every
# delay/event/SPI hit just to evaluate the Python guard.
install(PeriodicRegisterBreakpoint)
install(InvensenseV2StartBreakpoint)
install(GyroInitEntryBreakpoint)
install(GyroInitOuterLoopBreakpoint)
install(GyroInitCollectBreakpoint)
install(GyroInitEvalBreakpoint)
install(GyroInitConvergedBreakpoint)
install(GyroInitFinishBreakpoint)
install(RuntimeCounterBreakpoint, "Rover::ahrs_update", "Rover::ahrs_update", "ahrs_events", 8, 50)
install(RuntimeCounterBreakpoint, "Rover::read_radio", "Rover::read_radio", "read_radio_events", 8, 50)
install(RuntimeCounterBreakpoint, "Rover::one_second_loop", "Rover::one_second_loop", "one_second_events", 4, 1)
install(RuntimeCounterBreakpoint, "AP_InertialSensor::periodic", "AP_InertialSensor::periodic", "ins_periodic_events", 8, 50)
# The FIFO/raw-sample hooks are intentionally disabled for this pass. The last
# run already proved that periodic MPU9250 callbacks and raw sample notification
# are active. The current question is whether startup is stuck around
# ChibiOS timer/preemption churn, service-thread wakeups, or shared-DMA locking.
# install(InvensensePollBreakpoint)
# install(InvensenseReadFifoBreakpoint)
# install(InvensenseFifoCountBreakpoint)
# install(InvensenseAccumulateBreakpoint, "AP_InertialSensor_Invensense::_accumulate_sensor_rate_sampling", "Invensense::_accumulate_sensor_rate_sampling")
# install(InvensenseAccumulateBreakpoint, "AP_InertialSensor_Invensense::_accumulate", "Invensense::_accumulate")
# install(InvensenseV2PollBreakpoint)
# install(InvensenseV2ReadFifoBreakpoint)
# install(InvensenseV2FifoCountBreakpoint)
# install(InvensenseV2AccumulateBreakpoint, "AP_InertialSensor_Invensensev2::_accumulate_sensor_rate_sampling", "Invensensev2::_accumulate_sensor_rate_sampling")
# install(InvensenseV2AccumulateBreakpoint, "AP_InertialSensor_Invensensev2::_accumulate", "Invensensev2::_accumulate")
# install(SampleNotifyBreakpoint, "AP_InertialSensor_Backend::_notify_new_accel_raw_sample", "accel_raw_sample", "accel_notify_events")
# install(SampleNotifyBreakpoint, "AP_InertialSensor_Backend::_notify_new_gyro_raw_sample", "gyro_raw_sample", "gyro_notify_events")

print("[diag] breakpoints installed; continuing target")
end

continue
