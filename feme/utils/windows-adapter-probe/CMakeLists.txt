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
endif()

enable_testing()
add_test(NAME reference-warp COMMAND feme-windows-adapter-probe)
add_test(NAME software-reference-warp COMMAND feme-windows-adapter-probe
  --software "$ENV{SystemRoot}/System32/d3d10warp.dll")
add_test(NAME rejection-stub COMMAND feme-windows-adapter-probe
  --stub $<TARGET_FILE:feme-adapter-rejection-stub>)
add_test(NAME missing-module COMMAND feme-windows-adapter-probe
  --software "${CMAKE_CURRENT_BINARY_DIR}/nonexistent-driver.dll")
set_tests_properties(missing-module PROPERTIES WILL_FAIL TRUE)
set_tests_properties(reference-warp software-reference-warp rejection-stub
  missing-module PROPERTIES TIMEOUT 30)
