# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import struct
import sys
from pathlib import Path

bitcode = Path(sys.argv[1]).read_bytes()
program = struct.pack(
    "<BBHI4sBBHII",
    0x60,
    0,
    5,
    (28 + len(bitcode)) // 4,
    b"DXIL",
    0,
    1,
    0,
    20,
    len(bitcode),
) + b"\0" * 4 + bitcode
part = struct.pack("<4sI", b"DXIL", len(program)) + program
header = struct.pack("<4s16sHHIII", b"DXBC", bytes(16), 1, 0, 36 + len(part), 1, 36)
Path(sys.argv[2]).write_bytes(header + part)
