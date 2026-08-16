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
// feme/docs/FeMeCPUDesign.md carves out an exception for into loads from
// the CPU ABI's inline root-constant block, rather than leaving it for
// `feme::cpu::checkSupportedRaisedOps` to reject as an unsupported
// register-bound resource handle.
//
// Scope (roadmap step R25 broadened this from R12's original, narrower
// shape; see the design doc's "Root constants" section for the target
// shape this now matches):
//
//  - Any single `(space, register)` binding is recognized, not just the
//    default `(b0, space0)` -- whichever one `dx.CBuffer` handle a function
//    reads is the one this pass promotes, as long as it is the only one.
//    Two or more distinct bindings remain ambiguous and are left entirely
//    alone, for `checkSupportedRaisedOps` to reject, exactly as a single
//    non-default binding was before R25.
//  - An array binding (`RangeSize > 1`) is accepted, with either a constant
//    or dynamic array index (the `handlefrombinding` call's own `Index`
//    operand); an *unbounded* range (DXIL's `RangeSize == -1` sentinel) is
//    not, since it has no fixed advertised size to bounds-check against.
//  - A `llvm.dx.resource.load.cbufferrow.4.*` access (DXIL's 32-bit-per-
//    component row shape; `cbufferrow.2`/`.8`, 64- and 16-bit components,
//    are not yet lowered) is accepted with either a constant or dynamic row
//    index; anything else is left for `checkSupportedRaisedOps` to reject
//    exactly as before this pass existed.
//  - The root-constant span this access requires is the binding's full
//    advertised size (its declared per-element byte size, from the
//    `dx.CBuffer` handle's own type, times `RangeSize`) rather than only
//    the span rows actually touched happen to cover -- see "Root
//    constants" in feme/docs/FeMeCPUDesign.md for why this is required
//    once a row or array index can be dynamic (there is no longer a fixed
//    set of rows to inspect statically).
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

/// One `llvm.dx.resource.load.cbufferrow.4.*` call reading a row of the
/// recognized root-constant binding. \p Row is the load's own row-index
/// operand, kept as a `Value` rather than a resolved constant: roadmap R25
/// lifted the constant-row-index restriction, so this may be a
/// dynamically-computed `i32` just as easily as a `ConstantInt` (in which
/// case `lowerRootConstantAccess`'s arithmetic on it constant-folds back
/// down to the same code a compile-time-known row produced before).
struct RootConstantRowLoad {
  llvm::CallInst *Load;
  llvm::Value *Row;
};

/// A single function's complete, supported root-constant access: the one
/// recognized `dx.CBuffer` handle, every `cbufferrow` load through it, and
/// the binding's own declared shape. See RootConstantLowering.cpp's
/// `matchRootConstantAccess` for exactly what is recognized.
struct RootConstantAccess {
  llvm::CallInst *Handle;
  llvm::SmallVector<RootConstantRowLoad, 4> Loads;
  /// The binding's source register space and base register (roadmap R25:
  /// any single binding is recognized, not just `(space0, b0)`) -- reported
  /// to the caller so it can attach them to whichever `!feme.cpu.resources`
  /// metadata entry it emits, for a host to know which binding the root
  /// constant block corresponds to.
  uint32_t Space = 0;
  uint32_t Register = 0;
  /// The binding's declared array length (the `handlefrombinding` call's
  /// own `RangeSize` operand); 1 for a non-array binding.
  uint32_t RangeSize = 1;
  /// One array element's declared byte size (the `dx.CBuffer` handle
  /// type's own byte length, e.g. 32 for `target("dx.CBuffer", [32 x
  /// i8])`).
  uint32_t ElementSize = 0;
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
/// erases \p Access.Handle. Returns \p Access's binding's full advertised
/// byte span (`Access.ElementSize * Access.RangeSize`), for the caller to
/// attach to whichever `!feme.cpu.resources` metadata entry it emits --
/// not merely the span \p Access's own loads happen to statically cover,
/// which roadmap R25 made insufficient the moment a row or array index can
/// be dynamic (see the file comment above).
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
