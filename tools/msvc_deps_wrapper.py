#!/usr/bin/env python3
"""Normalize MSVC /showIncludes lines to a locale-independent ASCII prefix.

MSVC localizes the prefix and encodes it using the active console code page:

    chcp 936   -> "注意: 包含文件:" in GBK bytes
    chcp 65001 -> "注意: 包含文件:" in UTF-8 bytes

Ninja only remembers one msvc_deps_prefix. Qt Creator and terminals can use
different code pages, which silently breaks header dependency tracking.
This wrapper is used via CMAKE_CXX_COMPILER_LAUNCHER; it captures the compiler
output, rewrites the prefix to ASCII, and forwards the exit code.
"""

from __future__ import annotations

import subprocess
import sys

ASCII_PREFIX = b"Note: including file:"
PREFIXES = (
    "注意: 包含文件:".encode("utf-8"),
    "注意: 包含文件:".encode("gbk"),
    ASCII_PREFIX,
)


def normalize(data: bytes) -> bytes:
    out = bytearray()
    for line in data.splitlines(keepends=True):
        for prefix in PREFIXES:
            if line.startswith(prefix):
                line = ASCII_PREFIX + line[len(prefix):]
                break
        out += line
    return bytes(out)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: msvc_deps_wrapper.py <compiler> [args...]", file=sys.stderr)
        return 2

    result = subprocess.run(
        sys.argv[1:],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    sys.stdout.buffer.write(normalize(result.stdout))
    sys.stdout.buffer.flush()
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
