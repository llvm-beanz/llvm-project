//===- InlineHelperFunctions.h - Inline non-entry-point functions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::InlineHelperFunctionsPass, closing roadmap
// L76(b) (see feme/docs/Roadmap.md) -- a real GLSL-sourced (glslang-compiled)
// module that calls a user-defined helper function (e.g. a GLSL function
// computing a per-invocation-varying value from its arguments) produces
// silently wrong results (broadcasting only one lane's argument value to
// every invocation, and NaN once the callee also reads a bound resource):
// every entry-point-only CPU-pipeline pass downstream of this one
// (`feme::cpu::SIMDizePass`, `feme::cpu::LinearizePass`,
// `feme::cpu::WaveLoweringPass`, the stage-specific `*WrapperPass`es, ...)
// walks only the one `llvm::Function` `feme::isShaderEntryPoint` flags,
// leaving any other, separately-compiled helper function's own body
// completely untouched -- still scalar, still expecting a single uniform
// argument -- while the *call site* inside the (now SIMD-widened) entry
// function feeds it a value that's only correct for one lane.
//
// An HLSL/DXIL-sourced module never exercises this gap because `dxc`
// itself always fully inlines every user-defined function into its entry
// point before ever emitting DXIL/SPIR-V; only a GLSL/glslang-compiled
// module (where a non-trivial helper function routinely survives as a
// separate `OpFunction`/`OpFunctionCall` pair) reaches this pipeline with
// more than one non-declaration function in the first place.
//
// This pass restores the invariant every later pass already assumes --
// exactly one non-declaration function per shader stage -- by inlining
// every call to a non-entry-point function into its caller (using LLVM's
// own `AlwaysInlinerPass` after marking each such function `alwaysinline`
// and `internal`), then removing the now-dead helper function bodies with
// `GlobalDCEPass`. It runs first in `feme::cpu::JITOptions`'s pipeline (see
// Pipeline.cpp), before any pass that would otherwise see the helper
// function's own untransformed body.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_INLINEHELPERFUNCTIONS_H
#define FEME_TRANSFORMS_CPU_INLINEHELPERFUNCTIONS_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Inlines every call to a non-entry-point function into its caller, then
/// deletes the now-dead helper function bodies. See the file comment above
/// for why a GLSL-sourced module needs this and an HLSL/DXIL-sourced one
/// never does.
class InlineHelperFunctionsPass
    : public llvm::PassInfoMixin<InlineHelperFunctionsPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-inline-helper-functions"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_INLINEHELPERFUNCTIONS_H
