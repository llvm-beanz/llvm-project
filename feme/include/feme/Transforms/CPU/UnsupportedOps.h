//===- UnsupportedOps.h - CPU target early raised-op diagnostics -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::checkSupportedRaisedOps, which diagnoses a
// raised module before it enters the CPU pipeline (`feme::cpu::PreparePass`
// onwards) rather than letting it fail later, unhelpfully, deep inside a
// transform that assumed every operation it would see was one the CPU
// target actually supports. See the "Raised IR prerequisites" and "Resource
// Model" sections of feme/docs/FeMeCPUDesign.md:
//
//  - A source-format op the front end did not (yet) raise -- a leftover
//    `dx.op.*`/DXIL-specific declaration still being called -- means the
//    raised-IR contract the CPU pipeline depends on was not met.
//  - A register-bound resource handle (`llvm.{dx,spv}.resource.
//    handlefrombinding`/`handlefromimplicitbinding`) is rejected outright:
//    the CPU target accepts bindless shaders only (descriptor-heap handles,
//    `llvm.dx.resource.handlefromheap`), a deliberate narrowing of scope,
//    not an implementation gap. The one exception -- a single register-bound
//    constant buffer lowered to the CPU ABI's root-constant block (roadmap
//    step R12; see `feme::cpu::RootConstantLoweringPass`) -- is normalized
//    away before this check runs, the same way
//    `feme::cpu::BoundResourceNormalizationPass` normalizes a finite
//    traditional binding into a heap access; a register-bound handle this
//    check does still see was left behind because neither pass could (or
//    would) normalize it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_UNSUPPORTEDOPS_H
#define FEME_TRANSFORMS_CPU_UNSUPPORTEDOPS_H

#include "llvm/Support/Error.h"

namespace llvm {
class Module;
} // namespace llvm

namespace feme::cpu {

/// Returns an `Error` naming the first unsupported raised operation found in
/// \p M (see the file comment above for what that covers), or
/// `Error::success()` if none is found.
llvm::Error checkSupportedRaisedOps(const llvm::Module &M);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_UNSUPPORTEDOPS_H
