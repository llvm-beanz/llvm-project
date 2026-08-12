//===- RuntimeCPU.h - libFeMeRuntimeCPU bitcode accessor ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::cpu::getRuntimeCPUBitcode`, which returns
// `libFeMeRuntimeCPU`'s shader-side scalar helper IR (see "Runtime Support
// Library" in feme/docs/FeMeCPUDesign.md and
// feme/runtime/CPU/FeMeRuntimeCPU.ll) as an in-memory bitcode buffer. The
// bitcode is assembled at build time (`llvm-as`) and embedded directly into
// this library as a byte array, so a consumer -- the linking step a later
// milestone adds to the CPU pipeline (see "Descriptor formats"'s "After
// SIMDization and wrapper construction, FeMe links only the referenced
// helper definitions into the shader module") -- never needs to locate a
// `.bc` file on disk at run time.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_RUNTIMECPU_H
#define FEME_TARGET_CPU_RUNTIMECPU_H

#include "llvm/Support/MemoryBufferRef.h"

namespace feme::cpu {

/// Returns a `MemoryBufferRef` over `libFeMeRuntimeCPU`'s embedded bitcode
/// (see the file comment above). The returned buffer aliases static
/// storage and is valid for the lifetime of the program; parse it with
/// `llvm::parseBitcodeFile`/`llvm::getOwningLazyBitcodeModule` to get an
/// `llvm::Module` to link from.
llvm::MemoryBufferRef getRuntimeCPUBitcode();

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RUNTIMECPU_H
