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

set(FEME_INCLUDE_TESTS ${LLVM_INCLUDE_TESTS} CACHE BOOL
  "Generate build targets for the feme unit and lit tests.")

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(tools)

if(FEME_INCLUDE_TESTS)
  add_subdirectory(unittests)
  add_subdirectory(test)
endif()
