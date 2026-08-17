//===- DomainInvocations.h - Tessellator-to-domain-batch marshaling -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::graphics::buildDomainInvocations`, one piece of
// the host-side glue roadmap R34's open issue list calls out as missing:
// "nothing marshals a tessellator's `DomainPoint` output into a
// `FemeDomainInvocation` array". `feme::graphics::tessellate` (Tessellator.h)
// produces a `TessellatedPatch` of `DomainPoint`s; `feme::cpu::FemeDomainArgs`
// (RuntimeABI.h) expects those same coordinates as a
// `feme::cpu::FemeDomainInvocation` array, one per batched domain-stage
// invocation. This is the (trivial, but real) conversion between the two,
// kept as its own function so `feme::graphics::Executor` need not duplicate
// it once it drives a real domain-stage batch.
//
// This lives in `feme::graphics` rather than `feme::cpu` for the same reason
// GeometryStreamCollection.h does: it depends on `feme::graphics::
// TessellatedPatch`, and `FeMeTargetCPU` does not depend on `FeMeGraphics`
// (see feme/lib/Graphics/CMakeLists.txt).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_DOMAININVOCATIONS_H
#define FEME_GRAPHICS_DOMAININVOCATIONS_H

#include "feme/Graphics/Tessellator.h"

#include <vector>

namespace feme::cpu {
struct FemeDomainInvocation;
} // namespace feme::cpu

namespace feme::graphics {

/// Converts \p Patch's generated domain coordinates into one
/// `feme::cpu::FemeDomainInvocation` per point, in the same order, for use
/// as a `feme::cpu::FemeDomainArgs::Invocations` array. Every invocation's
/// `Reserved` field is zeroed.
std::vector<cpu::FemeDomainInvocation>
buildDomainInvocations(const TessellatedPatch &Patch);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_DOMAININVOCATIONS_H
