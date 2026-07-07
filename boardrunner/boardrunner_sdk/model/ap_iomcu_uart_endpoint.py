#!/usr/bin/env python3
# Host-side python endpoint: ap_iomcu_uart_endpoint.py
import errno
import os
import selectors
import struct
import sys
import termios
import time
import tty

ENV_CANDIDATES = (
    "AP_IOMCU_UART_PTY",
    "UART_PTY",
    "PTY_PATH",
    "SERIAL_PATH",
)

CODE_READ = 0
CODE_WRITE = 1
CODE_SUCCESS = 0

PAGE_CONFIG = 0
PAGE_STATUS = 1
PAGE_ACTUATORS = 2
PAGE_SERVOS = 3
PAGE_RAW_RC_INPUT = 4
PAGE_RC_INPUT = 5
PAGE_RAW_ADC_INPUT = 6
PAGE_PWM_INFO = 7
PAGE_SETUP = 50
PAGE_DIRECT_PWM = 54

BOOTSTRAP_SEQUENCE = (
    (PAGE_CONFIG, 0, 10),
    (PAGE_STATUS, 0, 5),
    (PAGE_SETUP, 0, 4),
    (PAGE_RAW_RC_INPUT, 0, 8),
    (PAGE_RC_INPUT, 0, 8),
    (PAGE_PWM_INFO, 0, 8),
)
BOOTSTRAP_INITIAL_REPEATS = 0
BOOTSTRAP_INTERVAL_S = 0.005
SELECT_TIMEOUT_S = 0.005
MAX_PENDING_TX = 4096

crc8_table = [
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31,
    0x24, 0x23, 0x2a, 0x2d, 0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
    0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d, 0xe0, 0xe7, 0xee, 0xe9,
    0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1,
    0xb4, 0xb3, 0xba, 0xbd, 0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
    0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea, 0xb7, 0xb0, 0xb9, 0xbe,
    0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16,
    0x03, 0x04, 0x0d, 0x0a, 0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
    0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a, 0x89, 0x8e, 0x87, 0x80,
    0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8,
    0xdd, 0xda, 0xd3, 0xd4, 0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
    0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44, 0x19, 0x1e, 0x17, 0x10,
    0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f,
    0x6a, 0x6d, 0x64, 0x63, 0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
    0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13, 0xae, 0xa9, 0xa0, 0xa7,
    0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef,
    0xfa, 0xfd, 0xf4, 0xf3
]


def resolve_path():
    if len(sys.argv) > 1 and sys.argv[1]:
        return sys.argv[1]
    for key in ENV_CANDIDATES:
        val = os.environ.get(key)
        if val:
            return val
    return None


def crc_crc8(data):
    crc = 0x00
    for byte in data:
        i = (crc ^ byte) & 0xFF
        crc = (crc8_table[i] ^ (crc << 8)) & 0xFF
    return crc & 0xFF


def set_raw_mode(fd):
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        pass


def make_count_code(code, count):
    return ((code & 0x03) << 6) | (count & 0x3F)


def packet_crc_ok(frame):
    tmp = bytearray(frame)
    recv_crc = tmp[1]
    tmp[1] = 0
    calc_crc = crc_crc8(tmp)
    if calc_crc != recv_crc:
        return False
    return True


def build_read_reply(page, offset, regs):
    count = len(regs)
    reply = bytearray(4 + count * 2)
    reply[0] = make_count_code(CODE_SUCCESS, count)
    reply[1] = 0
    reply[2] = page & 0xFF
    reply[3] = offset & 0xFF
    if count:
        struct.pack_into("<%dH" % count, reply, 4, *regs)
    reply[1] = crc_crc8(reply)
    return reply


def build_write_ack(page, offset):
    reply = bytearray(4)
    reply[0] = make_count_code(CODE_SUCCESS, 0)
    reply[1] = 0
    reply[2] = page & 0xFF
    reply[3] = offset & 0xFF
    reply[1] = crc_crc8(reply)
    return reply


class MockIOMCU:
    def __init__(self):
        self.pages = {}
        self.seq = 0
        self.status_flags = 0x0001
        self.setup_flags = 0x0000
        self.bootstrap_index = 0
        self._init_defaults()

    def _page(self, page):
        if page not in self.pages:
            self.pages[page] = [0] * 256
        return self.pages[page]

    def _set_u32(self, regs, idx, value):
        regs[idx] = value & 0xFFFF
        regs[idx + 1] = (value >> 16) & 0xFFFF

    def _init_defaults(self):
        cfg = self._page(PAGE_CONFIG)
        cfg[0] = 4
        cfg[1] = 10
        self._set_u32(cfg, 2, 0x12345678)
        self._set_u32(cfg, 4, 0x9ABCDEF0)
        cfg[6] = 32
        cfg[7] = 8
        cfg[8] = 8
        cfg[9] = 4

        status = self._page(PAGE_STATUS)
        status[0] = self.status_flags
        status[1] = 0
        status[2] = 0
        status[3] = 5000
        status[4] = 100

        setup = self._page(PAGE_SETUP)
        setup[0] = self.setup_flags
        setup[1] = 0
        setup[2] = 0
        setup[3] = 0
        try:
            crc_val = int(os.environ.get("IOMCU_CRC", "0xBB9E65BD"), 0)
        except ValueError:
            crc_val = 0xBB9E65BD
        self._set_u32(setup, 11, crc_val)

        pwm = self._page(PAGE_PWM_INFO)
        servos = self._page(PAGE_SERVOS)
        actuators = self._page(PAGE_ACTUATORS)
        direct_pwm = self._page(PAGE_DIRECT_PWM)
        for i in range(16):
            pwm[i] = 1500
            servos[i] = 1500
            actuators[i] = 1500
            direct_pwm[i] = 1500

        raw_adc = self._page(PAGE_RAW_ADC_INPUT)
        raw_adc[0] = 5000
        raw_adc[1] = 5000

        raw_rc = self._page(PAGE_RAW_RC_INPUT)
        rc = self._page(PAGE_RC_INPUT)
        for i in range(8):
            raw_rc[i] = 1500
            rc[i] = 1500

    def _refresh_dynamic_state(self):
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        status = self._page(PAGE_STATUS)
        status[0] = self.status_flags
        status[1] = self.seq & 0xFFFF
        status[2] = (self.seq >> 16) & 0xFFFF
        status[3] = 5000
        status[4] = 100

        setup = self._page(PAGE_SETUP)
        setup[0] = self.setup_flags

    def read_regs(self, page, offset, count):
        self._refresh_dynamic_state()
        regs = self._page(page)
        end = min(offset + count, len(regs))
        out = list(regs[offset:end])
        if len(out) < count:
            out.extend([0] * (count - len(out)))
        return out

    def write_regs(self, page, offset, values):
        regs = self._page(page)
        for i, value in enumerate(values):
            idx = offset + i
            if 0 <= idx < len(regs):
                regs[idx] = value & 0xFFFF

        if page in (PAGE_ACTUATORS, PAGE_DIRECT_PWM, PAGE_SERVOS):
            servos = self._page(PAGE_SERVOS)
            pwm = self._page(PAGE_PWM_INFO)
            for i, value in enumerate(values):
                idx = offset + i
                if 0 <= idx < len(servos):
                    servos[idx] = value & 0xFFFF
                if 0 <= idx < len(pwm):
                    pwm[idx] = value & 0xFFFF

        if page == PAGE_SETUP and offset == 0 and values:
            self.setup_flags = values[0] & 0xFFFF

        if page == PAGE_SETUP:
            status = self._page(PAGE_STATUS)
            status[0] = self.status_flags

    def bootstrap_frame(self):
        page, offset, count = BOOTSTRAP_SEQUENCE[
            self.bootstrap_index % len(BOOTSTRAP_SEQUENCE)
        ]
        self.bootstrap_index += 1
        print(f"[IOMCU Mock] Received Read Request for PAGE {page} (Offset {offset}, Count {count})", flush=True)
        if page == PAGE_SETUP:
            print("[IOMCU Mock] SUCCESS: The firmware advanced past the initialization timeout and requested PAGE_SETUP!", flush=True)
        return build_read_reply(page, offset, self.read_regs(page, offset, count))

    def bootstrap_burst(self, repeats=1):
        burst = bytearray()
        repeats = int(repeats)
        if repeats <= 0:
            return burst
        total = repeats * len(BOOTSTRAP_SEQUENCE)
        for _ in range(total):
            burst.extend(self.bootstrap_frame())
        return burst


def open_endpoint(path):
    use_existing = os.environ.get("AP_IOMCU_UART_USE_EXISTING_PTY", "").lower() in (
        "1",
        "true",
        "yes",
    )

    if path and os.path.exists(path) and use_existing:
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        set_raw_mode(fd)
        resolved = path
        try:
            resolved = os.path.realpath(path)
        except Exception:
            pass
        sys.stderr.write(
            "ap_iomcu_uart_endpoint: Mock started on existing %s%s\n"
            % (
                path,
                "" if resolved == path else " -> %s" % resolved,
            )
        )
        return fd

    if path and os.path.exists(path):
        try:
            os.remove(path)
        except OSError:
            pass

    master, slave = os.openpty()
    tty_name = os.ttyname(slave)
    set_raw_mode(slave)
    link_path = path if path else "/tmp/usart6_pty"
    try:
        if os.path.lexists(link_path):
            os.remove(link_path)
        os.symlink(tty_name, link_path)
    except Exception as e:
        with open("/tmp/endpoint.log", "a") as f: f.write("Failed to symlink %s: %s\n" % (link_path, e))
    os.close(slave)
    with open("/tmp/endpoint.log", "a") as f: f.write(
        "ap_iomcu_uart_endpoint: Mock created PTY at %s (symlinked to %s)\n"
        % (tty_name, link_path)
    )
    return master


def handle_frame(iomcu, frame):
    with open("/tmp/endpoint.log", "a") as f: f.write(f"RX Frame: code={frame[0]>>6} count={frame[0]&0x3F} page={frame[2]} offset={frame[3]}\n")
    count_code = frame[0]
    count = count_code & 0x3F
    code = count_code >> 6
    page = frame[2]
    offset = frame[3]

    if code == CODE_READ:
        return build_read_reply(page, offset, iomcu.read_regs(page, offset, count))

    if code == CODE_WRITE:
        values = []
        if count:
            values = list(struct.unpack("<%dH" % count, frame[4:4 + count * 2]))
        iomcu.write_regs(page, offset, values)
        return build_write_ack(page, offset)

    return build_read_reply(page, offset, [0] * count)


def flush_pending(fd, pending):
    if not pending:
        return
    try:
        n = os.write(fd, pending)
        with open("/tmp/endpoint_write.log", "a") as f:
            f.write(f"Wrote {n} bytes to pty\n")
        del pending[:n]
    except BlockingIOError:
        pass
    except Exception as e:
        print(f"Failed to write to pty: {e}")


def update_selector(sel, fd, pending_tx):
    mask = selectors.EVENT_READ
    if pending_tx:
        mask |= selectors.EVENT_WRITE
    sel.modify(fd, mask, "pty")


def main():
    path = resolve_path()
    fd = open_endpoint(path)

    sel = selectors.DefaultSelector()
    sel.register(fd, selectors.EVENT_READ, "pty")
    sys.stderr.flush()

    iomcu = MockIOMCU()
    buf = bytearray()
    pending_tx = bytearray()
    saw_valid_request = False
    next_bootstrap_at = time.monotonic()

    # Do not send any unsolicited bootstrap frames. Sit in silence
    # and strictly wait for the firmware to initiate communication.
    pending_tx.clear()
    update_selector(sel, fd, pending_tx)
    next_bootstrap_at = time.monotonic() + BOOTSTRAP_INTERVAL_S

    try:
        while True:
            if saw_valid_request:
                timeout = SELECT_TIMEOUT_S
            else:
                timeout = max(
                    0.0,
                    min(SELECT_TIMEOUT_S, next_bootstrap_at - time.monotonic()),
                )

            events = sel.select(timeout=timeout)
            for key, event_mask in events:
                if key.data != "pty":
                    continue

                if event_mask & selectors.EVENT_READ:
                    try:
                        data = os.read(fd, 1024)
                    except BlockingIOError:
                        data = b""
                    except OSError as e:
                        if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EIO):
                            data = b""
                        else:
                            raise
                    if data:
                        buf.extend(data)

                if event_mask & selectors.EVENT_WRITE:
                    flush_pending(fd, pending_tx)

            while len(buf) >= 4:
                count_code = buf[0]
                count = count_code & 0x3F
                code = count_code >> 6
                page = buf[2]
                offset = buf[3]

                if count == 0:
                    del buf[0]
                    continue

                if code == CODE_READ:
                    expected_len_1 = 4
                    expected_len_2 = 4 + count * 2

                    # Print the raw buffer here so we can see what the firmware sent!
                    print(f"[IOMCU Mock] Raw RX buf for CODE_READ: {buf[:expected_len_2].hex()} (count={count})", flush=True)

                    if len(buf) >= expected_len_1 and packet_crc_ok(bytes(buf[:expected_len_1])):
                        expected_len = expected_len_1
                    elif len(buf) >= expected_len_2 and packet_crc_ok(bytes(buf[:expected_len_2])):
                        expected_len = expected_len_2
                    else:
                        if len(buf) < expected_len_2:
                            break # Wait for more bytes just in case it's legacy format
                        del buf[0]
                        continue
                else:
                    expected_len = 4 + count * 2
                    if len(buf) < expected_len:
                        break
                    if not packet_crc_ok(bytes(buf[:expected_len])):
                        del buf[0]
                        continue

                frame = bytes(buf[:expected_len])

                del buf[:expected_len]
                saw_valid_request = True
                if code == CODE_READ:
                    print(f"[IOMCU Mock] Received Read Request: PAGE {page}, OFFSET {offset}", flush=True)
                    if page == PAGE_SETUP:
                        print("[IOMCU Mock] SUCCESS: Firmware requested PAGE_SETUP! Initialization succeeded!", flush=True)

                # Once the guest starts actively talking to us, prioritize
                # strict request/response traffic over any unsent bootstrap
                # packets.
                pending_tx.clear()

                reply = handle_frame(iomcu, frame)
                pending_tx.extend(reply)
                flush_pending(fd, pending_tx)

            if (
                not saw_valid_request
                and len(pending_tx) < MAX_PENDING_TX
                and time.monotonic() >= next_bootstrap_at
            ):
                pass # pending_tx.extend(iomcu.bootstrap_frame())
                # flush_pending(fd, pending_tx)
                next_bootstrap_at = time.monotonic() + BOOTSTRAP_INTERVAL_S

            update_selector(sel, fd, pending_tx)
    except KeyboardInterrupt:
        return 0
    finally:
        os.close(fd)

    return 0


if __name__ == '__main__':
    sys.exit(main())
