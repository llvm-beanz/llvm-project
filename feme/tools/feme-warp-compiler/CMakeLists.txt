# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_feme_library(feme_warp_compiler SHARED PARTIAL_SOURCES_INTENDED
  WarpShaderCompiler.cpp
  LINK_LIBS PRIVATE
  FeMeCore FeMeTranslateDXSA FeMeTargetDXSA FeMeTargetCPU FeMeTransformsDXIL
  FeMeGraphics FeMeDirect3D)
target_compile_definitions(feme_warp_compiler PRIVATE FEME_WARP_COMPILER_EXPORTS)
add_dependencies(feme_warp_compiler llvm_vcsrevision_h)

add_llvm_executable(feme-warp-compiler-test PARTIAL_SOURCES_INTENDED
  WarpShaderCompilerTest.cpp WarpCompilerDDITests.cpp
  ../../utils/windows-adapter-bootstrap/WarpTables.cpp
  ../../utils/windows-adapter-bootstrap/WarpHeap.cpp)
target_compile_definitions(feme-warp-compiler-test PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN FEME_BOOTSTRAP_WARP_COMPILER=1)
target_link_libraries(feme-warp-compiler-test PRIVATE feme_warp_compiler)
