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

set(FEME_WARP_REFERENCE "" CACHE FILEPATH "Existing WARP 1.0.19 x64 DLL")
if(NOT EXISTS "${FEME_WARP_REFERENCE}")
  message(FATAL_ERROR "Supply FEME_WARP_REFERENCE; no binaries are downloaded.")
endif()
file(SHA256 "${FEME_WARP_REFERENCE}" REFERENCE_HASH)
if(NOT REFERENCE_HASH STREQUAL
   "1fd2f9d991f00d890d06c00f791a756446f5f29da508b12fdeb8ffd1b5696add")
  message(FATAL_ERROR "The reference DLL is not the pinned WARP 1.0.19 x64 binary.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# PE forwarding preserves the reference functions' ABI without guessing private
# signatures or importing Microsoft's implementation into this project.
set(REFERENCE_EXPORTS
  D3D11RefGetLastCreation D3DLayerGetInterface OpenAdapter OpenAdapter10_2
  OpenDisplayAdapter1 QueryDListForApplication1 QueryDListForApplication2
  QueryMuxDListForApplication)
enable_testing()
foreach(VARIANT reference public-exports private-ordinal bare negotiation)
  set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${VARIANT}")
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")
  if(NOT VARIANT STREQUAL bare AND NOT VARIANT STREQUAL negotiation)
    configure_file("${FEME_WARP_REFERENCE}"
      "${OUTPUT_DIR}/feme_warp_reference.dll" COPYONLY)
  endif()
  if(VARIANT STREQUAL negotiation)
    add_library(${VARIANT}-shim SHARED
      ../windows-adapter-bootstrap/BootstrapStub.cpp)
  else()
    add_library(${VARIANT}-shim SHARED Intercept.cpp)
  endif()
  if(NOT VARIANT STREQUAL bare AND NOT VARIANT STREQUAL negotiation)
    foreach(EXPORT IN LISTS REFERENCE_EXPORTS)
      target_link_options(${VARIANT}-shim PRIVATE
        "/EXPORT:${EXPORT}=feme_warp_reference.${EXPORT}")
    endforeach()
  endif()
  if(VARIANT STREQUAL reference)
    target_link_options(${VARIANT}-shim PRIVATE
      "/EXPORT:OpenAdapter12=feme_warp_reference.OpenAdapter12")
  elseif(NOT VARIANT STREQUAL negotiation)
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
    add_test(NAME reference-forwarding COMMAND ${VARIANT}-harness)
  elseif(VARIANT STREQUAL negotiation)
    add_test(NAME os-routed-negotiation COMMAND ${VARIANT}-harness
      --warp-bootstrap $<TARGET_FILE:${VARIANT}-shim>)
  else()
    add_test(NAME ${VARIANT}-interception COMMAND ${VARIANT}-harness
      --warp-shim $<TARGET_FILE:${VARIANT}-shim>)
  endif()
endforeach()
add_test(NAME generic-software-rejection COMMAND bare-harness
  --stub $<TARGET_FILE:bare-shim>)
set_tests_properties(reference-forwarding public-exports-interception
  private-ordinal-interception bare-interception os-routed-negotiation
  generic-software-rejection
  PROPERTIES TIMEOUT 30)
