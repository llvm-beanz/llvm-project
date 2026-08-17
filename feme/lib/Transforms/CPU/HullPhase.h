//===- HullPhase.h - Hull control-point vs. patch-constant phase -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is private to feme/lib/Transforms/CPU: it declares
// `feme::cpu::isPatchConstantPhase`, the discriminator `HullWrapperPass`
// and `PatchConstantWrapperPass` both use to decide, among the functions
// declaring `feme::ShaderStage::Hull`, which one each of them wraps. A
// hull shader's two phases (control-point and patch-constant) share one
// `feme::ShaderStage` -- Direct3D and Vulkan have no separate stage of
// their own for the patch-constant function -- so a single stage tag
// cannot itself tell the two apart the way it does for every other stage.
//
// `SignatureDirection::PatchOutput` can: it names exactly the patch-
// constant signature (tessellation factors and patch constants; see
// `feme::SignatureDirection`'s own comment and
// `feme::dxil::convertEntrySignature`'s "a hull shader's patch-constant
// signature is its own output" rule), which only the patch-constant phase
// ever writes. A control-point-phase function's own signature, by
// contrast, only ever has `Input`/`Output` elements (its control points'
// attributes) -- see `HullWrapper.cpp`'s scope. So a hull-stage function
// is the patch-constant phase exactly when its attached `EntrySignature`
// contains a `PatchOutput` element, and the control-point phase otherwise.
//
// This is a structural discriminator, not a linkage from a real hull
// entry point to its separately-declared patch-constant function (e.g.
// DXIL's `hs.patchconstantfunc` property) -- that linkage is not parsed
// yet (see roadmap R34's "Deferred" list), so today each phase must be
// compiled as its own, independently named, `feme::ShaderStage::Hull`
// entry point.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_TRANSFORMS_CPU_HULLPHASE_H
#define FEME_LIB_TRANSFORMS_CPU_HULLPHASE_H

namespace llvm {
class Function;
} // namespace llvm

namespace feme::cpu {

/// Whether \p F's attached `feme.signature` metadata (if any) declares at
/// least one `SignatureDirection::PatchOutput` element -- i.e. whether \p F
/// is the patch-constant phase of a hull shader rather than its
/// control-point phase. Returns false (the control-point-phase default) for
/// a function with no attached signature at all.
bool isPatchConstantPhase(const llvm::Function &F);

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_HULLPHASE_H
