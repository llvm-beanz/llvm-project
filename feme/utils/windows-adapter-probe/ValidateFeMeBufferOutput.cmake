# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

file(STRINGS "${OUTPUT_BASE}.txt" dispatches)
list(LENGTH dispatches dispatch_count)
if(NOT dispatch_count EQUAL 3)
  message(FATAL_ERROR "Expected three complete retained-dispatch dumps, got ${dispatch_count}.")
endif()
set(dispatch_index 0)
foreach(dump IN LISTS dispatches)
  math(EXPR dispatch_index "${dispatch_index} + 1")
  file(WRITE "${OUTPUT_BASE}.dispatch-${dispatch_index}.txt" "${dump}\n")
  execute_process(COMMAND "${ORACLE}" --output
    "${OUTPUT_BASE}.dispatch-${dispatch_index}.txt" "${REGISTER_SPACE}" "${ORACLE_ARGUMENT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
  string(FIND "${output}" "${expected}" found)
  if(NOT result STREQUAL "0" OR found EQUAL -1)
    message(FATAL_ERROR "FeMe CPU oracle dispatch ${dispatch_index} failed (${result}):\n${output}\n${error}")
  endif()
  message(STATUS "Input=${INPUT_KIND} retained CPU dispatch ${dispatch_index}/3 (not a D3D12 device):\n${output}")
endforeach()
