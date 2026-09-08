//===- UnifyDivergentExitNodes.h - CPU target Phase 1 early-return fix -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::unifyDivergentExitNodes, a "Phase 1:
// Preparation" (feme::cpu::PreparePass) step that runs before
// `FixIrreducible`/`StructurizeCFG` (see feme/docs/FeMeCPUDesign.md). See
// its own doc comment below for why it exists and what it does.
//
// Roadmap L71: without this, a function with more than one `ret` (most
// commonly a GLSL/HLSL early-return bounds check, `if (cond) return;`, very
// common in a compute shader with no other bounds enforcement) reaches
// `feme::cpu::verifyStructured`'s "every divergent branch has a
// reconvergence point" postcondition already violated -- the branch's
// early-return arm exits the function outright rather than rejoining the
// other arm anywhere, so it has no immediate post-dominator at all. This
// mirrors AMDGPU's own `AMDGPUUnifyDivergentExitNodes`, an in-tree
// precedent for exactly this `StructurizeCFG` limitation.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_UNIFYDIVERGENTEXITNODES_H
#define FEME_TRANSFORMS_CPU_UNIFYDIVERGENTEXITNODES_H

namespace llvm {
class Function;
} // namespace llvm

namespace feme::cpu {

/// Merges every `ret` block in \p F into a single shared one, replacing
/// each original `ret` with an unconditional branch to it (and a `phi`
/// merging the returned value, for a non-`void` return type). Returns
/// whether \p F had more than one `ret` block to begin with (i.e. whether
/// it changed \p F).
///
/// This project's own SPMD/masked-lane execution model (see
/// `feme::cpu::WaveTTIImpl`) treats every branch as potentially divergent,
/// so unifying every `ret` block unconditionally -- rather than only when
/// actually reached divergently, a `UniformityInfo` analysis this function
/// deliberately skips -- both sidesteps needing that analysis at all and
/// is always safe: once every `ret` is the same block, an early-return
/// idiom becomes an ordinary reconverging `if`, indistinguishable in shape
/// from any other divergent branch's predicated/masked arm that later
/// phases (Phase 3: Linearization) already know how to handle. A function
/// with a single `ret` block already (the common case once every helper
/// function this project raises is fully inlined) is left untouched.
bool unifyDivergentExitNodes(llvm::Function &F);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_UNIFYDIVERGENTEXITNODES_H
