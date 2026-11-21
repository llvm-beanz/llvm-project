# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

foreach(tool DXC FEME_RUN ORACLE SOURCE)
  if(NOT EXISTS "${${tool}}")
    message(FATAL_ERROR "Supply an existing ${tool}.")
  endif()
endforeach()
if(NOT DEFINED OUTPUT_BASE OR
   NOT REGISTER_SPACE MATCHES "^(0|7)$" OR
   NOT WAVE_SIZE MATCHES "^(4|8|16|32)$")
  message(FATAL_ERROR "Supply OUTPUT_BASE, REGISTER_SPACE=0/7 and WAVE_SIZE=4/8/16/32.")
endif()
set(use_space 0)
if(REGISTER_SPACE STREQUAL "7")
  set(use_space 1)
endif()
execute_process(COMMAND "${DXC}" /nologo /WX /T cs_6_0 /E main
  /D "USE_REGISTER_SPACE=${use_space}" /Fo "${OUTPUT_BASE}.dxil" "${SOURCE}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "DXC failed (${result}):\n${output}\n${error}")
endif()
set(shader "${OUTPUT_BASE}.dxil")
if(INPUT_KIND STREQUAL "dxil")
  if(NOT EXISTS "${FEME}")
    message(FATAL_ERROR "Supply FEME for re-emitted input.")
  endif()
  execute_process(COMMAND "${FEME}" --target=dxil "${shader}"
    -o "${OUTPUT_BASE}.reemitted.dxil"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "FeMe export failed (${result}):\n${output}\n${error}")
  endif()
  set(shader "${OUTPUT_BASE}.reemitted.dxil")
elseif(NOT INPUT_KIND STREQUAL "dxc")
  message(FATAL_ERROR "Expected INPUT_KIND=dxc or dxil.")
endif()
execute_process(COMMAND "${ORACLE}" --heap "${REGISTER_SPACE}"
  RESULT_VARIABLE result OUTPUT_FILE "${OUTPUT_BASE}.yaml"
  ERROR_VARIABLE error TIMEOUT 5)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Heap generation failed (${result}):\n${error}")
endif()
execute_process(COMMAND "${FEME_RUN}" "--wave-size=${WAVE_SIZE}"
  --repeat=3 --groups=3,2,1 "--heap=${OUTPUT_BASE}.yaml" "${shader}"
  RESULT_VARIABLE result OUTPUT_FILE "${OUTPUT_BASE}.txt"
  ERROR_VARIABLE error TIMEOUT 20)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "FeMe CPU execution failed (${result}):\n${error}")
endif()
math(EXPR waves "6 * ((30 + ${WAVE_SIZE} - 1) / ${WAVE_SIZE})")
set(expected "FeMe CPU partial-wave verified: space=${REGISTER_SPACE} lanes=${WAVE_SIZE} waves=${waves} partial-waves=6 records=180 guard-bytes=59776")
set(ORACLE_ARGUMENT "${WAVE_SIZE}")
include("${CMAKE_CURRENT_LIST_DIR}/ValidateFeMeBufferOutput.cmake")
