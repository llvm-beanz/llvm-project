//===- UnsupportedOps.cpp - CPU target early raised-op diagnostics -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/UnsupportedOps.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace feme::cpu {

Error checkSupportedRaisedOps(const Module &M) {
  for (const Function &F : M) {
    if (!F.isDeclaration() || F.use_empty())
      continue;

    // A leftover DXIL-specific op the front end didn't raise: the CPU
    // pipeline only understands the format-agnostic `llvm.{dx,spv}.*`
    // vocabulary (see "Raised IR prerequisites").
    if (F.getName().starts_with("dx.op."))
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' was not raised to idiomatic "
          "LLVM IR before reaching the FeMe CPU target",
          F.getName().str().c_str());

    switch (F.getIntrinsicID()) {
    case Intrinsic::dx_resource_handlefrombinding:
    case Intrinsic::dx_resource_handlefromimplicitbinding:
    case Intrinsic::spv_resource_handlefrombinding:
    case Intrinsic::spv_resource_handlefromimplicitbinding:
      // A register-bound resource handle that survives to this point is
      // one `feme::cpu::BoundResourceNormalizationPass` could not (or does
      // not yet) normalize into a heap access -- an unbounded range, a
      // conflicting re-declaration of the same binding, an unsupported
      // resource kind, an implicit binding, or a SPIR-V binding (see
      // "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md for
      // exactly which of these that pass accepts). The CPU target has no
      // other way to address such a resource: bindless
      // (ResourceDescriptorHeap/SamplerDescriptorHeap) access, or a finite,
      // unambiguous traditional binding, are the only two forms it accepts.
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' is a register-bound resource "
          "handle the FeMe CPU target cannot normalize into a heap access "
          "(an unbounded range, a conflicting re-declaration, or an "
          "unsupported resource kind); express it as a finite, "
          "unambiguous traditional binding or bindless "
          "(ResourceDescriptorHeap/SamplerDescriptorHeap) access",
          F.getName().str().c_str());
    default:
      break;
    }
  }
  return Error::success();
}

} // namespace feme::cpu
