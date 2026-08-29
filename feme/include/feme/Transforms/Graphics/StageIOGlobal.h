//===- StageIOGlobal.h - Recognize SPIR-V stage-IO globals ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `isSPIRVStageIOGlobal`, shared between
// `CanonicalizeStagePass` (which rewrites a recognized stage-IO global's
// load/store into a `feme.stage.*` call where it can) and
// `ValidateStagePass` (which, roadmap H6g-b-c, diagnoses one that
// canonicalization left behind unrewritten instead of letting it reach
// `feme::cpu`'s JIT as a genuinely undefined symbol).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_GRAPHICS_STAGEIOGLOBAL_H
#define FEME_TRANSFORMS_GRAPHICS_STAGEIOGLOBAL_H

#include "llvm/IR/GlobalVariable.h"

namespace feme {
namespace graphics {

/// Whether \p GV is a stage-IO variable -- the shape
/// `StageIOGlobalVariablePattern`/`feme::spirv::attachStageIODecorations`
/// (feme/lib/Conversion/SPIRVToLLVM/) produce: address space 7 (`Input`) or
/// 8 (`Output`), carrying either whole-variable `!spirv.Decorations`
/// metadata or (roadmap H2d) a builtin interface block's own per-member
/// `feme.spirv.MemberDecorations` metadata -- the shape a block variable
/// (e.g. `gl_PerVertex`) gets instead, having no whole-variable decoration
/// of its own (roadmap H2c). Sets \p AddrSpace to \p GV's address space
/// when true.
inline bool isSPIRVStageIOGlobal(const llvm::GlobalVariable *GV,
                                 unsigned &AddrSpace) {
  if (!GV)
    return false;
  AddrSpace = GV->getAddressSpace();
  if (AddrSpace != 7 && AddrSpace != 8)
    return false;
  return GV->getMetadata("spirv.Decorations") != nullptr ||
        GV->getMetadata("feme.spirv.MemberDecorations") != nullptr;
}

} // namespace graphics
} // namespace feme

#endif // FEME_TRANSFORMS_GRAPHICS_STAGEIOGLOBAL_H
