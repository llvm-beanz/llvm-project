# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20)
project(FeMeWindowsAdapterProbe LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This standalone probe requires 64-bit Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(feme-adapter-rejection-stub SHARED RejectionStub.cpp)
add_executable(feme-windows-adapter-probe AdapterProbe.cpp)
add_executable(feme-buffer-copy-tests BufferCopyTests.cpp)
add_executable(feme-compute-shader-tests ComputeShaderTests.cpp)
add_executable(feme-dxil-validator DxilValidator.cpp)
target_link_libraries(feme-windows-adapter-probe PRIVATE d3d12 dxgi)
set(FEME_PROBE_AGILITY_CORE "" CACHE FILEPATH "Existing Agility D3D12Core.dll")
set(FEME_PROBE_AGILITY_VERSION "" CACHE STRING "Matching D3D12 SDK version")
if(FEME_PROBE_AGILITY_CORE OR FEME_PROBE_AGILITY_VERSION)
  if(NOT EXISTS "${FEME_PROBE_AGILITY_CORE}" OR
     NOT FEME_PROBE_AGILITY_VERSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Supply an existing Agility core and numeric SDK version.")
  endif()
  target_compile_definitions(feme-windows-adapter-probe PRIVATE
    FEME_PROBE_AGILITY_VERSION=${FEME_PROBE_AGILITY_VERSION})
  add_custom_command(TARGET feme-windows-adapter-probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:feme-windows-adapter-probe>/AgilitySDK"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FEME_PROBE_AGILITY_CORE}"
      "$<TARGET_FILE_DIR:feme-windows-adapter-probe>/AgilitySDK/D3D12Core.dll"
    VERBATIM)
endif()
if(MSVC)
  # The WDK DDI headers use anonymous structs/unions.
  target_compile_options(feme-adapter-rejection-stub PRIVATE /W4 /WX /wd4201 /UNDEBUG)
  target_compile_options(feme-windows-adapter-probe PRIVATE /W4 /WX /UNDEBUG)
  target_compile_options(feme-buffer-copy-tests PRIVATE /W4 /WX /UNDEBUG)
  target_compile_options(feme-compute-shader-tests PRIVATE /W4 /WX /UNDEBUG)
  target_compile_options(feme-dxil-validator PRIVATE /W4 /WX /UNDEBUG)
endif()

include(../windows-probe-tests/WindowsProbeTests.cmake)
feme_initialize_probe_tests()
feme_add_probe_test(NAME reference-warp COMMAND feme-windows-adapter-probe)
feme_add_probe_test(NAME software-reference-warp COMMAND feme-windows-adapter-probe
  --software "$ENV{SystemRoot}/System32/d3d10warp.dll")
feme_add_probe_test(NAME rejection-stub COMMAND feme-windows-adapter-probe
  --stub $<TARGET_FILE:feme-adapter-rejection-stub>)
feme_add_probe_test(NAME missing-module WILL_FAIL COMMAND feme-windows-adapter-probe
  --software "${CMAKE_CURRENT_BINARY_DIR}/nonexistent-driver.dll")
feme_add_probe_test(NAME reference-warp-buffer-copy COMMAND "${CMAKE_COMMAND}"
  "-DHARNESS=$<TARGET_FILE:feme-windows-adapter-probe>"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectBufferCopy.cmake")
feme_add_probe_test(NAME software-reference-warp-buffer-copy COMMAND "${CMAKE_COMMAND}"
  "-DHARNESS=$<TARGET_FILE:feme-windows-adapter-probe>"
  "-DMODULE=$ENV{SystemRoot}/System32/d3d10warp.dll"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectBufferCopy.cmake")
foreach(scenario success wrong-size missing-data prefix patch-first patch-last
                 tail-first last-byte patterns)
  feme_add_probe_test(NAME buffer-copy-fixture-${scenario}
    COMMAND feme-buffer-copy-tests "${scenario}")
endforeach()

foreach(scenario success wrong-size missing-data first-byte last-value
                 guard-first guard-last no-dispatch patterns)
  feme_add_probe_test(NAME compute-shader-fixture-${scenario}
    COMMAND feme-compute-shader-tests "${scenario}")
endforeach()
feme_add_probe_test(NAME compute-shader-missing-file WILL_FAIL
  COMMAND feme-windows-adapter-probe --warp-compute-shader
  "${CMAKE_CURRENT_BINARY_DIR}/missing-shader.dxil" 0)
feme_add_probe_test(NAME compute-shader-invalid-space WILL_FAIL
  COMMAND feme-windows-adapter-probe --warp-compute-shader
  "${CMAKE_CURRENT_BINARY_DIR}/missing-shader.dxil" 1)
feme_add_probe_test(NAME dxil-validator-missing-arguments WILL_FAIL
  COMMAND feme-dxil-validator)
feme_add_probe_test(NAME dxil-validator-missing-module WILL_FAIL
  COMMAND feme-dxil-validator "${CMAKE_CURRENT_BINARY_DIR}/missing-sdk"
  "${CMAKE_CURRENT_BINARY_DIR}/missing-shader.dxil")

set(FEME_PROBE_FXC "" CACHE FILEPATH "SDK FXC for original legacy HLSL tests")
set(FEME_PROBE_FEME "" CACHE FILEPATH "FeMe CLI for translated DXIL dispatch tests")
set(FEME_PROBE_DXIL_VALIDATOR_DIR "" CACHE PATH
  "Matching-architecture SDK directory containing dxcompiler.dll and dxil.dll")
if(FEME_PROBE_FEME AND NOT FEME_PROBE_FXC)
  message(FATAL_ERROR "Supply FEME_PROBE_FXC with FEME_PROBE_FEME.")
endif()
if(FEME_PROBE_FEME OR FEME_PROBE_DXIL_VALIDATOR_DIR)
  foreach(module dxcompiler.dll dxil.dll)
    if(NOT EXISTS "${FEME_PROBE_DXIL_VALIDATOR_DIR}/${module}")
      message(FATAL_ERROR "Supply FEME_PROBE_DXIL_VALIDATOR_DIR containing ${module}.")
    endif()
  endforeach()
  feme_add_probe_test(NAME dxil-validator-missing-input WILL_FAIL
    COMMAND feme-dxil-validator "${FEME_PROBE_DXIL_VALIDATOR_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/missing-shader.dxil")
  feme_add_probe_test(NAME dxil-validator-malformed-input WILL_FAIL
    COMMAND feme-dxil-validator "${FEME_PROBE_DXIL_VALIDATOR_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExpectComputeShader.cmake" "%t.dxil")
endif()
foreach(tool FEME_PROBE_FXC FEME_PROBE_FEME)
  if(${tool} AND NOT EXISTS "${${tool}}")
    message(FATAL_ERROR "${tool} must name an existing executable.")
  endif()
endforeach()
if(FEME_PROBE_FXC)
  set(input_kinds dxbc)
  if(FEME_PROBE_FEME)
    list(APPEND input_kinds dxil)
  endif()
  foreach(profile cs_5_0 cs_5_1)
    foreach(kind IN LISTS input_kinds)
      foreach(entry warp software)
        set(module_args)
        if(entry STREQUAL "software")
          list(APPEND module_args "-DMODULE=$ENV{SystemRoot}/System32/d3d10warp.dll")
        endif()
        feme_add_probe_test(NAME compute-shader-${profile}-${kind}-${entry}
          COMMAND "${CMAKE_COMMAND}"
          "-DHARNESS=$<TARGET_FILE:feme-windows-adapter-probe>"
          "-DFXC=${FEME_PROBE_FXC}" "-DFEME=${FEME_PROBE_FEME}"
          "-DVALIDATOR=$<TARGET_FILE:feme-dxil-validator>"
          "-DVALIDATOR_DIR=${FEME_PROBE_DXIL_VALIDATOR_DIR}"
          "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../../test/Translate/DXBC/fxc-legacy-compute.hlsl"
          "-DPROFILE=${profile}" "-DINPUT_KIND=${kind}" "-DOUTPUT_BASE=%t"
          ${module_args}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectComputeShader.cmake")
      endforeach()
    endforeach()
  endforeach()
  if(FEME_PROBE_FEME)
    feme_add_probe_test(NAME compute-shader-unvalidated-dxil
      COMMAND "${CMAKE_COMMAND}"
      "-DHARNESS=$<TARGET_FILE:feme-windows-adapter-probe>"
      "-DFXC=${FEME_PROBE_FXC}" "-DFEME=${FEME_PROBE_FEME}"
      "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../../test/Translate/DXBC/fxc-legacy-compute.hlsl"
      -DPROFILE=cs_5_1 -DINPUT_KIND=dxil "-DOUTPUT_BASE=%t"
      -DSKIP_DXIL_VALIDATION=ON -DEXPECT_PSO_REJECTION=ON
      -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectComputeShader.cmake")
  endif()
  feme_add_probe_test(NAME compute-shader-wrong-root-space
    COMMAND "${CMAKE_COMMAND}"
    "-DHARNESS=$<TARGET_FILE:feme-windows-adapter-probe>"
    "-DFXC=${FEME_PROBE_FXC}"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../../test/Translate/DXBC/fxc-legacy-compute.hlsl"
    -DPROFILE=cs_5_1 -DINPUT_KIND=dxbc "-DOUTPUT_BASE=%t"
    -DSPACE_OVERRIDE=0 -DEXPECT_PSO_REJECTION=ON
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectComputeShader.cmake")
endif()
