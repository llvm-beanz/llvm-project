# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20)
project(FeMeWindowsAdapterBootstrap LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This standalone probe requires 64-bit Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(feme-adapter-bootstrap-stub SHARED BootstrapStub.cpp)
add_executable(feme-windows-adapter-direct-harness DirectHarness.cpp)
if(MSVC)
  # The WDK DDI headers use anonymous structs/unions.
  target_compile_options(feme-adapter-bootstrap-stub PRIVATE /W4 /WX /wd4201 /UNDEBUG)
  target_compile_options(feme-windows-adapter-direct-harness PRIVATE /W4 /WX /wd4201 /UNDEBUG)
endif()

include(../windows-probe-tests/WindowsProbeTests.cmake)
feme_initialize_probe_tests()
feme_add_probe_test(NAME direct-openadapter12 COMMAND feme-windows-adapter-direct-harness
  $<TARGET_FILE:feme-adapter-bootstrap-stub>)

if(MSVC)
  add_executable(feme-paging-queue-tests BootstrapStub.cpp PagingQueueTests.cpp)
  target_compile_definitions(feme-paging-queue-tests PRIVATE
    FEME_BOOTSTRAP_PROBE_PAGING_QUEUE=1)
  target_compile_options(feme-paging-queue-tests PRIVATE /W4 /WX /wd4201 /UNDEBUG)
  foreach(SCENARIO success create-failure destroy-failure missing-create
                   missing-destroy missing-device missing-table missing-queue
                   missing-sync missing-fence create-exception destroy-exception
                   create-status destroy-status)
    feme_add_probe_test(NAME "paging-queue-fixture-${SCENARIO}"
      COMMAND feme-paging-queue-tests "${SCENARIO}")
  endforeach()
endif()
