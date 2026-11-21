# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

foreach(tool FXC FEME_RUN ORACLE SOURCE)
  if(NOT EXISTS "${${tool}}")
    message(FATAL_ERROR "Supply an existing ${tool}.")
  endif()
endforeach()
if(NOT DEFINED OUTPUT_BASE OR
   NOT PROFILE MATCHES "^cs_5_[01]$" OR
   NOT REGISTER_SPACE MATCHES "^(0|7)$" OR
   NOT WAVE_SIZE MATCHES "^(4|8)$" OR
   NOT WORKLOAD MATCHES "^(raw|grouped)$")
  message(FATAL_ERROR "Supply OUTPUT_BASE, PROFILE=cs_5_0/cs_5_1, REGISTER_SPACE=0/7, WAVE_SIZE=4/8 and WORKLOAD=raw/grouped.")
endif()
if(PROFILE STREQUAL "cs_5_0" AND NOT REGISTER_SPACE STREQUAL "0")
  message(FATAL_ERROR "SM5.0 does not support nonzero register spaces.")
endif()
set(use_space 0)
if(REGISTER_SPACE STREQUAL "7")
  set(use_space 1)
endif()
execute_process(COMMAND "${FXC}" /nologo /WX /T "${PROFILE}" /E main
  /D "USE_REGISTER_SPACE=${use_space}" /Fo "${OUTPUT_BASE}.dxbc" "${SOURCE}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "FXC failed (${result}):\n${output}\n${error}")
endif()
set(shader "${OUTPUT_BASE}.dxbc")
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
elseif(NOT INPUT_KIND STREQUAL "dxbc")
  message(FATAL_ERROR "Expected INPUT_KIND=dxbc or dxil.")
endif()
execute_process(COMMAND "${ORACLE}" --heap "${REGISTER_SPACE}"
  RESULT_VARIABLE result OUTPUT_FILE "${OUTPUT_BASE}.yaml"
  ERROR_VARIABLE error TIMEOUT 5)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Heap generation failed (${result}):\n${error}")
endif()
set(groups "32,1,1")
set(values 256)
set(threads 8)
set(guards 64512)
if(WORKLOAD STREQUAL "grouped")
  set(groups "2,2,2")
  set(values 512)
  set(threads 64)
  set(guards 63488)
endif()
execute_process(COMMAND "${FEME_RUN}" "--wave-size=${WAVE_SIZE}"
  --repeat=3 "--groups=${groups}" "--heap=${OUTPUT_BASE}.yaml" "${shader}"
  RESULT_VARIABLE result OUTPUT_FILE "${OUTPUT_BASE}.txt"
  ERROR_VARIABLE error TIMEOUT 20)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "FeMe legacy CPU execution failed (${result}):\n${error}")
endif()
set(expected "FeMe CPU compute verified: workload=${WORKLOAD} values=${values} bytes=65536 groups=${groups} threads=${threads} space=${REGISTER_SPACE} guard-bytes=${guards}")
set(ORACLE_ARGUMENT "${WORKLOAD}")
include("${CMAKE_CURRENT_LIST_DIR}/ValidateFeMeBufferOutput.cmake")
