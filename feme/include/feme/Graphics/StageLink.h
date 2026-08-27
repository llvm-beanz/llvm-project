//===- StageLink.h - Cross-stage attribute linking ---------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::graphics::linkStageElements`, the general
// cross-stage attribute linker roadmap G5/R34 recorded as missing: two
// independently compiled stages each carry their own `EntrySignature` with
// its own `ElementID` numbering, so "the producer's output and the
// consumer's input are the same attribute" is a `Location`/system-value
// match, not an `ElementID` match.
//
// `feme::graphics::executeDraws` already performs this match inline for
// the one pair it had (vertex output -> fragment input, its `LinkedVarying`
// list), where the *interpolator* consumes the result. The chained
// tessellation stages need the same match, but with a plain scalar copy
// between two `StageStorage` blocks as the consumer -- a hull control-point
// phase's output patch is read verbatim by the patch-constant phase and the
// domain stage, not interpolated. That copy is `copyLinkedElements`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_STAGELINK_H
#define FEME_GRAPHICS_STAGELINK_H

#include "feme/Core/Signature.h"
#include "feme/Graphics/StageStorage.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::graphics {

/// One producer element linked to the consumer element naming the same
/// attribute, plus everything a scalar copy between the two stages' own
/// `StageStorage` blocks needs. `RowCount`/`ComponentCount` are the shared
/// shape both sides agreed on; the two `FirstComponent`s may differ, since
/// each stage declares its own starting register component.
struct LinkedStageElement {
  uint32_t SourceElementID = 0;
  uint32_t DestElementID = 0;
  uint32_t SourceFirstComponent = 0;
  uint32_t DestFirstComponent = 0;
  uint32_t ComponentCount = 0;
  uint32_t RowCount = 0;
};

/// Links every \p ConsumerDir element of \p ConsumerSig accepted by
/// \p ConsumerFilter to the \p ProducerDir element of \p ProducerSig naming
/// the same attribute: the same `SignatureSystemValue` for a system-value
/// element, or the same `Location`/`Index` pair for an ordinary one.
///
/// \p StageDescription names the pair in diagnostics (e.g. "hull stage
/// output -> domain stage input"). Returns an `Error` if a consumer element
/// has no producer counterpart, has no `Location` to match on, or the two
/// disagree on component count, row count or component type -- each of
/// which would otherwise be a silently miscopied attribute.
///
/// A null \p ConsumerFilter accepts every \p ConsumerDir element.
llvm::Expected<llvm::SmallVector<LinkedStageElement, 4>> linkStageElements(
    const EntrySignature &ProducerSig, SignatureDirection ProducerDir,
    const EntrySignature &ConsumerSig, SignatureDirection ConsumerDir,
    llvm::StringRef StageDescription,
    llvm::function_ref<bool(const SignatureElement &)> ConsumerFilter = {});

/// Copies \p InvocationCount invocations' worth of every \p Links entry
/// from \p From to \p To. \p SourceInvocations, when non-empty, remaps each
/// destination invocation to its own source invocation (it must have
/// \p InvocationCount entries); when empty the two are the same index.
///
/// The remapping is what a patch draw needs: input control point `i` of
/// patch `p` is vertex-stage invocation `p * ControlPointCount + i`, not
/// invocation `i`.
void copyLinkedElements(const StageStorage &From, StageStorage &To,
                        llvm::ArrayRef<LinkedStageElement> Links,
                        uint32_t InvocationCount,
                        llvm::ArrayRef<uint32_t> SourceInvocations = {});

} // namespace feme::graphics

#endif // FEME_GRAPHICS_STAGELINK_H
