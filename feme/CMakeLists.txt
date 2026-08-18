# feme project.
#
# feme is currently only supported as an in-tree LLVM sub-project, built via
# -DLLVM_ENABLE_PROJECTS=feme from the top-level llvm/CMakeLists.txt.
# Standalone (out-of-tree) builds against an installed LLVM+MLIR are
# intentionally out of scope for now (see feme/docs/Design.md).
cmake_minimum_required(VERSION 3.20.0)

set(LLVM_SUBPROJECT_TITLE "FeMe")

if(NOT DEFINED LLVM_COMMON_CMAKE_UTILS)
  set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
endif()
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR
    "feme must be built in-tree as an LLVM sub-project "
    "(-DLLVM_ENABLE_PROJECTS=feme); standalone builds are not yet "
    "supported (see feme/docs/Design.md).")
endif()

include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FEME_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(FEME_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

# feme depends on MLIR (see feme/docs/Design.md); this is added as an
# implicit dependency in llvm/CMakeLists.txt when feme is enabled.
set(MLIR_MAIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mlir" CACHE PATH
  "Path to MLIR source tree")
set(MLIR_INCLUDE_DIRS ${MLIR_MAIN_SRC_DIR}/include)
set(MLIR_TABLEGEN_OUTPUT_DIR ${LLVM_BINARY_DIR}/tools/mlir/include)
include_directories(SYSTEM ${MLIR_INCLUDE_DIRS})
include_directories(SYSTEM ${MLIR_TABLEGEN_OUTPUT_DIR})

include_directories(${FEME_SOURCE_DIR}/include)
include_directories(${FEME_BINARY_DIR}/include)

list(APPEND CMAKE_MODULE_PATH "${FEME_SOURCE_DIR}/cmake/modules")
include(AddFeMe)

set(FEME_INCLUDE_TESTS ${LLVM_INCLUDE_TESTS} CACHE BOOL
  "Generate build targets for the feme unit and lit tests.")

# The Vulkan SDK is feme's first external (non-LLVM/MLIR) dependency, and
# feme has no prior optional-external-package pattern to copy (see
# feme/docs/FeMeVulkanDesign.md's "Project and Library Boundaries"). It backs
# the `libfeme_vulkan` ICD (feme/docs/FeMeVulkanDesign.md); when it can't be
# found, that ICD and its tests are simply left out of the build rather than
# failing configuration, so every other feme component stays buildable
# without it.
option(FEME_ENABLE_VULKAN
  "Build the FeMe Vulkan ICD (libfeme_vulkan) if a Vulkan SDK is found"
  ON)

set(FEME_HAVE_VULKAN_HEADERS OFF)
set(FEME_HAVE_VULKAN_LOADER OFF)
if(FEME_ENABLE_VULKAN)
  # vk.xml's schema and the loader-driver interface features this design
  # relies on (loader-driver interface version 7, `vk_icdGetPhysicalDeviceProcAddr`,
  # `LocalSizeId`) are all present by Vulkan 1.3; older SDKs are rejected
  # rather than silently generating a smaller entrypoint table. CMake's
  # built-in FindVulkan module finds a single Vulkan SDK installation (e.g.
  # via the `VULKAN_SDK` environment variable) and exposes both the headers
  # (`Vulkan::Headers`) and the loader import library (`Vulkan::Vulkan`)
  # from it, so no separate Vulkan-Headers CMake package or manual
  # `find_library()` for the loader is needed.
  find_package(Vulkan 1.3 QUIET)
  if(TARGET Vulkan::Headers)
    set(FEME_HAVE_VULKAN_HEADERS ON)
    message(STATUS "feme: found Vulkan ${Vulkan_VERSION}; building libfeme_vulkan")
  else()
    message(STATUS "feme: Vulkan SDK >= 1.3 not found; "
      "libfeme_vulkan will not be built (see FEME_ENABLE_VULKAN)")
  endif()

  # The Vulkan *loader* (as opposed to the headers) is only needed to link
  # the loader-smoke-test client tool below and to run the lit tests that
  # exercise `libfeme_vulkan` through it ("Testing Strategy": "Lit tests
  # invoking tiny Vulkan clients"); it is never a dependency of the ICD
  # itself (see "Project and Library Boundaries": "not ... the Vulkan loader
  # library at runtime").
  if(FEME_HAVE_VULKAN_HEADERS AND TARGET Vulkan::Vulkan)
    set(FEME_HAVE_VULKAN_LOADER ON)
  endif()
endif()

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(runtime)
add_subdirectory(tools)

if(FEME_INCLUDE_TESTS)
  add_subdirectory(unittests)
  add_subdirectory(test)
endif()
