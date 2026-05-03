#!/usr/bin/env python3
"""
print_eth_trace.py — parse a raw AFL/fastdyn Ethernet trace file and print
each frame with its nested headers.

The trace format matches extract_requests_ethernet / eth_frame_len in
aflnet/aflnet.c: a flat concatenation of raw Ethernet frames with no
inter-frame delimiters.  Frame boundaries are recovered from:
  - IPv4  (ethertype 0x0800): 14-byte Eth header + IP total-length field
  - ARP   (ethertype 0x0806): fixed 42 bytes (14 Eth + 28 ARP-over-IPv4)
  - anything else            : stop (unknown ethertype, treat rest as opaque)

All parsing is done conservatively: truncated or garbage fields are printed
as raw hex rather than causing an exception.
"""

import struct
import sys
import argparse

# ── constants matching aflnet.c ──────────────────────────────────────────────

ETH_HDR   = 14
ARP_FRAME = 42       # Ethernet(14) + ARP-over-IPv4(28)

ETHERTYPE_IPV4 = 0x0800
ETHERTYPE_ARP  = 0x0806
ETHERTYPE_IPV6 = 0x86DD   # recognised but not decoded

IP_PROTO_ICMP = 1
IP_PROTO_TCP  = 6
IP_PROTO_UDP  = 17

TCP_FLAG_NAMES = [
    (0x01, "FIN"), (0x02, "SYN"), (0x04, "RST"),
    (0x08, "PSH"), (0x10, "ACK"), (0x20, "URG"),
    (0x40, "ECE"), (0x80, "CWR"),
]

# ── small helpers ────────────────────────────────────────────────────────────

def _u8(buf, off):
    return buf[off] if off < len(buf) else None

def _u16be(buf, off):
    if off + 2 > len(buf):
        return None
    return struct.unpack_from(">H", buf, off)[0]

def _u32be(buf, off):
    if off + 4 > len(buf):
        return None
    return struct.unpack_from(">I", buf, off)[0]

def _mac(buf, off):
    if off + 6 > len(buf):
        return "??:??:??:??:??:??"
    return ":".join(f"{b:02x}" for b in buf[off:off+6])

def _ip(buf, off):
    if off + 4 > len(buf):
        return "?.?.?.?"
    return ".".join(str(b) for b in buf[off:off+4])

def _flags_str(flags):
    names = [n for mask, n in TCP_FLAG_NAMES if flags & mask]
    return "|".join(names) if names else "none"

def _hexdump(data, indent="    ", width=16):
    """Compact hex dump, max 3 rows then ellipsis."""
    lines = []
    for i in range(0, min(len(data), width * 3), width):
        chunk = data[i:i+width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{indent}{i:04x}  {hex_part:<{width*3-1}}  {asc_part}")
    if len(data) > width * 3:
        lines.append(f"{indent}... ({len(data)} bytes total)")
    return "\n".join(lines)

# ── frame-length logic (mirrors eth_frame_len in aflnet.c) ──────────────────

def _eth_frame_len(buf, offset):
    """Return byte length of the Ethernet frame at buf[offset], or 0 if malformed."""
    if offset + ETH_HDR > len(buf):
        return 0
    et = _u16be(buf, offset + 12)
    if et == ETHERTYPE_IPV4:
        if offset + ETH_HDR + 4 > len(buf):
            return 0
        ip_total = _u16be(buf, offset + ETH_HDR + 2)
        if ip_total is None or ip_total < 20:
            return 0
        total = ETH_HDR + ip_total
        if offset + total > len(buf):
            return 0
        return total
    elif et == ETHERTYPE_ARP:
        if offset + ARP_FRAME > len(buf):
            return 0
        return ARP_FRAME
    return 0

# ── protocol decoders ────────────────────────────────────────────────────────

def _decode_tcp(buf, base, indent="      "):
    """Decode TCP header starting at buf[base]. Conservative."""
    out = []
    if base + 20 > len(buf):
        out.append(f"{indent}[TCP] truncated ({len(buf)-base} bytes available)")
        return out

    src_port  = _u16be(buf, base)
    dst_port  = _u16be(buf, base + 2)
    seq       = _u32be(buf, base + 4)
    ack       = _u32be(buf, base + 8)
    data_off  = (_u8(buf, base + 12) or 0) >> 4   # high nibble, in 32-bit words
    flags     = _u8(buf, base + 13) or 0
    window    = _u16be(buf, base + 14)
    checksum  = _u16be(buf, base + 16)
    urgent    = _u16be(buf, base + 18)

    hdr_bytes = data_off * 4
    payload_start = base + hdr_bytes
    payload_len   = max(0, len(buf) - payload_start)

    out.append(f"{indent}[TCP]  {src_port} -> {dst_port}  flags={flags:#04x} ({_flags_str(flags)})")
    out.append(f"{indent}       seq={seq}  ack={ack}  win={window}  cksum={checksum:#06x}  urg={urgent}")
    out.append(f"{indent}       hdr={hdr_bytes}B  payload={payload_len}B")
    if payload_len > 0:
        out.append(_hexdump(buf[payload_start:payload_start+64], indent=indent+"  "))
    return out


def _decode_udp(buf, base, indent="      "):
    out = []
    if base + 8 > len(buf):
        out.append(f"{indent}[UDP] truncated")
        return out
    src_port = _u16be(buf, base)
    dst_port = _u16be(buf, base + 2)
    length   = _u16be(buf, base + 4)
    checksum = _u16be(buf, base + 6)
    payload_len = max(0, (length or 8) - 8)
    out.append(f"{indent}[UDP]  {src_port} -> {dst_port}  len={length}  cksum={checksum:#06x}")
    if payload_len > 0:
        out.append(_hexdump(buf[base+8:base+8+min(payload_len,64)], indent=indent+"  "))
    return out


def _decode_icmp(buf, base, indent="      "):
    out = []
    if base + 4 > len(buf):
        out.append(f"{indent}[ICMP] truncated")
        return out
    typ  = _u8(buf, base)
    code = _u8(buf, base + 1)
    cksum = _u16be(buf, base + 2)
    type_names = {0: "Echo Reply", 3: "Dest Unreach", 8: "Echo Request",
                  11: "Time Exceeded", 12: "Param Problem"}
    tname = type_names.get(typ, f"type={typ}")
    out.append(f"{indent}[ICMP] {tname}  code={code}  cksum={cksum:#06x}")
    return out


def _decode_ipv4(buf, base, indent="    "):
    """Decode IPv4 header and its payload protocol."""
    out = []
    if base + 20 > len(buf):
        out.append(f"{indent}[IPv4] truncated ({len(buf)-base} bytes available)")
        return out

    version_ihl = _u8(buf, base)
    version = (version_ihl or 0) >> 4
    ihl     = ((version_ihl or 0) & 0x0F) * 4

    if ihl < 20:
        out.append(f"{indent}[IPv4] bad IHL={ihl} (raw byte={version_ihl:#04x}) - skipping payload")
        return out

    tos          = _u8(buf, base + 1)
    total_len    = _u16be(buf, base + 2)
    ident        = _u16be(buf, base + 4)
    flags_frag   = _u16be(buf, base + 6)
    frag_flags   = (flags_frag or 0) >> 13
    frag_off     = (flags_frag or 0) & 0x1FFF
    ttl          = _u8(buf, base + 8)
    proto        = _u8(buf, base + 9)
    header_cksum = _u16be(buf, base + 10)
    src_ip       = _ip(buf, base + 12)
    dst_ip       = _ip(buf, base + 16)

    proto_names  = {IP_PROTO_ICMP: "ICMP", IP_PROTO_TCP: "TCP", IP_PROTO_UDP: "UDP"}
    proto_str    = proto_names.get(proto, f"proto={proto}")
    df = "DF " if frag_flags & 0x2 else ""
    mf = "MF " if frag_flags & 0x1 else ""

    out.append(f"{indent}[IPv4]  v={version}  ihl={ihl}B  tos={tos:#04x}  total={total_len}")
    out.append(f"{indent}        id={ident:#06x}  {df}{mf}frag_off={frag_off}  ttl={ttl}")
    out.append(f"{indent}        proto={proto_str}  cksum={header_cksum:#06x}")
    out.append(f"{indent}        src={src_ip}  dst={dst_ip}")

    payload_base = base + ihl
    if payload_base >= len(buf):
        out.append(f"{indent}        [no payload in buffer]")
        return out

    if proto == IP_PROTO_TCP:
        out.extend(_decode_tcp(buf, payload_base, indent=indent+"  "))
    elif proto == IP_PROTO_UDP:
        out.extend(_decode_udp(buf, payload_base, indent=indent+"  "))
    elif proto == IP_PROTO_ICMP:
        out.extend(_decode_icmp(buf, payload_base, indent=indent+"  "))
    else:
        rem = len(buf) - payload_base
        out.append(f"{indent}  [unknown proto={proto}  {rem}B payload]")
        out.append(_hexdump(buf[payload_base:payload_base+32], indent=indent+"    "))

    return out


def _decode_arp(buf, base, indent="    "):
    """Decode 28-byte ARP-over-Ethernet/IPv4 payload at buf[base]."""
    out = []
    if base + 28 > len(buf):
        out.append(f"{indent}[ARP] truncated ({len(buf)-base} bytes available, need 28)")
        return out

    htype  = _u16be(buf, base)
    ptype  = _u16be(buf, base + 2)
    hlen   = _u8(buf, base + 4)
    plen   = _u8(buf, base + 5)
    opcode = _u16be(buf, base + 6)

    op_names = {1: "REQUEST", 2: "REPLY", 3: "RARP-REQUEST", 4: "RARP-REPLY"}
    op_str   = op_names.get(opcode, f"op={opcode}")

    # Only attempt MAC/IP fields if sizes match standard Ethernet/IPv4
    standard = (htype == 1 and ptype == ETHERTYPE_IPV4 and hlen == 6 and plen == 4)

    out.append(f"{indent}[ARP]  {op_str}  htype={htype}  ptype={ptype:#06x}  hlen={hlen}  plen={plen}")

    if standard:
        sha = _mac(buf, base + 8)
        spa = _ip(buf,  base + 14)
        tha = _mac(buf, base + 18)
        tpa = _ip(buf,  base + 24)
        out.append(f"{indent}       sender: {sha}  {spa}")
        out.append(f"{indent}       target: {tha}  {tpa}")
    else:
        out.append(f"{indent}       [non-standard ARP sizes — raw payload]")
        out.append(_hexdump(buf[base+8:base+28], indent=indent+"  "))

    return out


def _decode_eth_frame(buf, frame_idx, offset, flen):
    """Decode one complete Ethernet frame starting at buf[offset]."""
    lines = []
    frame_data = buf[offset:offset+flen]

    dst_mac   = _mac(buf, offset)
    src_mac   = _mac(buf, offset + 6)
    ethertype = _u16be(buf, offset + 12)

    et_names  = {ETHERTYPE_IPV4: "IPv4", ETHERTYPE_ARP: "ARP",
                 ETHERTYPE_IPV6: "IPv6"}
    et_str    = et_names.get(ethertype, f"{ethertype:#06x}")

    lines.append(f"Frame {frame_idx:>3}  offset={offset:#010x}  len={flen}B  ethertype={et_str}")
    lines.append(f"  [Eth]  dst={dst_mac}  src={src_mac}")

    payload_base = offset + ETH_HDR

    if ethertype == ETHERTYPE_IPV4:
        lines.extend(_decode_ipv4(buf, payload_base))
    elif ethertype == ETHERTYPE_ARP:
        lines.extend(_decode_arp(buf, payload_base))
    elif ethertype == ETHERTYPE_IPV6:
        lines.append("    [IPv6] (not decoded)")
        lines.append(_hexdump(buf[payload_base:payload_base+40]))
    else:
        lines.append(f"    [unknown ethertype {ethertype:#06x}]")
        lines.append(_hexdump(buf[payload_base:payload_base+32]))

    return lines


# ── main ─────────────────────────────────────────────────────────────────────

def parse_trace(buf, verbose=False):
    offset = 0
    frame_idx = 0
    total = len(buf)

    while offset < total:
        flen = _eth_frame_len(buf, offset)

        if flen == 0:
            remaining = total - offset
            print(f"\n[!] Could not parse frame at offset {offset:#010x} "
                  f"({remaining}B remaining) — stopping")
            if verbose and remaining > 0:
                print(_hexdump(buf[offset:offset+min(remaining, 64)], indent="    "))
            break

        lines = _decode_eth_frame(buf, frame_idx, offset, flen)
        print("\n".join(lines))
        print()

        offset += flen
        frame_idx += 1

    print(f"--- {frame_idx} frame(s) parsed, {total}B total ---")


def main():
    parser = argparse.ArgumentParser(
        description="Print Ethernet frames from a raw AFL/fastdyn trace file.")
    parser.add_argument("trace", help="Raw trace file (binary)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Dump trailing garbage bytes on parse failure")
    args = parser.parse_args()

    try:
        with open(args.trace, "rb") as f:
            buf = f.read()
    except OSError as e:
        print(f"Error reading {args.trace}: {e}", file=sys.stderr)
        sys.exit(1)

    if len(buf) == 0:
        print("(empty file)")
        sys.exit(0)

    parse_trace(buf, verbose=args.verbose)


if __name__ == "__main__":
    main()
