#!/usr/bin/env python3
"""Encode and decode raw inputs as editable byte arrays."""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path
from typing import Sequence


NUMBER_RE = re.compile(
    r"(?<![A-Za-z0-9_])[-+]?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)(?![A-Za-z0-9_])"
)
ARRAY_TOKEN_RE = re.compile(
    r"(?<![A-Za-z0-9_])[-+]?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+|[0-9a-fA-F]{1,2})(?![A-Za-z0-9_])"
)
BYTE_TOKEN = (
    r"(?:0[xX][0-9a-fA-F]{1,2}|0[bB][01]{1,8}|\d{1,3}|"
    r"[0-9a-fA-F]{1,2})"
)
BYTE_RUN_RE = re.compile(
    rf"(?<![A-Za-z0-9_]){BYTE_TOKEN}"
    rf"(?:\s*(?:,|\s)\s*{BYTE_TOKEN})+(?![A-Za-z0-9_])"
)
COMPACT_HEX_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?:0[xX])?[0-9a-fA-F]{4,}(?![A-Za-z0-9_])"
)


def decode_bytes(raw: bytes, width: int = 12) -> str:
    """Decode raw bytes to a complete editable Python-style byte array."""
    if not raw:
        return "[]\n"

    lines = ["["]
    for offset in range(0, len(raw), width):
        chunk = raw[offset : offset + width]
        suffix = "," if offset + width < len(raw) else ""
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + suffix)
    lines.append("]")
    return "\n".join(lines) + "\n"


def decode(input_filename: str | Path, output_filename: str | Path) -> None:
    """Decode a raw binary file to an editable byte array text file."""
    text = decode_bytes(Path(input_filename).read_bytes())
    Path(output_filename).write_text(text, encoding="utf-8")


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    lines = []
    for line in text.splitlines():
        lines.append(re.split(r"#|//", line, maxsplit=1)[0])
    return "\n".join(lines)


def _strip_code_fences(text: str) -> str:
    return re.sub(r"```(?:[A-Za-z0-9_+-]+)?\s*|\s*```", "", text)


def _find_balanced(text: str, opener: str, closer: str) -> list[str]:
    spans = []
    depth = 0
    start = None

    for index, char in enumerate(text):
        if char == opener:
            if depth == 0:
                start = index
            depth += 1
        elif char == closer and depth > 0:
            depth -= 1
            if depth == 0 and start is not None:
                spans.append(text[start : index + 1])
                start = None

    return spans


def _candidate_arrays(text: str) -> list[tuple[str, bool]]:
    candidates = []
    candidates.extend(_find_balanced(text, "[", "]"))
    candidates.extend(_find_balanced(text, "{", "}"))

    if candidates:
        return [(candidate, True) for candidate in candidates]

    bytes_literal = re.search(
        r"(?:bytes|bytearray)\s*\((?P<body>.*?)\)",
        text,
        flags=re.DOTALL,
    )
    if bytes_literal is not None:
        return [(bytes_literal.group("body"), True)]

    byte_runs = BYTE_RUN_RE.findall(text)
    if byte_runs:
        return [(candidate, True) for candidate in byte_runs] + [(text, False)]

    return [(text, False)]


def _coerce_byte(value: int) -> int:
    return max(0, min(int(value), 0xFF))


def _literal_bytes(candidate: str) -> bytes | None:
    try:
        value = ast.literal_eval(candidate)
    except (SyntaxError, ValueError):
        return None

    if isinstance(value, bytes):
        return value
    if isinstance(value, bytearray):
        return bytes(value)
    if isinstance(value, (list, tuple)):
        parsed = []
        for item in value:
            if not isinstance(item, int):
                return None
            parsed.append(_coerce_byte(item))
        return bytes(parsed)

    return None


def _parse_number(token: str) -> int:
    token = token.strip()
    if token.lower().startswith("0x"):
        return int(token, 16)
    if token.lower().startswith("-0x") or token.lower().startswith("+0x"):
        return int(token, 16)
    if token.lower().startswith("0b"):
        return int(token, 2)
    if token.lower().startswith("-0b") or token.lower().startswith("+0b"):
        sign = -1 if token.startswith("-") else 1
        return sign * int(token[3:], 2)
    if any(ch in "abcdefABCDEF" for ch in token):
        return int(token, 16)
    return int(token, 10)


def _parse_numbers(candidate: str, allow_bare_hex: bool = False) -> bytes:
    pattern = ARRAY_TOKEN_RE if allow_bare_hex else NUMBER_RE
    return bytes(
        _coerce_byte(_parse_number(token))
        for token in pattern.findall(candidate)
    )


def _hex_blob_bytes(blob: str) -> bytes:
    blob = blob.strip()
    if blob.lower().startswith("0x"):
        blob = blob[2:]
    if len(blob) % 2 != 0:
        blob = "0" + blob
    return bytes(
        int(blob[index : index + 2], 16)
        for index in range(0, len(blob), 2)
    )


def _compact_hex_candidates(text: str) -> list[bytes]:
    candidates = [
        _hex_blob_bytes(match.group(0))
        for match in COMPACT_HEX_RE.finditer(text)
    ]

    compact = re.sub(r"[\s,;:_-]+", "", text)
    if re.fullmatch(r"(?:0[xX])?[0-9a-fA-F]{4,}", compact):
        candidates.append(_hex_blob_bytes(compact))

    return candidates


def _parse_escaped_string(candidate: str) -> bytes | None:
    match = re.search(
        r"(?:b|bytes)?\s*(['\"])(?P<body>(?:\\.|(?!\1).)*)\1",
        candidate,
        flags=re.DOTALL,
    )
    if match is None:
        return None

    try:
        value = ast.literal_eval(match.group(0))
    except (SyntaxError, ValueError):
        return None

    if isinstance(value, str):
        return value.encode("latin-1", errors="ignore")
    if isinstance(value, bytes):
        return value
    return None


def encode_text(text: str) -> bytes:
    """Encode a forgiving LLM-produced byte array back to raw bytes."""
    cleaned = _strip_comments(_strip_code_fences(text)).strip()

    best = b""
    for candidate, allow_bare_hex in _candidate_arrays(cleaned):
        parsed = _literal_bytes(candidate)
        if parsed is None:
            escaped = _parse_escaped_string(candidate)
            parsed = (
                escaped
                if escaped is not None
                else _parse_numbers(candidate, allow_bare_hex)
            )

        if len(parsed) > len(best):
            best = parsed

    for parsed in _compact_hex_candidates(cleaned):
        if len(parsed) > len(best):
            best = parsed

    if best:
        return best

    return b""


def encode(input_filename: str | Path, output_filename: str | Path) -> None:
    """Encode an editable byte array text file to raw binary."""
    text = Path(input_filename).read_text(encoding="utf-8", errors="ignore")
    Path(output_filename).write_bytes(encode_text(text))


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert raw bytes and byte arrays.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    decode_parser = subparsers.add_parser("decode", help="raw binary to byte array")
    decode_parser.add_argument("input", type=Path)
    decode_parser.add_argument("output", type=Path, nargs="?")

    encode_parser = subparsers.add_parser("encode", help="byte array to raw binary")
    encode_parser.add_argument("input", type=Path)
    encode_parser.add_argument("output", type=Path)

    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if args.command == "decode":
        text = decode_bytes(args.input.read_bytes())
        if args.output is None:
            sys.stdout.write(text)
        else:
            args.output.write_text(text, encoding="utf-8")
    elif args.command == "encode":
        encode(args.input, args.output)
    else:
        raise AssertionError(f"unhandled command: {args.command}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
