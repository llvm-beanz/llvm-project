# libFeMeRuntimeCPU: the CPU target's shader-side scalar helper bitcode (see
# "Runtime Support Library" in feme/docs/FeMeCPUDesign.md).
#
# FeMeRuntimeCPU.ll is hand-written LLVM IR (not C++): the descriptor
# lookup, bounds/kind checks and per-format conversions it implements are
# meant to be linked *into* a compiled shader module and inlined there (see
# "Descriptor formats"), not called through an ABI boundary of their own --
# writing it directly as IR avoids a clang dependency (feme currently has
# none) for a file whose only consumer is LLVM IR anyway.
#
# The build assembles it to bitcode with llvm-as, then embeds the resulting
# bytes into this library as an ordinary C++ byte array (`embed_bitcode.py`)
# so `feme::cpu::getRuntimeCPUBitcode()` never needs to locate a `.bc` file
# on disk at run time.

set(feme_runtime_cpu_ll ${CMAKE_CURRENT_SOURCE_DIR}/FeMeRuntimeCPU.ll)
set(feme_runtime_cpu_bc ${CMAKE_CURRENT_BINARY_DIR}/FeMeRuntimeCPU.bc)
set(feme_runtime_cpu_embed_cpp
  ${CMAKE_CURRENT_BINARY_DIR}/FeMeRuntimeCPUBitcode.cpp)

add_custom_command(
  OUTPUT ${feme_runtime_cpu_bc}
  COMMAND llvm-as ${feme_runtime_cpu_ll} -o ${feme_runtime_cpu_bc}
  DEPENDS ${feme_runtime_cpu_ll} llvm-as
  COMMENT "Assembling libFeMeRuntimeCPU bitcode"
  VERBATIM
  )

add_custom_command(
  OUTPUT ${feme_runtime_cpu_embed_cpp}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/embed_bitcode.py
          ${feme_runtime_cpu_bc} ${feme_runtime_cpu_embed_cpp}
          FeMeRuntimeCPU
  DEPENDS ${feme_runtime_cpu_bc} ${CMAKE_CURRENT_SOURCE_DIR}/embed_bitcode.py
  COMMENT "Embedding libFeMeRuntimeCPU bitcode"
  VERBATIM
  )

add_llvm_library(FeMeRuntimeCPU
  RuntimeCPU.cpp
  ${feme_runtime_cpu_embed_cpp}

  LINK_COMPONENTS
  Support
  )
