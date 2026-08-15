//===- ResourceLowering.h - Lower raised resources to NVPTX -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::nvptx::ResourceLoweringPass, the NVPTX
// counterpart to feme::amdgpu::ResourceLoweringPass (see that pass's own
// header comment for the general shape this mirrors): it re-expresses a
// raised shader's resource bindings -- `llvm.dx.resource.*` or
// `llvm.spv.resource.*` -- in terms NVPTX understands.
//
// A graphics API binds a shader's resources out of band, through a
// descriptor table the shader refers to by (register space, register). A
// CUDA kernel has no such concept either: it receives everything it
// operates on as kernel parameters, the same as an AMDGPU kernel does. This
// pass therefore turns each distinct resource binding an entry point uses
// into an additional `ptr addrspace(1)` kernel parameter (NVPTX's global
// address space, the same numeric value as AMDGPU's -- see
// feme::amdgpu::ResourceLoweringPass's own `GlobalAddressSpace` comment),
// appended to the entry point's signature in a deterministic (space,
// register) order, and rewrites the buffer accesses through that binding
// into ordinary loads and stores of the pointed-to memory.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_NVPTX_RESOURCELOWERING_H
#define FEME_TRANSFORMS_NVPTX_RESOURCELOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace nvptx {

/// Converts each resource binding a shader entry point uses into a kernel
/// pointer parameter, and its typed buffer accesses into plain loads/stores.
/// Handles both raised intrinsic families (`llvm.dx.resource.*` and
/// `llvm.spv.resource.*`); a single binding is always reached through one
/// family, never a mix of both.
///
/// Entry points using a resource this pass cannot model yet -- a non-typed
/// buffer, a dynamically indexed binding array, or a handle consumed by
/// anything other than a single typed buffer load/store per access -- are
/// left untouched entirely, rather than partially rewritten (see
/// feme::amdgpu::ResourceLoweringPass's own class comment).
class ResourceLoweringPass : public llvm::PassInfoMixin<ResourceLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-nvptx-lower-resources"; }
};

} // namespace nvptx
} // namespace feme

#endif // FEME_TRANSFORMS_NVPTX_RESOURCELOWERING_H
