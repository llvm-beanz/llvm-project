# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.29)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
  "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
project(FeMeWarpInterposition LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This diagnostic requires 64-bit Windows.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "Use an MSVC-compatible compiler and PE linker.")
endif()

option(FEME_PROBE_DDI_MATRIX_ONLY
  "Configure only the public DDI-version audit tests" OFF)
set(FEME_PROBE_DDI_VERSIONS "" CACHE STRING
  "Semicolon-separated D3D12DDI_SUPPORTED_00NN macros to audit")
set(FEME_PROBE_DDI_EXPECT_UNSUPPORTED "" CACHE STRING
  "Subset of FEME_PROBE_DDI_VERSIONS expected to fail before CreateDevice")
option(FEME_PROBE_PAGING_QUEUE
  "Exercise public kernel paging-queue create/destroy in the DDI matrix" OFF)
option(FEME_PROBE_GPU_VA
  "Exercise public kernel GPU virtual-address reserve/free in the DDI matrix" OFF)
option(FEME_PROBE_MONITORED_FENCE
  "Exercise public runtime-owned monitored-fence CPU wait/signal in the DDI matrix" OFF)
if(FEME_PROBE_MONITORED_FENCE AND
   (FEME_PROBE_GPU_VA OR FEME_PROBE_PAGING_QUEUE))
  message(FATAL_ERROR
    "Run monitored-fence probing separately from the other callback probes.")
endif()
option(FEME_PROBE_GPU_VA_EXPECT_INVALIDARG
  "Require the measured E_INVALIDARG reservation rejection, not reserve/free success" OFF)
if(FEME_PROBE_GPU_VA_EXPECT_INVALIDARG AND NOT FEME_PROBE_GPU_VA)
  message(FATAL_ERROR
    "FEME_PROBE_GPU_VA_EXPECT_INVALIDARG requires FEME_PROBE_GPU_VA.")
endif()
if(FEME_PROBE_GPU_VA AND FEME_PROBE_PAGING_QUEUE)
  message(FATAL_ERROR
    "Run GPU VA reservation and paging-queue probes separately.")
endif()
if((FEME_PROBE_PAGING_QUEUE OR FEME_PROBE_GPU_VA OR
    FEME_PROBE_MONITORED_FENCE) AND
   NOT FEME_PROBE_DDI_VERSIONS)
  message(FATAL_ERROR
    "Set FEME_PROBE_DDI_VERSIONS when enabling kernel callback probes.")
endif()
if(FEME_PROBE_DDI_MATRIX_ONLY AND NOT FEME_PROBE_DDI_VERSIONS)
  message(FATAL_ERROR
    "Set FEME_PROBE_DDI_VERSIONS when FEME_PROBE_DDI_MATRIX_ONLY is enabled.")
endif()

if(NOT FEME_PROBE_DDI_MATRIX_ONLY)
  set(FEME_WARP_REFERENCE "" CACHE FILEPATH "Existing WARP 1.0.19 x64 DLL")
  if(NOT EXISTS "${FEME_WARP_REFERENCE}")
    message(FATAL_ERROR "Supply FEME_WARP_REFERENCE; no binaries are downloaded.")
  endif()
  file(SHA256 "${FEME_WARP_REFERENCE}" REFERENCE_HASH)
  if(NOT REFERENCE_HASH STREQUAL
     "1fd2f9d991f00d890d06c00f791a756446f5f29da508b12fdeb8ffd1b5696add")
    message(FATAL_ERROR "The reference DLL is not the pinned WARP 1.0.19 x64 binary.")
  endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# PE forwarding preserves the reference functions' ABI without guessing private
# signatures or importing Microsoft's implementation into this project.
include(../windows-probe-tests/WindowsProbeTests.cmake)
feme_initialize_probe_tests()
if(NOT FEME_PROBE_DDI_MATRIX_ONLY)
set(REFERENCE_EXPORTS
  D3D11RefGetLastCreation D3DLayerGetInterface OpenAdapter OpenAdapter10_2
  OpenDisplayAdapter1 QueryDListForApplication1 QueryDListForApplication2
  QueryMuxDListForApplication)
foreach(VARIANT reference public-exports private-ordinal bare negotiation
                pinned-version pinned-version-newer callback-probe)
  set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${VARIANT}")
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")
  if(NOT VARIANT STREQUAL bare AND NOT VARIANT STREQUAL negotiation AND
     NOT VARIANT STREQUAL pinned-version AND
     NOT VARIANT STREQUAL pinned-version-newer AND
     NOT VARIANT STREQUAL callback-probe)
    configure_file("${FEME_WARP_REFERENCE}"
      "${OUTPUT_DIR}/feme_warp_reference.dll" COPYONLY)
  endif()
  if(VARIANT STREQUAL negotiation OR VARIANT STREQUAL pinned-version OR
     VARIANT STREQUAL pinned-version-newer OR VARIANT STREQUAL callback-probe)
    add_library(${VARIANT}-shim SHARED
      ../windows-adapter-bootstrap/BootstrapStub.cpp)
  else()
    add_library(${VARIANT}-shim SHARED Intercept.cpp)
  endif()
  if(VARIANT STREQUAL pinned-version OR VARIANT STREQUAL callback-probe)
    # W0(g)'s audit: pin the earliest public D3D12 UMD DDI baseline
    # (Interface Version R1, Build 10) to keep the negotiated device-funcs
    # surface minimal while observing what the stock runtime asks for.
    target_compile_definitions(${VARIANT}-shim PRIVATE
      FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0010)
  elseif(VARIANT STREQUAL pinned-version-newer)
    # W0(g) control: pin the newest public D3D12 UMD DDI baseline in this
    # WDK to check whether reaching CreateDevice (once the two observed
    # caps are answered) is specific to the oldest version or general.
    target_compile_definitions(${VARIANT}-shim PRIVATE
      FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0119)
  endif()
  if(VARIANT STREQUAL callback-probe)
    # W0(g) follow-up: actually invoke the one pure user-mode, non-allocation
    # thunk callback (pfnSetErrorCb) observed to be non-null, wrapped in SEH
    # so a fault is observed rather than silently tearing down the process.
    target_compile_definitions(${VARIANT}-shim PRIVATE
      FEME_BOOTSTRAP_PROBE_CALLBACK=1)
  endif()
  if(NOT VARIANT STREQUAL bare AND NOT VARIANT STREQUAL negotiation AND
     NOT VARIANT STREQUAL pinned-version AND
     NOT VARIANT STREQUAL pinned-version-newer AND
     NOT VARIANT STREQUAL callback-probe)
    foreach(EXPORT IN LISTS REFERENCE_EXPORTS)
      target_link_options(${VARIANT}-shim PRIVATE
        "/EXPORT:${EXPORT}=feme_warp_reference.${EXPORT}")
    endforeach()
  endif()
  if(VARIANT STREQUAL reference)
    target_link_options(${VARIANT}-shim PRIVATE
      "/EXPORT:OpenAdapter12=feme_warp_reference.OpenAdapter12")
  elseif(NOT VARIANT STREQUAL negotiation AND
         NOT VARIANT STREQUAL pinned-version AND
         NOT VARIANT STREQUAL pinned-version-newer AND
         NOT VARIANT STREQUAL callback-probe)
    target_link_options(${VARIANT}-shim PRIVATE
      "/EXPORT:OpenAdapter12=FeMeInterceptOpenAdapter12")
  endif()
  if(VARIANT STREQUAL reference OR VARIANT STREQUAL private-ordinal)
    target_link_options(${VARIANT}-shim PRIVATE
      "/EXPORT:PrivateBootstrap=feme_warp_reference.#199,@199,NONAME")
  endif()
  set_target_properties(${VARIANT}-shim PROPERTIES
    OUTPUT_NAME d3d10warp RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
  add_executable(${VARIANT}-harness ../windows-adapter-probe/AdapterProbe.cpp)
  set_target_properties(${VARIANT}-harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
  target_link_libraries(${VARIANT}-harness PRIVATE d3d12 dxgi)
  add_dependencies(${VARIANT}-harness ${VARIANT}-shim)
  if(MSVC)
    target_compile_options(${VARIANT}-shim PRIVATE /W4 /WX /wd4201 /UNDEBUG)
    target_compile_options(${VARIANT}-harness PRIVATE /W4 /WX /UNDEBUG)
  endif()
  if(VARIANT STREQUAL reference)
    feme_add_probe_test(NAME reference-forwarding COMMAND ${VARIANT}-harness)
  elseif(VARIANT STREQUAL negotiation)
    feme_add_probe_test(NAME os-routed-negotiation COMMAND ${VARIANT}-harness
      --warp-bootstrap $<TARGET_FILE:${VARIANT}-shim>)
  elseif(VARIANT STREQUAL pinned-version)
    feme_add_probe_test(NAME os-routed-pinned-version COMMAND ${VARIANT}-harness
      --warp-bootstrap-audit $<TARGET_FILE:${VARIANT}-shim>
      --expect-create-device)
  elseif(VARIANT STREQUAL pinned-version-newer)
    feme_add_probe_test(NAME os-routed-pinned-version-newer COMMAND ${VARIANT}-harness
      --warp-bootstrap-audit $<TARGET_FILE:${VARIANT}-shim>)
  elseif(VARIANT STREQUAL callback-probe)
    feme_add_probe_test(NAME os-routed-callback-probe COMMAND ${VARIANT}-harness
      --warp-bootstrap-audit $<TARGET_FILE:${VARIANT}-shim> --expect-callback)
  else()
    feme_add_probe_test(NAME ${VARIANT}-interception COMMAND ${VARIANT}-harness
      --warp-shim $<TARGET_FILE:${VARIANT}-shim>)
  endif()
endforeach()

feme_add_probe_test(NAME generic-software-rejection COMMAND bare-harness
  --stub $<TARGET_FILE:bare-shim>)
endif()

# Allow the VM-first W0(g) version bisect to test an explicit set of public
# WDK DDI versions without editing this project for each trial.
set(SEEN_DDI_VERSIONS)
foreach(DDI_VERSION IN LISTS FEME_PROBE_DDI_EXPECT_UNSUPPORTED)
  if(NOT DDI_VERSION IN_LIST FEME_PROBE_DDI_VERSIONS)
    message(FATAL_ERROR
      "Expected unsupported version '${DDI_VERSION}' is not in "
      "FEME_PROBE_DDI_VERSIONS")
  endif()
endforeach()
foreach(DDI_VERSION IN LISTS FEME_PROBE_DDI_VERSIONS)
  if(NOT DDI_VERSION MATCHES "^D3D12DDI_SUPPORTED_[0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR
      "Invalid DDI version '${DDI_VERSION}'; use a public "
      "D3D12DDI_SUPPORTED_00NN macro from d3d12umddi.h")
  endif()
  if(DDI_VERSION IN_LIST SEEN_DDI_VERSIONS)
    message(FATAL_ERROR "Duplicate DDI version '${DDI_VERSION}'")
  endif()
  list(APPEND SEEN_DDI_VERSIONS "${DDI_VERSION}")
  string(REGEX REPLACE "^D3D12DDI_SUPPORTED_" "" DDI_VERSION_SUFFIX
    "${DDI_VERSION}")
  set(VARIANT "pinned-version-custom-${DDI_VERSION_SUFFIX}")
  set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${VARIANT}")
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")

  add_library(${VARIANT}-shim SHARED
    ../windows-adapter-bootstrap/BootstrapStub.cpp)
  target_compile_definitions(${VARIANT}-shim PRIVATE
    FEME_BOOTSTRAP_PINNED_VERSION=${DDI_VERSION})
  if(FEME_PROBE_PAGING_QUEUE)
    target_compile_definitions(${VARIANT}-shim PRIVATE
      FEME_BOOTSTRAP_PROBE_PAGING_QUEUE=1)
  endif()
  if(FEME_PROBE_GPU_VA)
    target_compile_definitions(${VARIANT}-shim PRIVATE
      FEME_BOOTSTRAP_PROBE_GPU_VA=1)
  endif()
  if(FEME_PROBE_MONITORED_FENCE)
    target_compile_definitions(${VARIANT}-shim PRIVATE
      FEME_BOOTSTRAP_PROBE_MONITORED_FENCE=1)
  endif()
  set_target_properties(${VARIANT}-shim PROPERTIES
    OUTPUT_NAME d3d10warp RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

  add_executable(${VARIANT}-harness ../windows-adapter-probe/AdapterProbe.cpp)
  set_target_properties(${VARIANT}-harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
  target_link_libraries(${VARIANT}-harness PRIVATE d3d12 dxgi)
  add_dependencies(${VARIANT}-harness ${VARIANT}-shim)
  if(MSVC)
    target_compile_options(${VARIANT}-shim PRIVATE /W4 /WX /wd4201 /UNDEBUG)
    target_compile_options(${VARIANT}-harness PRIVATE /W4 /WX /UNDEBUG)
  endif()
  set(EXPECTED_STAGE --expect-create-device)
  set(UNEXPECTED_STAGE --expect-unsupported)
  if(FEME_PROBE_PAGING_QUEUE)
    set(EXPECTED_STAGE --expect-paging-queue)
  endif()
  if(FEME_PROBE_GPU_VA)
    set(EXPECTED_STAGE --expect-gpu-va)
    if(FEME_PROBE_GPU_VA_EXPECT_INVALIDARG)
      set(EXPECTED_STAGE --expect-gpu-va-rejected)
    endif()
  endif()
  if(FEME_PROBE_MONITORED_FENCE)
    set(EXPECTED_STAGE --expect-monitored-fence)
  endif()
  if(DDI_VERSION IN_LIST FEME_PROBE_DDI_EXPECT_UNSUPPORTED)
    set(EXPECTED_STAGE --expect-unsupported)
    set(UNEXPECTED_STAGE --expect-create-device)
  endif()
  feme_add_probe_test(NAME "os-routed-${VARIANT}" COMMAND ${VARIANT}-harness
    --warp-bootstrap-audit $<TARGET_FILE:${VARIANT}-shim> ${EXPECTED_STAGE})
  feme_add_probe_test(NAME "os-routed-${VARIANT}-wrong-stage"
    COMMAND ${CMAKE_COMMAND}
      "-DHARNESS=$<TARGET_FILE:${VARIANT}-harness>"
      "-DSHIM=$<TARGET_FILE:${VARIANT}-shim>"
      "-DEXPECTATION=${UNEXPECTED_STAGE}"
      -P "${CMAKE_CURRENT_LIST_DIR}/../windows-adapter-probe/ExpectAuditFailure.cmake")
  if(NOT FEME_PROBE_MONITORED_FENCE AND
     NOT DDI_VERSION IN_LIST FEME_PROBE_DDI_EXPECT_UNSUPPORTED)
    feme_add_probe_test(NAME "os-routed-${VARIANT}-monitored-fence-disabled"
      COMMAND ${CMAKE_COMMAND}
        "-DHARNESS=$<TARGET_FILE:${VARIANT}-harness>"
        "-DSHIM=$<TARGET_FILE:${VARIANT}-shim>"
        "-DEXPECTATION=--expect-monitored-fence"
        -P "${CMAKE_CURRENT_LIST_DIR}/../windows-adapter-probe/ExpectAuditFailure.cmake")
  endif()
  if(NOT FEME_PROBE_PAGING_QUEUE AND
     NOT DDI_VERSION IN_LIST FEME_PROBE_DDI_EXPECT_UNSUPPORTED)
    feme_add_probe_test(NAME "os-routed-${VARIANT}-paging-disabled"
      COMMAND ${CMAKE_COMMAND}
        "-DHARNESS=$<TARGET_FILE:${VARIANT}-harness>"
        "-DSHIM=$<TARGET_FILE:${VARIANT}-shim>"
        "-DEXPECTATION=--expect-paging-queue"
        -P "${CMAKE_CURRENT_LIST_DIR}/../windows-adapter-probe/ExpectAuditFailure.cmake")
  endif()
  if(NOT FEME_PROBE_GPU_VA AND
     NOT DDI_VERSION IN_LIST FEME_PROBE_DDI_EXPECT_UNSUPPORTED)
    feme_add_probe_test(NAME "os-routed-${VARIANT}-gpu-va-disabled"
        COMMAND ${CMAKE_COMMAND}
          "-DHARNESS=$<TARGET_FILE:${VARIANT}-harness>"
          "-DSHIM=$<TARGET_FILE:${VARIANT}-shim>"
          "-DEXPECTATION=--expect-gpu-va"
          -P "${CMAKE_CURRENT_LIST_DIR}/../windows-adapter-probe/ExpectAuditFailure.cmake")
    feme_add_probe_test(NAME "os-routed-${VARIANT}-gpu-va-rejected-disabled"
      COMMAND ${CMAKE_COMMAND}
          "-DHARNESS=$<TARGET_FILE:${VARIANT}-harness>"
          "-DSHIM=$<TARGET_FILE:${VARIANT}-shim>"
          "-DEXPECTATION=--expect-gpu-va-rejected"
          -P "${CMAKE_CURRENT_LIST_DIR}/../windows-adapter-probe/ExpectAuditFailure.cmake")
  endif()
  if(FEME_PROBE_GPU_VA AND
     NOT DDI_VERSION IN_LIST FEME_PROBE_DDI_EXPECT_UNSUPPORTED)
    set(WRONG_GPU_VA_OUTCOME --expect-gpu-va-rejected)
    if(FEME_PROBE_GPU_VA_EXPECT_INVALIDARG)
      set(WRONG_GPU_VA_OUTCOME --expect-gpu-va)
    endif()
    feme_add_probe_test(NAME "os-routed-${VARIANT}-gpu-va-wrong-outcome"
      COMMAND ${CMAKE_COMMAND}
          "-DHARNESS=$<TARGET_FILE:${VARIANT}-harness>"
          "-DSHIM=$<TARGET_FILE:${VARIANT}-shim>"
          "-DEXPECTATION=${WRONG_GPU_VA_OUTCOME}"
          -P "${CMAKE_CURRENT_LIST_DIR}/../windows-adapter-probe/ExpectAuditFailure.cmake")
  endif()
endforeach()
