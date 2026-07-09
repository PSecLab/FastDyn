# FastDyn CUBS2 network-init/Zenoh diagnostic.
#
# Start probe-run with --run-gdb, then run from the FastDyn repo:
#   script -q -f fastdyn_work_cerebri_cubs2_logs/gdb_cerebri_cubs2_diagnostic_output.log -c 'gdb-multiarch -q -x gdbscripts/cerebri_cubs2_diagnostic.gdb'
#
# Current hypothesis:
#   The init ordering, PHY link, and ENET TX-completion blockers are fixed.
#   Zenoh now sends its first UDP handshake packet and waits/retries in the
#   receive side. This script captures socket/IP/ENET TX plus ENET RX, UDP recv,
#   and Zenoh transport receive paths.

set pagination off
set confirm off
set print pretty on
set print elements 0
set print thread-events off
set breakpoint pending on
set style enabled off

file /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/build-mr_vmu_tropic/zephyr/zephyr.elf
directory /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2
directory /scratch/Fastdyn/zephyr_rehosting/modules/lib/csyn/zephyr/src
directory /scratch/Fastdyn/zephyr_rehosting/modules/lib/zenoh-pico/src
directory /scratch/Fastdyn/zephyr_rehosting/zephyr/subsys/net
directory /scratch/Fastdyn/zephyr_rehosting/zephyr/subsys/net/ip
directory /scratch/Fastdyn/zephyr_rehosting/zephyr/subsys/net/lib/sockets

target remote localhost:1234

set $diag_events = 0
set $net_init_events = 0
set $net_context_init_events = 0
set $csyn_init_events = 0
set $zenoh_thread_events = 0
set $z_open_events = 0
set $udp_open_events = 0
set $zsock_socket_events = 0
set $net_context_get_events = 0
set $ctx_sem_take_events = 0
set $ctx_sem_give_events = 0
set $zenoh_open_success = 0
set $zenoh_open_failed = 0
set $main_entries = 0
set $idle_output_events = 0
set $control_step_events = 0
set $send_t_msg_events = 0
set $send_wbuf_events = 0
set $sendto_events = 0
set $zsock_sendto_ctx_events = 0
set $net_context_sendto_events = 0
set $net_try_send_data_events = 0
set $net_if_try_send_data_events = 0
set $enet_tx_events = 0
set $iface_up_events = 0
set $carrier_on_events = 0
set $recv_t_msg_events = 0
set $recv_zbuf_events = 0
set $read_udp_events = 0
set $zsock_recvfrom_events = 0
set $zsock_recvfrom_ctx_events = 0
set $net_context_recv_events = 0
set $enet_isr_events = 0
set $enet_rx_irq_events = 0
set $enet_rx_events = 0
set $net_recv_data_events = 0
set $max_diag_events = 300

define dump_regs
  printf "regs pc=%p lr=%p sp=%p r0=%#x r1=%#x r2=%#x r3=%#x r4=%#x r5=%#x r6=%#x r7=%#x r8=%#x r9=%#x r10=%#x r11=%#x r12=%#x\n", $pc, $lr, $sp, $r0, $r1, $r2, $r3, $r4, $r5, $r6, $r7, $r8, $r9, $r10, $r11, $r12
end

define dump_current_thread
  set $t = _kernel.cpus[0].current
  if $t
    printf "current_thread=%p name=%s state=0x%x prio=%d pended_on=%p timeout_dticks=%d psp=%p\n", $t, $t->name, $t->base.thread_state, $t->base.prio, $t->base.pended_on, $t->base.timeout.dticks, $t->callee_saved.psp
  else
    printf "current_thread=NULL\n"
  end
end

define dump_threads
  set $t = _kernel.threads
  set $i = 0
  printf "thread_list:\n"
  while $t && $i < 16
    printf "  [%02d] thread=%p name=%s state=0x%x prio=%d pended_on=%p timeout_dticks=%d entry=%p\n", $i, $t, $t->name, $t->base.thread_state, $t->base.prio, $t->base.pended_on, $t->base.timeout.dticks, $t->entry.pEntry
    set $t = $t->next_thread
    set $i = $i + 1
  end
end

define dump_contexts_lock
  printf "contexts_lock=%p count=%u limit=%u wait_head=%p wait_tail=%p\n", &contexts_lock, contexts_lock.count, contexts_lock.limit, contexts_lock.wait_q.waitq.head, contexts_lock.wait_q.waitq.tail
end

define dump_iface_ptr
  if $argc > 0 && $arg0
    set $iface = (struct net_if *)$arg0
    if $iface->if_dev
      set $flags0 = $iface->if_dev->flags[0]
      printf "iface=%p if_dev=%p dev=%p dev_name=%s flags0=%#x admin_up=%u lower_up=%u running=%u suspended=%u oper_state=%d mtu=%u\n", $iface, $iface->if_dev, $iface->if_dev->dev, $iface->if_dev->dev->name, $flags0, (($flags0 & (1 << 0)) != 0), (($flags0 & (1 << 9)) != 0), (($flags0 & (1 << 8)) != 0), (($flags0 & (1 << 4)) != 0), $iface->if_dev->oper_state, $iface->if_dev->mtu
    else
      printf "iface=%p if_dev=NULL\n", $iface
    end
  else
    printf "iface=NULL\n"
  end
end

define dump_summary
  printf "[summary] net_init=%u net_context_init=%u csyn_init=%u zenoh_thread=%u z_open=%u udp_open=%u zsock_socket=%u net_context_get=%u ctx_sem_take=%u ctx_sem_give=%u open_success=%u open_failed=%u main=%u idle_output=%u control_step=%u\n", $net_init_events, $net_context_init_events, $csyn_init_events, $zenoh_thread_events, $z_open_events, $udp_open_events, $zsock_socket_events, $net_context_get_events, $ctx_sem_take_events, $ctx_sem_give_events, $zenoh_open_success, $zenoh_open_failed, $main_entries, $idle_output_events, $control_step_events
  printf "[summary-tx] send_t_msg=%u send_wbuf=%u sendto=%u zsock_sendto_ctx=%u net_context_sendto=%u net_try_send_data=%u net_if_try_send_data=%u enet_tx=%u iface_up=%u carrier_on=%u\n", $send_t_msg_events, $send_wbuf_events, $sendto_events, $zsock_sendto_ctx_events, $net_context_sendto_events, $net_try_send_data_events, $net_if_try_send_data_events, $enet_tx_events, $iface_up_events, $carrier_on_events
  printf "[summary-rx] recv_t_msg=%u recv_zbuf=%u read_udp=%u zsock_recvfrom=%u zsock_recvfrom_ctx=%u net_context_recv=%u enet_rx=%u net_recv_data=%u\n", $recv_t_msg_events, $recv_zbuf_events, $read_udp_events, $zsock_recvfrom_events, $zsock_recvfrom_ctx_events, $net_context_recv_events, $enet_rx_events, $net_recv_data_events
  printf "[summary-note] ISR-context breakpoints are intentionally skipped; use qemu.log for eth_nxp_enet_isr / ENET_ReceiveIRQHandler evidence.\n"
  dump_contexts_lock
  dump_current_thread
  dump_threads
end

define maybe_stop_on_max_events
  if $diag_events >= $max_diag_events
    printf "\n[diag-stop] max diagnostic events reached\n"
    dump_summary
    bt 12
    quit
  end
end

printf "\n[diag] CUBS2 network-init/Zenoh diagnostic loaded\n"
printf "[diag] contexts_lock address should be 0x20204f54; GDB says %p\n", &contexts_lock
printf "[diag] Expected healthy ordering: net_init/net_context_init before csyn_zenoh socket/open path\n"

break net_init
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_init_events = $net_init_events + 1
  printf "\n[net_init %u] entered before net_context_init call\n", $net_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 8
  maybe_stop_on_max_events
  continue
end

break net_context_init
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_context_init_events = $net_context_init_events + 1
  printf "\n[net_context_init %u] initializing contexts_lock\n", $net_context_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 8
  continue
end

break /scratch/Fastdyn/zephyr_rehosting/zephyr/subsys/net/ip/net_context.c:550
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_context_get_events = $net_context_get_events + 1
  printf "\n[net_context_get %u] before k_sem_take(&contexts_lock); net_context_init_seen=%u\n", $net_context_get_events, $net_context_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 12
  if $net_context_init_events == 0
    printf "\n[diag-stop] net_context_get is trying to take contexts_lock before net_context_init initialized it\n"
    dump_summary
    quit
  end
  maybe_stop_on_max_events
  continue
end

break /scratch/Fastdyn/zephyr_rehosting/zephyr/subsys/net/ip/net_context.c:646
commands
  silent
  set $diag_events = $diag_events + 1
  printf "\n[net_context_get] about to give contexts_lock / return from allocation path\n"
  dump_regs
  dump_current_thread
  dump_contexts_lock
  maybe_stop_on_max_events
  continue
end

break z_impl_k_sem_take
commands
  silent
  if $r0 == (unsigned int)&contexts_lock
    set $diag_events = $diag_events + 1
    set $ctx_sem_take_events = $ctx_sem_take_events + 1
    printf "\n[contexts_lock take %u] timeout=%#x net_context_init_seen=%u\n", $ctx_sem_take_events, $r1, $net_context_init_events
    dump_regs
    dump_current_thread
    dump_contexts_lock
    bt 12
    if $net_context_init_events == 0
      printf "\n[diag-stop] contexts_lock is taken before net_context_init; csyn_zenoh started too early\n"
      dump_summary
      quit
    end
    maybe_stop_on_max_events
  end
  continue
end

break z_impl_k_sem_give
commands
  silent
  if $r0 == (unsigned int)&contexts_lock
    set $diag_events = $diag_events + 1
    set $ctx_sem_give_events = $ctx_sem_give_events + 1
    printf "\n[contexts_lock give %u]\n", $ctx_sem_give_events
    dump_regs
    dump_current_thread
    dump_contexts_lock
    bt 8
    maybe_stop_on_max_events
  end
  continue
end

break csyn_zenoh_init
commands
  silent
  set $diag_events = $diag_events + 1
  set $csyn_init_events = $csyn_init_events + 1
  printf "\n[csyn_zenoh_init %u] creating csyn_zenoh thread; net_context_init_seen=%u\n", $csyn_init_events, $net_context_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 8
  maybe_stop_on_max_events
  continue
end

break csyn_zenoh_thread
commands
  silent
  set $diag_events = $diag_events + 1
  set $zenoh_thread_events = $zenoh_thread_events + 1
  printf "\n[csyn_zenoh_thread %u] entered; net_context_init_seen=%u\n", $zenoh_thread_events, $net_context_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 8
  maybe_stop_on_max_events
  continue
end

break z_open
commands
  silent
  set $diag_events = $diag_events + 1
  set $z_open_events = $z_open_events + 1
  printf "\n[z_open %u] entry zs=%p config=%p options=%p net_context_init_seen=%u\n", $z_open_events, $r0, $r1, $r2, $net_context_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 8
  maybe_stop_on_max_events
  continue
end

break _z_open_udp_unicast
commands
  silent
  set $diag_events = $diag_events + 1
  set $udp_open_events = $udp_open_events + 1
  printf "\n[_z_open_udp_unicast %u] entering socket open path\n", $udp_open_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 10
  maybe_stop_on_max_events
  continue
end

break /scratch/Fastdyn/zephyr_rehosting/modules/lib/zenoh-pico/src/system/zephyr/network.c:474
commands
  silent
  set $diag_events = $diag_events + 1
  printf "\n[_z_open_udp_unicast] returned from socket(); if this fires, socket did not block permanently\n"
  dump_regs
  dump_current_thread
  dump_contexts_lock
  info args
  info locals
  bt 10
  maybe_stop_on_max_events
  continue
end

break z_impl_zsock_socket
commands
  silent
  set $diag_events = $diag_events + 1
  set $zsock_socket_events = $zsock_socket_events + 1
  printf "\n[z_impl_zsock_socket %u] family=%d type=%d proto=%d net_context_init_seen=%u\n", $zsock_socket_events, $r0, $r1, $r2, $net_context_init_events
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 12
  maybe_stop_on_max_events
  continue
end

break zsock_socket_internal
commands
  silent
  set $diag_events = $diag_events + 1
  printf "\n[zsock_socket_internal] entered\n"
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 10
  maybe_stop_on_max_events
  continue
end

break _z_link_send_t_msg
commands
  silent
  set $diag_events = $diag_events + 1
  set $send_t_msg_events = $send_t_msg_events + 1
  printf "\n[_z_link_send_t_msg %u] sending Zenoh transport message link=%p t_msg=%p socket=%p\n", $send_t_msg_events, $r0, $r1, $r2
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break _z_link_send_wbuf
commands
  silent
  set $diag_events = $diag_events + 1
  set $send_wbuf_events = $send_wbuf_events + 1
  printf "\n[_z_link_send_wbuf %u] link=%p wbuf=%p socket=%p\n", $send_wbuf_events, $r0, $r1, $r2
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break /scratch/Fastdyn/zephyr_rehosting/modules/lib/zenoh-pico/src/link/link.c:215
commands
  silent
  set $diag_events = $diag_events + 1
  printf "\n[_z_link_send_wbuf] post-write result; wb==n and ret==0 means TX succeeded, wb!=n means TX failure\n"
  dump_regs
  dump_current_thread
  info locals
  bt 12
  maybe_stop_on_max_events
  continue
end

break _z_send_udp_unicast
commands
  silent
  set $diag_events = $diag_events + 1
  set $sendto_events = $sendto_events + 1
  printf "\n[_z_send_udp_unicast %u] fd=%d buf=%p len=%u endpoint=%p\n", $sendto_events, $r0, $r1, $r2, $r3
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break z_impl_zsock_sendto
commands
  silent
  set $diag_events = $diag_events + 1
  printf "\n[z_impl_zsock_sendto] sock=%d buf=%p len=%u flags=%#x dest_addr=%p addrlen=%u\n", $r0, $r1, $r2, $r3, *(unsigned int *)$sp, *(unsigned int *)($sp + 4)
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break zsock_sendto_ctx
commands
  silent
  set $diag_events = $diag_events + 1
  set $zsock_sendto_ctx_events = $zsock_sendto_ctx_events + 1
  printf "\n[zsock_sendto_ctx %u] ctx=%p buf=%p len=%u flags=%#x dest_addr=%p addrlen=%u\n", $zsock_sendto_ctx_events, $r0, $r1, $r2, $r3, *(unsigned int *)$sp, *(unsigned int *)($sp + 4)
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break net_context_sendto
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_context_sendto_events = $net_context_sendto_events + 1
  printf "\n[net_context_sendto %u] context=%p buf=%p len=%u dst_addr=%p addrlen=%u\n", $net_context_sendto_events, $r0, $r1, $r2, $r3, *(unsigned int *)$sp
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break net_try_send_data
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_try_send_data_events = $net_try_send_data_events + 1
  printf "\n[net_try_send_data %u] pkt=%p\n", $net_try_send_data_events, $r0
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break net_if_try_send_data
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_if_try_send_data_events = $net_if_try_send_data_events + 1
  printf "\n[net_if_try_send_data %u] iface=%p pkt=%p\n", $net_if_try_send_data_events, $r0, $r1
  dump_regs
  dump_current_thread
  dump_iface_ptr $r0
  bt 10
  maybe_stop_on_max_events
  continue
end

break eth_nxp_enet_tx
commands
  silent
  set $diag_events = $diag_events + 1
  set $enet_tx_events = $enet_tx_events + 1
  printf "\n[eth_nxp_enet_tx %u] dev=%p pkt=%p\n", $enet_tx_events, $r0, $r1
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break _z_link_recv_t_msg
commands
  silent
  set $diag_events = $diag_events + 1
  set $recv_t_msg_events = $recv_t_msg_events + 1
  printf "\n[_z_link_recv_t_msg %u] waiting for Zenoh transport message t_msg=%p link=%p socket=%p\n", $recv_t_msg_events, $r0, $r1, $r2
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break _z_link_recv_zbuf
commands
  silent
  set $diag_events = $diag_events + 1
  set $recv_zbuf_events = $recv_zbuf_events + 1
  printf "\n[_z_link_recv_zbuf %u] reading Zenoh link payload link=%p zbuf=%p addr=%p\n", $recv_zbuf_events, $r0, $r1, $r2
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break _z_read_udp_unicast
commands
  silent
  set $diag_events = $diag_events + 1
  set $read_udp_events = $read_udp_events + 1
  printf "\n[_z_read_udp_unicast %u] Zenoh UDP read path entered\n", $read_udp_events
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break z_impl_zsock_recvfrom
commands
  silent
  set $diag_events = $diag_events + 1
  set $zsock_recvfrom_events = $zsock_recvfrom_events + 1
  printf "\n[z_impl_zsock_recvfrom %u] sock=%d buf=%p max_len=%u flags=%#x\n", $zsock_recvfrom_events, $r0, $r1, $r2, $r3
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break zsock_recvfrom_ctx
commands
  silent
  set $diag_events = $diag_events + 1
  set $zsock_recvfrom_ctx_events = $zsock_recvfrom_ctx_events + 1
  printf "\n[zsock_recvfrom_ctx %u] ctx=%p buf=%p max_len=%u flags=%#x src_addr=%p addrlen=%p\n", $zsock_recvfrom_ctx_events, $r0, $r1, $r2, $r3, *(unsigned int *)$sp, *(unsigned int *)($sp + 4)
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

break net_context_recv
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_context_recv_events = $net_context_recv_events + 1
  printf "\n[net_context_recv %u] context=%p cb=%p timeout/raw=%#x\n", $net_context_recv_events, $r0, $r1, $r2
  dump_regs
  dump_current_thread
  bt 10
  maybe_stop_on_max_events
  continue
end

# Do not place GDB breakpoints inside Cortex-M exception/ISR context here.
# Resuming from those breakpoints makes this FastDyn/QEMU/GDB setup chase the
# EXC_RETURN LR value (usually 0xfffffffd) as a memory address. Use qemu.log to
# confirm eth_nxp_enet_isr / ENET_ReceiveIRQHandler execution, and break in the
# RX workqueue path below for debugger-visible state.

break /scratch/Fastdyn/zephyr_rehosting/zephyr/drivers/ethernet/eth_nxp_enet.c:358
commands
  silent
  set $diag_events = $diag_events + 1
  set $enet_rx_events = $enet_rx_events + 1
  printf "\n[eth_nxp_enet_rx %u] RX worker is checking frame size\n", $enet_rx_events
  dump_regs
  dump_current_thread
  bt 8
  maybe_stop_on_max_events
  continue
end

break net_recv_data
commands
  silent
  set $diag_events = $diag_events + 1
  set $net_recv_data_events = $net_recv_data_events + 1
  printf "\n[net_recv_data %u] packet delivered into Zephyr IP stack iface=%p pkt=%p\n", $net_recv_data_events, $r0, $r1
  dump_regs
  dump_current_thread
  bt 8
  maybe_stop_on_max_events
  continue
end

break net_if_up
commands
  silent
  set $diag_events = $diag_events + 1
  set $iface_up_events = $iface_up_events + 1
  printf "\n[net_if_up %u] iface=%p\n", $iface_up_events, $r0
  dump_regs
  dump_current_thread
  dump_iface_ptr $r0
  bt 8
  maybe_stop_on_max_events
  continue
end

break net_if_carrier_on
commands
  silent
  set $diag_events = $diag_events + 1
  set $carrier_on_events = $carrier_on_events + 1
  printf "\n[net_if_carrier_on %u] iface=%p\n", $carrier_on_events, $r0
  dump_regs
  dump_current_thread
  dump_iface_ptr $r0
  bt 8
  maybe_stop_on_max_events
  continue
end

break /scratch/Fastdyn/zephyr_rehosting/modules/lib/csyn/zephyr/src/csyn_zenoh.c:175
commands
  silent
  set $diag_events = $diag_events + 1
  set $zenoh_open_failed = $zenoh_open_failed + 1
  printf "\n[csyn_zenoh] open_session failed branch reached\n"
  dump_regs
  dump_current_thread
  dump_contexts_lock
  info locals
  bt 10
  printf "\n[diag-stop] Zenoh open returned failure; inspect TX path events and rc/locals above\n"
  dump_summary
  quit
end

break /scratch/Fastdyn/zephyr_rehosting/modules/lib/csyn/zephyr/src/csyn_zenoh.c:180
commands
  silent
  set $diag_events = $diag_events + 1
  set $zenoh_open_success = $zenoh_open_success + 1
  printf "\n[csyn_zenoh] session opened successfully\n"
  dump_regs
  dump_current_thread
  dump_contexts_lock
  bt 10
  printf "\n[diag-stop] Zenoh session opened; next blocker is after network/session setup\n"
  dump_summary
  quit
end

break main
commands
  silent
  set $diag_events = $diag_events + 1
  set $main_entries = $main_entries + 1
  printf "\n[main %u] entered\n", $main_entries
  dump_regs
  dump_current_thread
  dump_contexts_lock
  maybe_stop_on_max_events
  continue
end

break /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/src/main.c:275
commands
  silent
  set $diag_events = $diag_events + 1
  set $control_step_events = $control_step_events + 1
  printf "\n[main] EFMI_STEP reached; control loop has valid mocap and auto mode\n"
  dump_regs
  dump_current_thread
  bt 8
  printf "\n[diag-stop] control step reached\n"
  dump_summary
  quit
end

break /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/src/main.c:278
commands
  silent
  set $idle_output_events = $idle_output_events + 1
  if $idle_output_events <= 10 || ($idle_output_events % 25) == 0
    set $diag_events = $diag_events + 1
    printf "\n[main] idle_output path hit %u time(s); auto_mode true but mocap invalid\n", $idle_output_events
    dump_regs
    dump_current_thread
  end
  if $idle_output_events >= 50 && $zenoh_open_success == 0 && $zenoh_open_failed == 0 && $recv_t_msg_events == 0
    printf "\n[diag-stop] main loop is alive, but Zenoh has not reached receive yet; likely still in send/socket path\n"
    dump_summary
    bt 8
    quit
  end
  if $idle_output_events >= 100 && $zenoh_open_success == 0 && $zenoh_open_failed == 0 && $recv_t_msg_events > 0
    printf "\n[diag-stop] main loop is alive, Zenoh reached receive, but session is still not open; likely waiting for peer/RX frame\n"
    dump_summary
    bt 8
    quit
  end
  maybe_stop_on_max_events
  continue
end

continue
