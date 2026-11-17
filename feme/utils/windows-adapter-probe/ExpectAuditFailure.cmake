# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

execute_process(
  COMMAND "${HARNESS}" --warp-bootstrap-audit "${SHIM}" "${EXPECTATION}"
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE OUTPUT
  ERROR_VARIABLE ERROR
  TIMEOUT 20)
if(NOT RESULT STREQUAL "1" OR
   NOT OUTPUT MATCHES "Explicit bootstrap expectation failed: 0x80004005")
  message(FATAL_ERROR
    "Expected an explicit audit mismatch, not a load error or crash.\n"
    "Exit: ${RESULT}\n${OUTPUT}${ERROR}")
endif()
