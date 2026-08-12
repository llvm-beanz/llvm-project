//===- UnsupportedOps.h - CPU target early raised-op diagnostics -*- C++ -*-===//
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
//    not an implementation gap. (The design's future root-constant
//    exception for exactly one register-bound constant buffer is not yet
//    implemented -- see the Roadmap -- so every register-bound handle is
//    rejected for now, including that one.)
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
