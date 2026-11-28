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

set(FEME_WINDOWS_SDK_ROOT "")
foreach(CANDIDATE
    "C:/Program Files (x86)/Windows Kits/10/Include"
    "C:/Program Files/Windows Kits/10/Include")
  if(EXISTS "${CANDIDATE}")
    set(FEME_WINDOWS_SDK_ROOT "${CANDIDATE}")
    break()
  endif()
endforeach()
set(FEME_WINDOWS_SDK_VERSION "")
file(GLOB FEME_SDK_VERSION_DIRS
  LIST_DIRECTORIES true
  "${FEME_WINDOWS_SDK_ROOT}/10.0.*")
list(REVERSE FEME_SDK_VERSION_DIRS)
foreach(VERSION_DIR IN LISTS FEME_SDK_VERSION_DIRS)
  if(EXISTS "${VERSION_DIR}/um/d3d12umddi.h")
    get_filename_component(FEME_WINDOWS_SDK_VERSION
      "${VERSION_DIR}" NAME)
    break()
  endif()
endforeach()
if(NOT FEME_WINDOWS_SDK_VERSION)
  message(FATAL_ERROR
    "The Windows SDK DDI headers were not found under "
    "${FEME_WINDOWS_SDK_ROOT}")
endif()
set(FEME_WINDOWS_SDK_INCLUDE
  "${FEME_WINDOWS_SDK_ROOT}/${FEME_WINDOWS_SDK_VERSION}/um"
  "${FEME_WINDOWS_SDK_ROOT}/${FEME_WINDOWS_SDK_VERSION}/shared")
include_directories(SYSTEM ${FEME_WINDOWS_SDK_INCLUDE})

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
  if(DDI_VERSION_SUFFIX STRGREATER_EQUAL "0022")
    target_compile_definitions(${VARIANT}-harness PRIVATE
      FEME_PROBE_EXPECT_CORE_LAYER_ALLOCATION_CALLBACKS=1
      FEME_PROBE_EXPECT_KT_ALLOCATION_CALLBACKS=0)
  else()
    target_compile_definitions(${VARIANT}-harness PRIVATE
      FEME_PROBE_EXPECT_CORE_LAYER_ALLOCATION_CALLBACKS=0
      FEME_PROBE_EXPECT_KT_ALLOCATION_CALLBACKS=1)
  endif()
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

set(WARP_PRIVATE_INFO_VARIANT warp-private-info)
set(WARP_PRIVATE_INFO_OUTPUT_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/${WARP_PRIVATE_INFO_VARIANT}")
file(MAKE_DIRECTORY "${WARP_PRIVATE_INFO_OUTPUT_DIR}")
add_library(${WARP_PRIVATE_INFO_VARIANT}-shim SHARED
  ../windows-adapter-bootstrap/BootstrapStub.cpp)
target_compile_definitions(${WARP_PRIVATE_INFO_VARIANT}-shim PRIVATE
  FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0010
  FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO=1)
set_target_properties(${WARP_PRIVATE_INFO_VARIANT}-shim PROPERTIES
  OUTPUT_NAME d3d10warp
  RUNTIME_OUTPUT_DIRECTORY "${WARP_PRIVATE_INFO_OUTPUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${WARP_PRIVATE_INFO_OUTPUT_DIR}")
add_executable(${WARP_PRIVATE_INFO_VARIANT}-harness
  ../windows-adapter-probe/AdapterProbe.cpp)
set_target_properties(${WARP_PRIVATE_INFO_VARIANT}-harness PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${WARP_PRIVATE_INFO_OUTPUT_DIR}")
target_link_libraries(${WARP_PRIVATE_INFO_VARIANT}-harness PRIVATE d3d12 dxgi)
add_dependencies(${WARP_PRIVATE_INFO_VARIANT}-harness
  ${WARP_PRIVATE_INFO_VARIANT}-shim)
if(MSVC)
  target_compile_options(${WARP_PRIVATE_INFO_VARIANT}-shim PRIVATE
    /W4 /WX /wd4201 /UNDEBUG)
  target_compile_options(${WARP_PRIVATE_INFO_VARIANT}-harness PRIVATE
    /W4 /WX /UNDEBUG)
endif()
feme_add_probe_test(NAME os-routed-warp-private-info
  COMMAND ${WARP_PRIVATE_INFO_VARIANT}-harness
    --warp-bootstrap-audit $<TARGET_FILE:${WARP_PRIVATE_INFO_VARIANT}-shim>
    --expect-warp-private-info)

set(WARP_CONTEXT_VARIANT warp-context)
set(WARP_CONTEXT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${WARP_CONTEXT_VARIANT}")
file(MAKE_DIRECTORY "${WARP_CONTEXT_OUTPUT_DIR}")
add_library(${WARP_CONTEXT_VARIANT}-shim SHARED
  ../windows-adapter-bootstrap/BootstrapStub.cpp)
target_compile_definitions(${WARP_CONTEXT_VARIANT}-shim PRIVATE
  FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0010
  FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO=1
  FEME_BOOTSTRAP_PROBE_WARP_CONTEXT=1)
set_target_properties(${WARP_CONTEXT_VARIANT}-shim PROPERTIES
  OUTPUT_NAME d3d10warp
  RUNTIME_OUTPUT_DIRECTORY "${WARP_CONTEXT_OUTPUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${WARP_CONTEXT_OUTPUT_DIR}")
add_executable(${WARP_CONTEXT_VARIANT}-harness
  ../windows-adapter-probe/AdapterProbe.cpp)
set_target_properties(${WARP_CONTEXT_VARIANT}-harness PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${WARP_CONTEXT_OUTPUT_DIR}")
target_link_libraries(${WARP_CONTEXT_VARIANT}-harness PRIVATE d3d12 dxgi)
add_dependencies(${WARP_CONTEXT_VARIANT}-harness ${WARP_CONTEXT_VARIANT}-shim)
if(MSVC)
  target_compile_options(${WARP_CONTEXT_VARIANT}-shim PRIVATE
    /W4 /WX /wd4201 /UNDEBUG)
  target_compile_options(${WARP_CONTEXT_VARIANT}-harness PRIVATE
    /W4 /WX /UNDEBUG)
endif()
feme_add_probe_test(NAME os-routed-warp-context
  COMMAND ${WARP_CONTEXT_VARIANT}-harness
    --warp-bootstrap-audit $<TARGET_FILE:${WARP_CONTEXT_VARIANT}-shim>
    --expect-warp-context)

set(WARP_DEVICE_CONTEXT_VARIANT warp-device-context)
set(WARP_DEVICE_CONTEXT_OUTPUT_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/${WARP_DEVICE_CONTEXT_VARIANT}")
file(MAKE_DIRECTORY "${WARP_DEVICE_CONTEXT_OUTPUT_DIR}")
add_library(${WARP_DEVICE_CONTEXT_VARIANT}-shim SHARED
  ../windows-adapter-bootstrap/BootstrapStub.cpp
  ../windows-adapter-bootstrap/WarpHeap.cpp)
target_compile_definitions(${WARP_DEVICE_CONTEXT_VARIANT}-shim PRIVATE
  FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0072
  FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO=1
  FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT=1)
set_target_properties(${WARP_DEVICE_CONTEXT_VARIANT}-shim PROPERTIES
  OUTPUT_NAME d3d10warp
  RUNTIME_OUTPUT_DIRECTORY "${WARP_DEVICE_CONTEXT_OUTPUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${WARP_DEVICE_CONTEXT_OUTPUT_DIR}")
add_executable(${WARP_DEVICE_CONTEXT_VARIANT}-harness
  ../windows-adapter-probe/AdapterProbe.cpp)
set_target_properties(${WARP_DEVICE_CONTEXT_VARIANT}-harness PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${WARP_DEVICE_CONTEXT_OUTPUT_DIR}")
target_link_libraries(${WARP_DEVICE_CONTEXT_VARIANT}-harness PRIVATE d3d12 dxgi)
add_dependencies(${WARP_DEVICE_CONTEXT_VARIANT}-harness
  ${WARP_DEVICE_CONTEXT_VARIANT}-shim)
if(MSVC)
  target_compile_options(${WARP_DEVICE_CONTEXT_VARIANT}-shim PRIVATE
    /W4 /WX /wd4201 /UNDEBUG)
  target_compile_options(${WARP_DEVICE_CONTEXT_VARIANT}-harness PRIVATE
    /W4 /WX /UNDEBUG)
endif()
feme_add_probe_test(NAME os-routed-warp-device-context
  COMMAND ${WARP_DEVICE_CONTEXT_VARIANT}-harness
    --warp-bootstrap-audit
    $<TARGET_FILE:${WARP_DEVICE_CONTEXT_VARIANT}-shim>
    --expect-warp-device-context)

set(WARP_PROTOTYPE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/warp-prototype")
add_library(warp-prototype-shim SHARED
  ../windows-adapter-bootstrap/BootstrapStub.cpp
  ../windows-adapter-bootstrap/WarpHeap.cpp
  ../windows-adapter-bootstrap/WarpTables.cpp)
target_compile_definitions(warp-prototype-shim PRIVATE
  FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0072
  FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO=1
  FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT=1
  FEME_BOOTSTRAP_WARP_PROTOTYPE=1)
set_target_properties(warp-prototype-shim PROPERTIES
  OUTPUT_NAME d3d10warp
  RUNTIME_OUTPUT_DIRECTORY "${WARP_PROTOTYPE_OUTPUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${WARP_PROTOTYPE_OUTPUT_DIR}")
add_executable(warp-prototype-harness
  ../windows-adapter-probe/WarpPrototypeProbe.cpp)
set_target_properties(warp-prototype-harness PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${WARP_PROTOTYPE_OUTPUT_DIR}")
target_link_libraries(warp-prototype-harness PRIVATE d3d12 dxgi d3dcompiler)
add_dependencies(warp-prototype-harness warp-prototype-shim)
target_compile_options(warp-prototype-shim PRIVATE /W4 /WX /wd4201 /UNDEBUG)
target_compile_options(warp-prototype-harness PRIVATE /W4 /WX /wd4201 /UNDEBUG)
feme_add_probe_test(NAME os-routed-warp-prototype-shader-rejection
  COMMAND warp-prototype-harness --expect-shader-rejection)
feme_add_probe_test(NAME os-routed-warp-prototype-not-a-device
  WILL_FAIL COMMAND warp-prototype-harness)
feme_add_probe_test(NAME os-routed-warp-prototype-reference-buffer
  COMMAND warp-prototype-harness --reference)

set(FEME_WARP_COMPILER_DLL "" CACHE FILEPATH "Owned FeMe compiler bridge DLL")
set(FEME_WARP_COMPILER_LIBRARY "" CACHE FILEPATH "Compiler bridge import library")
if(FEME_WARP_COMPILER_DLL OR FEME_WARP_COMPILER_LIBRARY)
  if(NOT EXISTS "${FEME_WARP_COMPILER_DLL}" OR
     NOT EXISTS "${FEME_WARP_COMPILER_LIBRARY}")
    message(FATAL_ERROR "Both FeMe compiler DLL and import library are required")
  endif()
  set(WARP_COMPILER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/warp-compiler")
  add_library(warp-compiler-shim SHARED
    ../windows-adapter-bootstrap/BootstrapStub.cpp
    ../windows-adapter-bootstrap/WarpHeap.cpp
    ../windows-adapter-bootstrap/WarpTables.cpp
    ../../lib/Image/CopyWalk.cpp
    ../../lib/Image/SubresourceLayout.cpp)
  target_include_directories(warp-compiler-shim PRIVATE ../../include)
  target_compile_definitions(warp-compiler-shim PRIVATE
    FEME_BOOTSTRAP_PINNED_VERSION=D3D12DDI_SUPPORTED_0072
    FEME_BOOTSTRAP_PROBE_WARP_PRIVATE_INFO=1
    FEME_BOOTSTRAP_PROBE_WARP_DEVICE_CONTEXT=1
    FEME_BOOTSTRAP_WARP_PROTOTYPE=1 FEME_BOOTSTRAP_WARP_COMPILER=1)
  target_link_libraries(warp-compiler-shim PRIVATE "${FEME_WARP_COMPILER_LIBRARY}")
  set_target_properties(warp-compiler-shim PROPERTIES
    OUTPUT_NAME d3d10warp
    RUNTIME_OUTPUT_DIRECTORY "${WARP_COMPILER_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${WARP_COMPILER_OUTPUT_DIR}"
    LINK_DEPENDS "${FEME_WARP_COMPILER_DLL}")
  add_custom_command(TARGET warp-compiler-shim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FEME_WARP_COMPILER_DLL}"
      "${WARP_COMPILER_OUTPUT_DIR}/feme_warp_compiler.dll")
  add_executable(warp-compiler-harness
    ../windows-adapter-probe/WarpPrototypeProbe.cpp)
  set_target_properties(warp-compiler-harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WARP_COMPILER_OUTPUT_DIR}")
  target_link_libraries(warp-compiler-harness PRIVATE d3d12 dxgi d3dcompiler)
  set(FEME_WARP_PROFILE_VERSION 27)
  target_compile_definitions(warp-compiler-harness PRIVATE
    FEME_WARP_PROFILE_VERSION=${FEME_WARP_PROFILE_VERSION})
  set(FEME_WARP_PROFILE_DXC_EVIDENCE false)
  set(FEME_WARP_PROFILE_COEXIST_EVIDENCE false)
  add_dependencies(warp-compiler-harness warp-compiler-shim)
  target_compile_options(warp-compiler-shim PRIVATE /W4 /WX /wd4201 /UNDEBUG)
  target_compile_options(warp-compiler-harness PRIVATE /W4 /WX /wd4201 /UNDEBUG)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-buffer
    COMMAND warp-compiler-harness)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-placed-buffers
    COMMAND warp-compiler-harness --placed-buffers)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-footprint-copies
    COMMAND warp-compiler-harness --footprint-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-footprint-copies
    COMMAND warp-compiler-harness --ordered-footprint-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-footprint-copies
    COMMAND warp-compiler-harness --reference-footprint-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-ordered-footprint-copies
    COMMAND warp-compiler-harness --reference-ordered-footprint-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-texture-copies
    COMMAND warp-compiler-harness --texture-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-texture-copies
    COMMAND warp-compiler-harness --ordered-texture-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-texture-copies
    COMMAND warp-compiler-harness --reference-texture-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-ordered-texture-copies
    COMMAND warp-compiler-harness --reference-ordered-texture-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-texture-array-copies
    COMMAND warp-compiler-harness --texture-array-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-texture-array-copies
    COMMAND warp-compiler-harness --ordered-texture-array-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-texture-array-copies
    COMMAND warp-compiler-harness --reference-texture-array-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-ordered-texture-array-copies
    COMMAND warp-compiler-harness --reference-ordered-texture-array-copies)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-placed-buffers
    COMMAND warp-compiler-harness --reference-placed-buffers)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-commands
    COMMAND warp-compiler-harness --commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-buffer-commands
    COMMAND warp-compiler-harness --buffer-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-fences
    COMMAND warp-compiler-harness --fences)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-buffer-commands
    COMMAND warp-compiler-harness --ordered-buffer-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-buffer-commands
    COMMAND warp-compiler-harness --reference-buffer-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-indirect-commands
    COMMAND warp-compiler-harness --indirect-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-indirect-commands
    COMMAND warp-compiler-harness --reference-indirect-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-indirect-commands
    COMMAND warp-compiler-harness --ordered-indirect-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-predicate-commands
    COMMAND warp-compiler-harness --predicate-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-predicate-commands
    COMMAND warp-compiler-harness --reference-predicate-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-predicate-commands
    COMMAND warp-compiler-harness --ordered-predicate-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-predicate-indirect-commands
    COMMAND warp-compiler-harness --predicate-indirect-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-predicate-indirect-commands
    COMMAND warp-compiler-harness --reference-predicate-indirect-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-predicate-indirect-commands
    COMMAND warp-compiler-harness --ordered-predicate-indirect-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-cbv-commands
    COMMAND warp-compiler-harness --cbv-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-cbv-commands
    COMMAND warp-compiler-harness --reference-cbv-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-cbv-commands
    COMMAND warp-compiler-harness --ordered-cbv-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-constant-commands
    COMMAND warp-compiler-harness --constant-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-constant-commands
    COMMAND warp-compiler-harness --reference-constant-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-constant-commands
    COMMAND warp-compiler-harness --ordered-constant-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-visibility-commands
    COMMAND warp-compiler-harness --visibility-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-visibility-commands
    COMMAND warp-compiler-harness --reference-visibility-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-visibility-commands
    COMMAND warp-compiler-harness --ordered-visibility-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-table-commands
    COMMAND warp-compiler-harness --table-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-table-commands
    COMMAND warp-compiler-harness --reference-table-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-table-commands
    COMMAND warp-compiler-harness --ordered-table-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-view-commands
    COMMAND warp-compiler-harness --view-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-view-commands
    COMMAND warp-compiler-harness --reference-view-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-view-commands
    COMMAND warp-compiler-harness --ordered-view-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-structured-view-commands
    COMMAND warp-compiler-harness --structured-view-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-structured-view-commands
    COMMAND warp-compiler-harness --reference-structured-view-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-structured-view-commands
    COMMAND warp-compiler-harness --ordered-structured-view-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-array-commands
    COMMAND warp-compiler-harness --array-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-array-commands
    COMMAND warp-compiler-harness --reference-array-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-array-commands
    COMMAND warp-compiler-harness --ordered-array-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-structured-array-commands
    COMMAND warp-compiler-harness --structured-array-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-structured-array-commands
    COMMAND warp-compiler-harness --reference-structured-array-commands)
  feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-structured-array-commands
    COMMAND warp-compiler-harness --ordered-structured-array-commands)
  foreach(operation IN ITEMS increment decrement append consume)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-counter-${operation}-commands
      COMMAND warp-compiler-harness --counter-${operation}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-reference-counter-${operation}-commands
      COMMAND warp-compiler-harness --reference-counter-${operation}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-counter-${operation}-commands
      COMMAND warp-compiler-harness --ordered-counter-${operation}-commands)
  endforeach()
  set(WARP_TYPED_FORMATS uint sint float uint2 sint2 float2 uint4 sint4 float4
    uint16 sint16 float16 uint8 sint8 unorm8
    uint16x4 sint16x4 float16x4 uint8x4 sint8x4 unorm8x4
    uint8x2 sint8x2 uint16x2 sint16x2)
  set(FEME_WARP_PACKED_GATES "")
  set(FEME_WARP_TYPED_UAV_GATES "")
  foreach(scalar IN LISTS WARP_TYPED_FORMATS)
    foreach(prefix IN ITEMS owned owned-ordered owned-dxil reference)
      string(APPEND FEME_WARP_TYPED_UAV_GATES
        "    \"os-routed-warp-compiler-${prefix}-typed-uav-${scalar}-commands\",\n")
    endforeach()
    if(scalar MATCHES "(16|8)")
      foreach(prefix IN ITEMS owned owned-ordered owned-dxil reference)
        foreach(binding IN ITEMS typed mapped-typed)
          string(APPEND FEME_WARP_PACKED_GATES
            "    \"os-routed-warp-compiler-${prefix}-${binding}-${scalar}-commands\",\n")
        endforeach()
      endforeach()
    endif()
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-typed-${scalar}-commands
      COMMAND warp-compiler-harness --typed-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-reference-typed-${scalar}-commands
      COMMAND warp-compiler-harness --reference-typed-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-typed-${scalar}-commands
      COMMAND warp-compiler-harness --ordered-typed-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-mapped-typed-${scalar}-commands
      COMMAND warp-compiler-harness --mapped-typed-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-reference-mapped-typed-${scalar}-commands
      COMMAND warp-compiler-harness --mapped-reference-typed-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-mapped-typed-${scalar}-commands
      COMMAND warp-compiler-harness --mapped-ordered-typed-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-typed-uav-${scalar}-commands
      COMMAND warp-compiler-harness --typed-uav-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-reference-typed-uav-${scalar}-commands
      COMMAND warp-compiler-harness --reference-typed-uav-${scalar}-commands)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-ordered-typed-uav-${scalar}-commands
      COMMAND warp-compiler-harness --ordered-typed-uav-${scalar}-commands)
  endforeach()
  find_program(FEME_DXC_EXECUTABLE NAMES dxc
    DOC "DXC for original owned-DXIL execution fixtures")
  if(FEME_DXC_EXECUTABLE)
    set(FEME_WARP_PROFILE_DXC_EVIDENCE true)
    set(WARP_DXIL_COUNTER_FIXTURES)
    foreach(operation IN ITEMS increment decrement append consume)
      set(counter_define)
      if(operation STREQUAL "increment")
        set(counter_operation IncrementCounter)
      elseif(operation STREQUAL "decrement")
        set(counter_operation DecrementCounter)
      elseif(operation STREQUAL "append")
        set(counter_operation Append)
        set(counter_define -DAPPEND_VIEWS=1)
      else()
        set(counter_operation Consume)
        set(counter_define -DCONSUME_VIEWS=1)
      endif()
      set(fixture "${WARP_COMPILER_OUTPUT_DIR}/owned-counter-${operation}-compute.dxil")
      list(APPEND WARP_DXIL_COUNTER_FIXTURES "${fixture}")
      add_custom_command(OUTPUT "${fixture}"
        COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
          -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DSTRUCTURED_VIEWS=1
          -DDESCRIPTOR_ARRAYS=1 -DCOUNTER_VIEWS=1
          -DCOUNTER_OPERATION=${counter_operation}
          ${counter_define}
          -DROOT_REGISTER=u3 -Fo "${fixture}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
        DEPENDS ../../test/Tools/warp-compiler-dxil.hlsl
        VERBATIM)
      feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-counter-${operation}-commands
        COMMAND warp-compiler-harness --dxil-counter-${operation}-commands "${fixture}")
    endforeach()
    add_custom_target(warp-dxil-counter-fixtures
      DEPENDS ${WARP_DXIL_COUNTER_FIXTURES})
    add_dependencies(warp-compiler-harness warp-dxil-counter-fixtures)
    set(WARP_DXIL_TYPED_FIXTURES)
    foreach(scalar IN LISTS WARP_TYPED_FORMATS)
      set(normalized_define)
      set(half_define)
      set(packed_define)
      if(scalar MATCHES "(16|8)")
        set(packed_define -DPACKED_VIEWS=1)
        if(scalar MATCHES "^(float|unorm)")
          set(shader_scalar float)
        elseif(scalar MATCHES "^sint")
          set(shader_scalar int)
        else()
          set(shader_scalar uint)
        endif()
        if(scalar MATCHES "x([24])$")
          string(APPEND shader_scalar "${CMAKE_MATCH_1}")
        endif()
        if(scalar MATCHES "^unorm")
          set(normalized_define -DTYPED_UNORM=1)
        elseif(scalar MATCHES "^float16")
          set(half_define -DTYPED_HALF=1)
        endif()
      else()
        string(REPLACE "sint" "int" shader_scalar "${scalar}")
      endif()
      string(REGEX REPLACE "[24]$" "" output_scalar "${shader_scalar}")
      set(vector_define)
      if(scalar MATCHES "[24]$")
        set(vector_define -DTYPED_VECTOR=1)
      endif()
      set(fixture "${WARP_COMPILER_OUTPUT_DIR}/owned-typed-${scalar}-compute.dxil")
      list(APPEND WARP_DXIL_TYPED_FIXTURES "${fixture}")
      add_custom_command(OUTPUT "${fixture}"
        COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
          -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DTYPED_VIEWS=1
          -DTYPED_SCALAR=${shader_scalar} -DDESCRIPTOR_ARRAYS=1
          -DTYPED_OUTPUT_SCALAR=${output_scalar}
          ${vector_define}
          ${normalized_define}
          ${half_define}
          ${packed_define}
          -DROOT_REGISTER=u3 -Fo "${fixture}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
        DEPENDS ../../test/Tools/warp-compiler-dxil.hlsl
        VERBATIM)
      feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-typed-${scalar}-commands
        COMMAND warp-compiler-harness --dxil-typed-${scalar}-commands "${fixture}")
      feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-mapped-typed-${scalar}-commands
        COMMAND warp-compiler-harness --mapped-dxil-typed-${scalar}-commands "${fixture}")
      set(fixture "${WARP_COMPILER_OUTPUT_DIR}/owned-typed-uav-${scalar}-compute.dxil")
      list(APPEND WARP_DXIL_TYPED_FIXTURES "${fixture}")
      add_custom_command(OUTPUT "${fixture}"
        COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
          -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DTYPED_VIEWS=1
          -DTYPED_SCALAR=${shader_scalar} -DDESCRIPTOR_ARRAYS=1
          -DTYPED_OUTPUT_SCALAR=${shader_scalar} -DTYPED_UAV_VIEWS=1
          -DROOT_REGISTER=u3 -Fo "${fixture}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
        DEPENDS ../../test/Tools/warp-compiler-dxil.hlsl
        VERBATIM)
      feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-typed-uav-${scalar}-commands
        COMMAND warp-compiler-harness --dxil-typed-uav-${scalar}-commands "${fixture}")
    endforeach()
    set(WARP_DXIL_FIXTURE "${WARP_COMPILER_OUTPUT_DIR}/owned-compute.dxil")
    set(WARP_DXIL_BUFFER_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-buffer-compute.dxil")
    set(WARP_DXIL_CBV_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-cbv-compute.dxil")
    set(WARP_DXIL_CONSTANT_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-constant-compute.dxil")
    set(WARP_DXIL_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-view-compute.dxil")
    set(WARP_DXIL_STRUCTURED_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-structured-view-compute.dxil")
    set(WARP_DXIL_ARRAY_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-array-compute.dxil")
    set(WARP_DXIL_STRUCTURED_ARRAY_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-structured-array-compute.dxil")
    set(WARP_DXIL_TEXTURE_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-texture-view-compute.dxil")
    set(WARP_DXIL_TEXTURE_UAV_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-texture-uav-view-compute.dxil")
    set(WARP_DXIL_SIGNED_TEXTURE_UAV_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-signed-texture-uav-view-compute.dxil")
    add_custom_command(OUTPUT "${WARP_DXIL_SIGNED_TEXTURE_UAV_VIEW_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main -DSIGNED_INTEGER=1
        -Fo "${WARP_DXIL_SIGNED_TEXTURE_UAV_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-texture-uav-views.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-texture-uav-views.hlsl
      VERBATIM)
    set(WARP_DXIL_TEXTURE_UAV_VOLUME_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-volume-texture-uav-view-compute.dxil")
    add_custom_command(OUTPUT "${WARP_DXIL_TEXTURE_UAV_VOLUME_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main -DVOLUME=1
        -Fo "${WARP_DXIL_TEXTURE_UAV_VOLUME_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-texture-uav-views.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-texture-uav-views.hlsl
      VERBATIM)
    add_custom_command(OUTPUT "${WARP_DXIL_TEXTURE_UAV_VIEW_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -Fo "${WARP_DXIL_TEXTURE_UAV_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-texture-uav-views.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-texture-uav-views.hlsl
      VERBATIM)
    set(WARP_DXIL_SAMPLER_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-sampler-view-compute.dxil")
    set(WARP_DXIL_STATIC_SAMPLER_VIEW_FIXTURE
      "${WARP_COMPILER_OUTPUT_DIR}/owned-static-sampler-view-compute.dxil")
    add_custom_command(OUTPUT "${WARP_DXIL_STATIC_SAMPLER_VIEW_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main -DSTATIC_SAMPLERS=1
        -Fo "${WARP_DXIL_STATIC_SAMPLER_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-sampler-views.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-sampler-views.hlsl
      VERBATIM)
    add_custom_command(OUTPUT "${WARP_DXIL_SAMPLER_VIEW_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -Fo "${WARP_DXIL_SAMPLER_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-sampler-views.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-sampler-views.hlsl
      VERBATIM)
    add_custom_command(OUTPUT "${WARP_DXIL_TEXTURE_VIEW_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -Fo "${WARP_DXIL_TEXTURE_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-texture-views.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-texture-views.hlsl
      VERBATIM)
    foreach(mode IN ITEMS "" "ordered-" "reference-" "reference-ordered-")
      set(gate_mode "${mode}")
      if(NOT mode MATCHES "^reference")
        set(gate_mode "owned-${mode}")
      endif()
      feme_add_probe_test(NAME os-routed-warp-compiler-${gate_mode}texture-views
        COMMAND warp-compiler-harness "--${mode}texture-views"
          "${WARP_DXIL_TEXTURE_VIEW_FIXTURE}")
      feme_add_probe_test(NAME os-routed-warp-compiler-${gate_mode}texture-uav-views
        COMMAND warp-compiler-harness "--${mode}texture-uav-views"
          "${WARP_DXIL_TEXTURE_UAV_VIEW_FIXTURE}")
      feme_add_probe_test(NAME os-routed-warp-compiler-${gate_mode}volume-texture-uav-views
        COMMAND warp-compiler-harness "--${mode}volume-texture-uav-views"
          "${WARP_DXIL_TEXTURE_UAV_VOLUME_FIXTURE}")
      foreach(format IN ITEMS uint8 sint8 uint16 sint16 uint8x4 sint8x4 uint16x4 sint16x4
          uint8x2 sint8x2 uint16x2 sint16x2)
        set(fixture "${WARP_DXIL_TEXTURE_UAV_VIEW_FIXTURE}")
        if(format MATCHES "^sint")
          set(fixture "${WARP_DXIL_SIGNED_TEXTURE_UAV_VIEW_FIXTURE}")
        endif()
        set(gate "os-routed-warp-compiler-${gate_mode}narrow-${format}-texture-uav-views")
        feme_add_probe_test(NAME "${gate}"
          COMMAND warp-compiler-harness "--${mode}narrow-texture-uav-views"
            "${fixture}" "${format}")
        string(APPEND FEME_WARP_NARROW_IMAGE_GATES "    \"${gate}\",\n")
      endforeach()
      feme_add_probe_test(NAME os-routed-warp-compiler-${gate_mode}single-layer-texture-views
        COMMAND warp-compiler-harness "--${mode}single-layer-texture-views"
          "${WARP_DXIL_TEXTURE_VIEW_FIXTURE}")
      feme_add_probe_test(NAME os-routed-warp-compiler-${gate_mode}sampler-views
        COMMAND warp-compiler-harness "--${mode}sampler-views"
          "${WARP_DXIL_SAMPLER_VIEW_FIXTURE}")
      feme_add_probe_test(NAME os-routed-warp-compiler-${gate_mode}static-sampler-views
        COMMAND warp-compiler-harness "--${mode}static-sampler-views"
          "${WARP_DXIL_STATIC_SAMPLER_VIEW_FIXTURE}")
    endforeach()
    add_custom_command(OUTPUT "${WARP_DXIL_FIXTURE}" "${WARP_DXIL_BUFFER_FIXTURE}"
        "${WARP_DXIL_CBV_FIXTURE}"
        "${WARP_DXIL_CONSTANT_FIXTURE}"
        "${WARP_DXIL_VIEW_FIXTURE}" "${WARP_DXIL_STRUCTURED_VIEW_FIXTURE}"
        "${WARP_DXIL_ARRAY_FIXTURE}" "${WARP_DXIL_STRUCTURED_ARRAY_FIXTURE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${WARP_COMPILER_OUTPUT_DIR}"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -Fo "${WARP_DXIL_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DROOT_REGISTER=u3 -Fo "${WARP_DXIL_BUFFER_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DCBV=1 -DROOT_REGISTER=u3 -Fo "${WARP_DXIL_CBV_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DCBV=1 -DINLINE_CONSTANTS=1 -DROOT_REGISTER=u3
        -Fo "${WARP_DXIL_CONSTANT_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DROOT_REGISTER=u3
        -Fo "${WARP_DXIL_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DSTRUCTURED_VIEWS=1 -DROOT_REGISTER=u3
        -Fo "${WARP_DXIL_STRUCTURED_VIEW_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DDESCRIPTOR_ARRAYS=1 -DROOT_REGISTER=u3
        -Fo "${WARP_DXIL_ARRAY_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      COMMAND "${FEME_DXC_EXECUTABLE}" -T cs_6_0 -E main
        -DBUFFER=1 -DCBV=1 -DBUFFER_VIEWS=1 -DSTRUCTURED_VIEWS=1
        -DDESCRIPTOR_ARRAYS=1 -DROOT_REGISTER=u3
        -Fo "${WARP_DXIL_STRUCTURED_ARRAY_FIXTURE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Tools/warp-compiler-dxil.hlsl"
      DEPENDS ../../test/Tools/warp-compiler-dxil.hlsl
      VERBATIM)
    add_custom_target(warp-owned-dxil-fixture
      DEPENDS ${WARP_DXIL_TYPED_FIXTURES}
        "${WARP_DXIL_TEXTURE_UAV_VIEW_FIXTURE}"
        "${WARP_DXIL_SIGNED_TEXTURE_UAV_VIEW_FIXTURE}"
        "${WARP_DXIL_TEXTURE_UAV_VOLUME_FIXTURE}"
        "${WARP_DXIL_TEXTURE_VIEW_FIXTURE}"
        "${WARP_DXIL_SAMPLER_VIEW_FIXTURE}"
        "${WARP_DXIL_STATIC_SAMPLER_VIEW_FIXTURE}"
        "${WARP_DXIL_FIXTURE}" "${WARP_DXIL_BUFFER_FIXTURE}"
        "${WARP_DXIL_CBV_FIXTURE}"
        "${WARP_DXIL_CONSTANT_FIXTURE}" "${WARP_DXIL_VIEW_FIXTURE}"
        "${WARP_DXIL_STRUCTURED_VIEW_FIXTURE}" "${WARP_DXIL_ARRAY_FIXTURE}"
        "${WARP_DXIL_STRUCTURED_ARRAY_FIXTURE}")
    add_dependencies(warp-compiler-harness warp-owned-dxil-fixture)
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-commands
      COMMAND warp-compiler-harness --dxil-commands "${WARP_DXIL_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-buffer-commands
      COMMAND warp-compiler-harness --dxil-buffer-commands
        "${WARP_DXIL_BUFFER_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-indirect-commands
      COMMAND warp-compiler-harness --dxil-indirect-commands
        "${WARP_DXIL_BUFFER_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-predicate-commands
      COMMAND warp-compiler-harness --dxil-predicate-commands
        "${WARP_DXIL_BUFFER_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-predicate-indirect-commands
      COMMAND warp-compiler-harness --dxil-predicate-indirect-commands
        "${WARP_DXIL_BUFFER_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-cbv-commands
      COMMAND warp-compiler-harness --dxil-cbv-commands
        "${WARP_DXIL_CBV_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-constant-commands
      COMMAND warp-compiler-harness --dxil-constant-commands
        "${WARP_DXIL_CONSTANT_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-visibility-commands
      COMMAND warp-compiler-harness --dxil-visibility-commands
        "${WARP_DXIL_CONSTANT_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-table-commands
      COMMAND warp-compiler-harness --dxil-table-commands
        "${WARP_DXIL_CBV_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-view-commands
      COMMAND warp-compiler-harness --dxil-view-commands
        "${WARP_DXIL_VIEW_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-structured-view-commands
      COMMAND warp-compiler-harness --dxil-structured-view-commands
        "${WARP_DXIL_STRUCTURED_VIEW_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-array-commands
      COMMAND warp-compiler-harness --dxil-array-commands
        "${WARP_DXIL_ARRAY_FIXTURE}")
    feme_add_probe_test(NAME os-routed-warp-compiler-owned-dxil-structured-array-commands
      COMMAND warp-compiler-harness --dxil-structured-array-commands
        "${WARP_DXIL_STRUCTURED_ARRAY_FIXTURE}")
    get_filename_component(FEME_DXC_DIRECTORY "${FEME_DXC_EXECUTABLE}" DIRECTORY)
    find_file(FEME_DXCOMPILER_DLL NAMES dxcompiler.dll
      HINTS "${FEME_DXC_DIRECTORY}")
    find_path(FEME_VULKAN_HEADERS NAMES vulkan/vulkan.h
      HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
    get_filename_component(FEME_COMPILER_DIRECTORY
      "${FEME_WARP_COMPILER_DLL}" DIRECTORY)
    set(FEME_WARP_COEXIST_VULKAN_DLL
      "${FEME_COMPILER_DIRECTORY}/libfeme_vulkan.dll" CACHE FILEPATH
      "FeMe Vulkan ICD for the same-process WARP coexistence gate")
    if(FEME_DXCOMPILER_DLL AND FEME_VULKAN_HEADERS AND
       EXISTS "${FEME_WARP_COEXIST_VULKAN_DLL}")
      set(FEME_WARP_PROFILE_COEXIST_EVIDENCE true)
      target_compile_definitions(warp-compiler-harness PRIVATE
        FEME_WARP_COEXIST=1)
      target_include_directories(warp-compiler-harness PRIVATE
        "${FEME_VULKAN_HEADERS}")
      feme_add_probe_test(NAME os-routed-warp-compiler-owned-coexistence
        COMMAND warp-compiler-harness --coexist "${FEME_DXCOMPILER_DLL}"
          "${FEME_WARP_COEXIST_VULKAN_DLL}")
    endif()
  endif()
  configure_file(WARPHeadlessCapabilities.json.in
    "${WARP_COMPILER_OUTPUT_DIR}/warp-headless-capabilities.json" @ONLY)
  feme_add_probe_test(NAME os-routed-warp-compiler-reference-buffer
    COMMAND warp-compiler-harness --reference)
  feme_add_probe_test(NAME os-routed-warp-compiler-not-shader-rejection
    WILL_FAIL COMMAND warp-compiler-harness --expect-shader-rejection)
endif()
