//===- StageLink.cpp - Cross-stage attribute linking ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/StageLink.h"

#include "llvm/Support/Error.h"

using namespace llvm;

namespace feme::graphics {

namespace {

/// The \p ProducerDir element of \p Sig naming the same attribute as
/// \p Consumer: system values match by `SignatureSystemValue`, everything
/// else by `Location`/`Index`.
const SignatureElement *findProducer(const EntrySignature &Sig,
                                     SignatureDirection ProducerDir,
                                     const SignatureElement &Consumer) {
  if (Consumer.SystemValue != SignatureSystemValue::None)
    return findElement(Sig, ProducerDir, Consumer.SystemValue);
  if (!Consumer.Location)
    return nullptr;
  return findElementByLocation(Sig, ProducerDir, *Consumer.Location,
                               Consumer.Index);
}

/// (Roadmap H9b) \p Elt's own real per-vertex-invocation row shape, for
/// comparing/linking against another stage's element: `RowCount` alone
/// conflates two structurally-identical shapes `CanonicalizeStage.cpp`'s
/// `addElements` can produce for an `Input`-direction element (see
/// `SignatureElement::RowCountIsVertexArray`'s own comment) -- a real
/// matrix's row count (meaningful across a cross-stage link, e.g. an
/// `mat3` varying), or a geometry/hull/domain entry's own per-vertex
/// array extent (e.g. 3 for a triangle's `gl_in[]`), which is not: that
/// dimension is a genuinely different attribute-copy semantics
/// (`feme::graphics::executeDraws`'s own per-vertex expansion into
/// separate producer invocations via `copyLinkedElements`'s
/// `SourceInvocations` remapping, mirrored by `PatchPipeline.cpp`'s own
/// per-control-point remapping -- see `copyLinkedElements`'s own file
/// comment), not a same-invocation multi-row copy. Every producer this
/// element could ever link against (an ordinary, unarrayed vertex/domain
/// stage output) describes only the single vertex its own invocation
/// produced, i.e. `RowCount == 1` in this same sense, so folding a
/// per-vertex array's own extent into the comparison/copy below wrongly
/// disagrees with that producer's genuine `RowCount == 1` -- exactly the
/// `vkQueueSubmit`-time "disagree on component/row count or type"
/// mismatch this row fixes.
uint32_t effectiveRowCount(const SignatureElement &Elt) {
  return Elt.RowCountIsVertexArray ? 1 : Elt.RowCount;
}

} // namespace

Expected<SmallVector<LinkedStageElement, 4>>
linkStageElements(const EntrySignature &ProducerSig,
                  SignatureDirection ProducerDir,
                  const EntrySignature &ConsumerSig,
                  SignatureDirection ConsumerDir, StringRef StageDescription,
                  function_ref<bool(const SignatureElement &)> ConsumerFilter) {
  SmallVector<LinkedStageElement, 4> Links;
  for (const SignatureElement &Consumer : ConsumerSig.Elements) {
    if (Consumer.Direction != ConsumerDir)
      continue;
    if (ConsumerFilter && !ConsumerFilter(Consumer))
      continue;
    if (Consumer.SystemValue == SignatureSystemValue::None &&
        !Consumer.Location)
      return createStringError(inconvertibleErrorCode(),
                               "%s: element %u has no location to link "
                               "against",
                               StageDescription.str().c_str(),
                               Consumer.ElementID);
    const SignatureElement *Producer =
        findProducer(ProducerSig, ProducerDir, Consumer);
    if (!Producer)
      return createStringError(inconvertibleErrorCode(),
                               "%s: element %u has no matching producer "
                               "element",
                               StageDescription.str().c_str(),
                               Consumer.ElementID);
    if (Producer->ComponentCount != Consumer.ComponentCount ||
        effectiveRowCount(*Producer) != effectiveRowCount(Consumer) ||
        Producer->ComponentType != Consumer.ComponentType)
      return createStringError(inconvertibleErrorCode(),
                               "%s: element %u and its producer element %u "
                               "disagree on component/row count or type",
                               StageDescription.str().c_str(),
                               Consumer.ElementID, Producer->ElementID);
    Links.push_back({Producer->ElementID, Consumer.ElementID,
                     Producer->FirstComponent, Consumer.FirstComponent,
                     Consumer.ComponentCount, effectiveRowCount(Consumer)});
  }
  return Links;
}

void copyLinkedElements(const StageStorage &From, StageStorage &To,
                        ArrayRef<LinkedStageElement> Links,
                        uint32_t InvocationCount,
                        ArrayRef<uint32_t> SourceInvocations) {
  assert((SourceInvocations.empty() ||
          SourceInvocations.size() == InvocationCount) &&
         "a source-invocation remapping must cover every destination "
         "invocation");
  for (const LinkedStageElement &Link : Links)
    for (uint32_t Invocation = 0; Invocation != InvocationCount; ++Invocation) {
      uint32_t Source = SourceInvocations.empty()
                            ? Invocation
                            : SourceInvocations[Invocation];
      for (uint32_t Row = 0; Row != Link.RowCount; ++Row)
        for (uint32_t C = 0; C != Link.ComponentCount; ++C)
          To.writeRaw(Link.DestElementID, Link.DestFirstComponent + C,
                      Invocation,
                      From.readRaw(Link.SourceElementID,
                                   Link.SourceFirstComponent + C, Source, Row),
                      Row);
    }
}

} // namespace feme::graphics
