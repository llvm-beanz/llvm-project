//===- VerifyStructured.h - CPU target Phase 1 postcondition check -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::verifyStructured, the postcondition checker
// for "Phase 1: Preparation" (feme::cpu::PreparePass) described in the "CFG
// restructurization test suite" section of feme/docs/FeMeCPUDesign.md:
//
//  - no irreducible cycles (every `llvm::CycleInfo` cycle, at every nesting
//    level, has a single entry block);
//  - every cycle has a unique exit block;
//  - no `switch` (the linearizer only understands two-way branches);
//  - no critical edges; and
//  - every divergent branch has a reconvergence point (an immediate
//    post-dominator), which is what makes linearizing it possible.
//
// This is roadmap milestone 5's layer 2: it turns the named-shape corpus
// (layer 1) into one-line tests ("restructure this and assert it is
// structured") and is also run as an assertions-only postcondition inside
// `PreparePass` itself.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_VERIFYSTRUCTURED_H
#define FEME_TRANSFORMS_CPU_VERIFYSTRUCTURED_H

namespace llvm {
class Function;
class raw_ostream;
} // namespace llvm

namespace feme::cpu {

/// Checks that \p F satisfies Phase 1's postconditions (see the file
/// comment above). Every violation found is reported to \p ErrOS (if
/// non-null); returns whether \p F satisfied every one of them.
bool verifyStructured(llvm::Function &F, llvm::raw_ostream *ErrOS = nullptr);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_VERIFYSTRUCTURED_H
