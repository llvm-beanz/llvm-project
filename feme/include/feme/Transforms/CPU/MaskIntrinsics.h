//===- MaskIntrinsics.h - `feme.cpu.mask.*` call helpers ----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the creation helper for `feme.cpu.mask.any`, one of the
// FeMe-internal mask intrinsics described in the "Mask representation
// between phases" subsection of "Phase 3: Linearization and Predication" in
// feme/docs/FeMeCPUDesign.md.
//
// `feme::cpu::LinearizePass` (roadmap milestone 6) is the only current
// producer: a loop with a divergent exit needs a way to ask "is any lane
// still active", which is meaningless on the scalar `i1` mask this pass
// works with (a single invocation's mask is just that one bit) until Phase 4
// widens it to `<W x i1>` and lowers this call to `llvm.vector.reduce.or`
// (see the "Mask representation between phases" table) -- that lowering is
// roadmap milestone 7's job, not this file's.
//
// Only `feme.cpu.mask.any` is implemented so far. The masked memory
// intrinsics the same design subsection describes (`feme.cpu.masked.load`/
// `.store`/...) are not yet needed: this milestone's linearizer only masks
// the canonical `feme.cpu.resource.*` calls (which already carry a mask
// operand, see feme::cpu::ResourceCalls.h), not arbitrary `load`/`store`;
// see the Status section's milestone 6 deviation note in
// feme/docs/FeMeCPUDesign.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_MASKINTRINSICS_H
#define FEME_TRANSFORMS_CPU_MASKINTRINSICS_H

#include "llvm/ADT/Twine.h"

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
class Value;
} // namespace llvm

namespace feme::cpu {

/// Gets (inserting if absent) the `feme.cpu.mask.any` declaration in \p M:
/// `declare i1 @feme.cpu.mask.any(i1)`, an ordinary declaration (not an
/// intrinsic -- see "Mask representation between phases": `feme.cpu.*` names
/// are deliberately not `llvm.`-prefixed) with `nounwind willreturn` and no
/// memory effects, since it is a pure reduction over its argument.
llvm::Function *getOrInsertMaskAny(llvm::Module &M);

/// Builds a `feme.cpu.mask.any` call over \p Mask.
llvm::CallInst *createMaskAny(llvm::IRBuilderBase &Builder, llvm::Value *Mask,
                              const llvm::Twine &Name = "");

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_MASKINTRINSICS_H
