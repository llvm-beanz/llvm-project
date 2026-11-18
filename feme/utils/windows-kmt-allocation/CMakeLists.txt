# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.29)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
  "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
project(FeMeWindowsKmtAllocation LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC)
  message(FATAL_ERROR "This diagnostic requires 64-bit Windows and MSVC.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(feme-windows-kmt-allocation AllocationProbe.cpp)
add_executable(feme-windows-kmt-allocation-tests AllocationTests.cpp)
foreach(target feme-windows-kmt-allocation feme-windows-kmt-allocation-tests)
  target_link_libraries(${target} PRIVATE dxgi gdi32)
  target_compile_options(${target} PRIVATE /W4 /WX /wd4201 /UNDEBUG)
endforeach()

include(../windows-probe-tests/WindowsProbeTests.cmake)
feme_initialize_probe_tests()
feme_add_probe_test(NAME public-kmt-allocation-residency
  COMMAND feme-windows-kmt-allocation)
foreach(scenario
    success pending open-failure open-status missing-adapter
    device-failure missing-device allocation-failure missing-resource
    missing-allocation queue-failure missing-queue missing-sync missing-fence
    resident-failure resident-status incomplete-resident
    wait-failure wait-status wait-timeout wait-failed evict-failure
    destroy-allocation-failure destroy-allocation-status
    destroy-queue-failure destroy-queue-status
    destroy-device-failure destroy-device-status
    close-adapter-failure close-adapter-status)
  feme_add_probe_test(NAME kmt-fixture-${scenario}
    COMMAND feme-windows-kmt-allocation-tests ${scenario})
endforeach()
