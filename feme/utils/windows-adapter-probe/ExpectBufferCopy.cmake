# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(NOT EXISTS "${HARNESS}")
  message(FATAL_ERROR "Supply an existing HARNESS.")
endif()
set(args --warp-buffer-copy)
if(DEFINED MODULE)
  set(args --software-buffer-copy "${MODULE}")
endif()
execute_process(COMMAND "${HARNESS}" ${args}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  TIMEOUT 20)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Buffer-copy probe failed (${result}):\n${output}\n${error}")
endif()
set(expected
  "Buffer copy verified: bytes=65536 prefix=257 patch=60013 tail=5266; source-offset=113 target-offset=257")
string(FIND "${output}" "${expected}" found)
if(found EQUAL -1)
  message(FATAL_ERROR
    "Missing byte-exact buffer-copy completion:\n${output}\n${error}")
endif()
message(STATUS "${output}")
