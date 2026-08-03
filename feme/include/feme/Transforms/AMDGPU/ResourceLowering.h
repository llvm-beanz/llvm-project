//===- ResourceLowering.h - Lower raised resources to AMDGPU ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::amdgpu::ResourceLoweringPass, which re-expresses a
// raised shader's resource bindings (`llvm.dx.resource.*`, see
// feme::dxil::OpRaisingPass) in terms AMDGPU understands.
//
// A graphics API binds a shader's resources out of band, through a descriptor
// table the shader refers to by (register space, register). AMDGPU kernels
// have no such concept: a compute kernel receives everything it operates on
// as kernel arguments. This pass therefore turns each distinct resource
// binding an entry point uses into an additional `ptr addrspace(1)` kernel
// argument, appended to the entry point's signature in a deterministic
// (space, register) order, and rewrites the buffer accesses through that
// binding into ordinary loads and stores of the pointed-to memory.
//
// The resulting kernel is dispatchable by any host runtime that can bind a
// global memory allocation per resource, in the same order the original
// shader declared its bindings -- the AMDGPU equivalent of the descriptor
// table the shader started with.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_AMDGPU_RESOURCELOWERING_H
#define FEME_TRANSFORMS_AMDGPU_RESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace amdgpu {

/// Converts each resource binding a shader entry point uses into a kernel
/// pointer argument, and its typed buffer accesses into plain loads/stores.
///
/// Entry points using a resource this pass cannot model yet -- a non-typed
/// buffer, a dynamically indexed binding array, or a handle consumed by
/// anything other than a typed buffer load/store -- are left untouched
/// entirely, rather than partially rewritten, so the failure is a clean
/// "unsupported" from the backend rather than silently wrong code.
class ResourceLoweringPass : public llvm::PassInfoMixin<ResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-amdgpu-lower-resources"; }
};

} // namespace amdgpu
} // namespace feme

#endif // FEME_TRANSFORMS_AMDGPU_RESOURCELOWERING_H
