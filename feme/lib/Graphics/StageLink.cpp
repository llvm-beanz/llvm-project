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
                               Consumer.Index, Consumer.FirstComponent);
}

/// (Roadmap H9b/L94(h)) \p Elt's own real per-vertex-invocation row shape,
/// for comparing/linking against another stage's element. Before L94(h),
/// `CanonicalizeStage.cpp`'s `addElements` folded a per-vertex-arrayed
/// `Input` element's own outer per-vertex array dimension directly into
/// `RowCount` (leaving `RowCountIsVertexArray` as the only way to tell
/// that dimension apart from a real matrix's row count), so this function
/// unconditionally folded it back down to `1` before comparing/copying --
/// correct only by coincidence, since every ordinary per-vertex varying's
/// own real (per-vertex) shape happened to already be a single row.
/// L94(h) fixed the actual bug at its source instead: `addElements` now
/// peels that same per-vertex dimension off before computing `RowCount`
/// at all (mirroring how `resolveOffsetWithinElement`'s own
/// `Vertex`-threading path, and `StageStorage::readRaw`/`writeRaw`'s
/// separate `Invocation` parameter, already address it apart from `Row`),
/// so `RowCount` is always this element's own real per-vertex shape now
/// -- including a genuinely arrayed one, e.g. `RowCount == 3` for
/// `layout(location=1) in float looseVar[3];` -- needing no folding here
/// at all. `RowCountIsVertexArray` remains meaningful only as a "this
/// element's own per-invocation copies come from `copyLinkedElements`'s
/// own `SourceInvocations` remapping, not a same-invocation multi-row
/// copy" marker, nothing this function itself still needs to special-case.
uint32_t effectiveRowCount(const SignatureElement &Elt) { return Elt.RowCount; }

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
