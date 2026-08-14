//===- RootConstantLowering.h - CPU target root constant lowering -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::RootConstantLoweringPass, which lowers the
// one register-bound constant buffer "Root constants" in
// feme/docs/FeMeCPUDesign.md carves out an exception for -- by default
// `(b0, space0)` -- into loads from the CPU ABI's inline root-constant
// block, rather than leaving it for `feme::cpu::checkSupportedRaisedOps` to
// reject as an unsupported register-bound resource handle.
//
// Scope (roadmap step R12; see the design doc's "Root constants" section
// for the target shape this narrows):
//
//  - Only the single default binding `(b0, space0)` is recognized; the
//    `--cpu-root-constants=bN,spaceM` override the design describes is not
//    yet implemented.
//  - Only a non-array (`RangeSize == 1`) `dx.CBuffer` handle whose every use
//    is a `llvm.dx.resource.load.cbufferrow.4.*` call (DXIL's 32-bit-per-
//    component row shape; `cbufferrow.2`/`.8`, 64- and 16-bit components,
//    are not yet lowered) with a *constant* row index is accepted; anything
//    else is left for `checkSupportedRaisedOps` to reject exactly as before
//    this pass existed.
//  - A function that also performs bindless (`handlefromheap`) resource
//    access is still supported, but lowered differently: rather than this
//    pass adding its own `root_constants`/`root_constant_size` parameters
//    (which would collide by name with `feme::cpu::ResourceLoweringPass`'s
//    own, since every function it touches gets that pair regardless of
//    whether it actually uses root constants), `feme::cpu::
//    ResourceLoweringPass` itself finishes the job, reusing the
//    parameters it already adds -- see `matchRootConstantAccess`/
//    `lowerRootConstantAccess` in RootConstantLowering.h, which both this
//    pass and that one call.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_ROOTCONSTANTLOWERING_H
#define FEME_TRANSFORMS_CPU_ROOTCONSTANTLOWERING_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"

#include <cstdint>
#include <optional>

namespace llvm {
class CallInst;
class Function;
class Value;
} // namespace llvm

namespace feme::cpu {

/// One `llvm.dx.resource.load.cbufferrow.4.*` call reading a constant,
/// compile-time-known row of the recognized root-constant binding.
struct RootConstantRowLoad {
  llvm::CallInst *Load;
  uint64_t Row;
};

/// A single function's complete, supported root-constant access: the one
/// recognized `dx.CBuffer` handle, and every `cbufferrow` load through it.
/// See RootConstantLowering.cpp's `matchRootConstantAccess` for exactly
/// what is recognized.
struct RootConstantAccess {
  llvm::CallInst *Handle;
  llvm::SmallVector<RootConstantRowLoad, 4> Loads;
};

/// Returns \p F's root-constant access, if it has exactly one recognized
/// `dx.CBuffer` handle (see the file comment above for the one binding and
/// access shape this milestone accepts) used only in ways this milestone
/// can lower, or `std::nullopt` otherwise (no candidate, more than one, or
/// one with an unsupported use). Performs no mutation -- both
/// `feme::cpu::RootConstantLoweringPass` (a function with no other
/// resource access) and `feme::cpu::ResourceLoweringPass` (a function that
/// also performs bindless resource access, and so already has its own
/// `RootConstants`/`RootConstantSize` parameters to lower into) call this
/// to decide whether they have anything to lower, and
/// `feme::cpu::checkSupportedRaisedOps` calls it to know a still-present
/// `handlefrombinding` call is one of these two passes' responsibility,
/// not an unsupported operation.
std::optional<RootConstantAccess> matchRootConstantAccess(llvm::Function &F);

/// Rewrites every load in \p Access into a bounds-checked load from \p
/// RootConstants (zero for any component outside \p RootConstantSize's
/// declared span, see "Root constants" in feme/docs/FeMeCPUDesign.md), and
/// erases \p Access.Handle. Returns the total root-constant byte span \p
/// Access's own loads read, for the caller to attach to whichever
/// `!feme.cpu.resources` metadata entry it emits.
uint32_t lowerRootConstantAccess(const RootConstantAccess &Access,
                                 llvm::Value *RootConstants,
                                 llvm::Value *RootConstantSize);

/// Lowers the one register-bound constant buffer the CPU ABI's root
/// constant block covers into loads from it. See the file comment above
/// for current scope.
class RootConstantLoweringPass
    : public llvm::PassInfoMixin<RootConstantLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-root-constants"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_ROOTCONSTANTLOWERING_H
