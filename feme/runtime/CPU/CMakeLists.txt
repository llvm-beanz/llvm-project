# libFeMeRuntimeCPU: the CPU target's shader-side scalar helper bitcode (see
# "Runtime Support Library" in feme/docs/FeMeCPUDesign.md).
#
# FeMeRuntimeCPU.c is plain freestanding C (see feme/docs/Design.md's
# "Dependencies" note): the descriptor lookup, bounds/kind checks and
# per-format conversions it implements are meant to be linked *into* a
# compiled shader module and inlined there (see "Descriptor formats"), not
# called through an ABI boundary of their own, but hand-writing that logic
# directly as LLVM IR proved a maintainability burden not worth the clang
# dependency it was avoiding -- feme depends on clang as its host C compiler
# for this file (llvm/CMakeLists.txt enables clang implicitly alongside
# feme, the same way it does for flang and lldb).
#
# The build compiles it to bitcode with clang, then embeds the resulting
# bytes into this library as an ordinary C++ byte array (`embed_bitcode.py`)
# so `feme::cpu::getRuntimeCPUBitcode()` never needs to locate a `.bc` file
# on disk at run time.

set(feme_runtime_cpu_c ${CMAKE_CURRENT_SOURCE_DIR}/FeMeRuntimeCPU.c)
set(feme_runtime_cpu_bc ${CMAKE_CURRENT_BINARY_DIR}/FeMeRuntimeCPU.bc)
set(feme_runtime_cpu_embed_cpp
  ${CMAKE_CURRENT_BINARY_DIR}/FeMeRuntimeCPUBitcode.cpp)

# (Roadmap H8v) An image atomic (`__atomic_fetch_add`/etc., compiled for
# this bitcode's own host architecture -- it is never cross-compiled, see
# `get_host_tool_path` below) would otherwise codegen, on an AArch64 host,
# as a call to an out-of-line `__aarch64_ldadd4_acq_rel`-style helper (part
# of libgcc/compiler-rt, normally resolved via IFUNC at ordinary
# static-link time) rather than an inline LDADD/CAS instruction --
# unresolvable by this bitcode's own ORC JIT, which has no such runtime
# library to link against. `-mno-outline-atomics` avoids that, but is an
# AArch64-only driver flag clang rejects under `-Werror` on every other
# host architecture (`-Werror,-Woption-ignored`), so it is only added when
# actually targeting AArch64.
set(feme_runtime_cpu_extra_flags)
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  list(APPEND feme_runtime_cpu_extra_flags -mno-outline-atomics)
endif()

get_host_tool_path(clang CLANG clang_exe clang_target)

# FeMeRuntimeCPU.c is compiled by the custom command above, not by
# add_llvm_library's normal per-target compiler invocation (it needs
# freestanding/optimization flags of its own, and produces bitcode rather
# than an object file for this library); tell LLVM's source-file-list check
# to allow it to sit in this directory without being one of
# add_llvm_library's sources below.
set(LLVM_OPTIONAL_SOURCES FeMeRuntimeCPU.c)

add_custom_command(
  OUTPUT ${feme_runtime_cpu_bc}
  COMMAND ${clang_exe}
          -c -emit-llvm -O2 -ffreestanding -fno-builtin
          ${feme_runtime_cpu_extra_flags}
          -Wall -Wextra -Werror
          ${feme_runtime_cpu_c} -o ${feme_runtime_cpu_bc}
  DEPENDS ${feme_runtime_cpu_c} ${clang_target}
  COMMENT "Compiling libFeMeRuntimeCPU bitcode"
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
