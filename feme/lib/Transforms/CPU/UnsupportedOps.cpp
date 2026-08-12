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
      // Register-bound resources are rejected: the CPU target accepts
      // bindless shaders only (see "Resource Model" in
      // feme/docs/FeMeCPUDesign.md). This over-approximates that section's
      // eventual root-constant exception -- not yet implemented, see the
      // Roadmap -- by rejecting every register-bound handle, that one
      // included.
      return createStringError(
          inconvertibleErrorCode(),
          "unsupported raised operation: '%s' is a register-bound resource "
          "handle; the FeMe CPU target accepts bindless "
          "(ResourceDescriptorHeap/SamplerDescriptorHeap) shaders only",
          F.getName().str().c_str());
    default:
      break;
    }
  }
  return Error::success();
}

} // namespace feme::cpu
