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
    if (!Producer) {
      // (roadmap L238) A *system-value* consumer (e.g. `Position`) with
      // no producer counterpart is legal, not a link failure: per the
      // Vulkan spec, "Any input value that does not have a matching
      // output value is undefined" -- this is precisely `gl_PerVertex`'s
      // whole point (writing `gl_Position`/`gl_PointSize`/
      // `gl_ClipDistance`/`gl_CullDistance` is always optional in every
      // non-final pre-rasterization stage). Record a producer-less link
      // instead of erroring; `copyLinkedElements` writes a deterministic
      // zero for it. An ordinary, `Location`-addressed consumer with no
      // producer remains a hard error below -- unchanged.
      if (Consumer.SystemValue != SignatureSystemValue::None) {
        Links.push_back({0, Consumer.ElementID, 0, Consumer.FirstComponent,
                         Consumer.ComponentCount, effectiveRowCount(Consumer),
                         /*HasProducer=*/false});
        continue;
      }
      return createStringError(inconvertibleErrorCode(),
                               "%s: element %u has no matching producer "
                               "element",
                               StageDescription.str().c_str(),
                               Consumer.ElementID);
    }
    // (roadmap L94(i)) `VK_KHR_maintenance4` (core since Vulkan 1.3, always
    // implemented here per roadmap E4) explicitly allows a consumer's
    // input variable to declare *fewer* vector components than its
    // producer's matching output: only the consumer's own leading
    // components are read, the producer's extra trailing ones are simply
    // dropped. A consumer that declares *more* components than its
    // producer wrote remains invalid -- there is nothing to read them
    // from -- so only that direction of mismatch is still rejected here.
    if (Consumer.ComponentCount > Producer->ComponentCount ||
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
      // (roadmap L238) A producer-less system-value link
      // (`LinkedStageElement::HasProducer == false`) has no source
      // element to read at all -- write a deterministic zero (the
      // Vulkan spec leaves the actual value undefined) instead of
      // indexing into `From`.
      if (!Link.HasProducer) {
        for (uint32_t Row = 0; Row != Link.RowCount; ++Row)
          for (uint32_t C = 0; C != Link.ComponentCount; ++C)
            To.writeRaw(Link.DestElementID, Link.DestFirstComponent + C,
                        Invocation, 0, Row);
        continue;
      }
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

void copyLinkedPatchFrequencyElements(const StageStorage &From,
                                     StageStorage &To,
                                     ArrayRef<LinkedStageElement> Links,
                                     uint32_t SourceInvocationCount,
                                     uint32_t DestInvocationCount) {
  for (const LinkedStageElement &Link : Links) {
    // (Roadmap L345) `Link.RowCount` is NOT always equal to
    // `SourceInvocationCount`: `getStageIORowShape` (`CanonicalizeStage.cpp`)
    // flattens a matrix-typed `PerPatch` array element's own row/column
    // dimension into the same `RowCount` it computes for the array's own
    // instance dimension (e.g. a `mat4x3[OUTPUT_PATCH_SIZE]` array's
    // `RowCount` is `OUTPUT_PATCH_SIZE * 4`, not just `OUTPUT_PATCH_SIZE`).
    // `RowsPerInvocation` is how many of those flattened rows belong to a
    // single producing invocation's own contiguous block (1 for the plain
    // scalar/vector case this function originally only handled, matching
    // the previous unconditional `SourceInvocation == Row`).
    uint32_t RowsPerInvocation =
        SourceInvocationCount != 0 ? Link.RowCount / SourceInvocationCount : 0;
    assert((SourceInvocationCount == 0 ||
            Link.RowCount % SourceInvocationCount == 0) &&
           "Link.RowCount must be an exact multiple of SourceInvocationCount: "
           "every producing invocation's own block of flattened rows must be "
           "the same size");
    for (uint32_t Row = 0; Row != Link.RowCount; ++Row)
      for (uint32_t C = 0; C != Link.ComponentCount; ++C) {
        // `Row / RowsPerInvocation` identifies which producing invocation's
        // own per-invocation storage copy holds the real (non-garbage)
        // value at this flattened `Row` (the diagonal `HullWrapper.cpp`'s
        // `lowerHullOutputStore` leaves behind -- see this function's own
        // header comment); every other row of that same source invocation's
        // own copy was never written, so only this invocation's own
        // contiguous row block is ever read.
        uint32_t SourceInvocation =
            Link.HasProducer && RowsPerInvocation != 0 &&
                    Row / RowsPerInvocation < SourceInvocationCount
                ? Row / RowsPerInvocation
                : 0;
        uint32_t Value =
            Link.HasProducer
                ? From.readRaw(Link.SourceElementID,
                               Link.SourceFirstComponent + C,
                               SourceInvocation, Row)
                : 0;
        for (uint32_t Invocation = 0; Invocation != DestInvocationCount;
            ++Invocation)
          To.writeRaw(Link.DestElementID, Link.DestFirstComponent + C,
                      Invocation, Value, Row);
      }
  }
}

} // namespace feme::graphics
