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
        Producer->RowCount != Consumer.RowCount ||
        Producer->ComponentType != Consumer.ComponentType)
      return createStringError(inconvertibleErrorCode(),
                               "%s: element %u and its producer element %u "
                               "disagree on component/row count or type",
                               StageDescription.str().c_str(),
                               Consumer.ElementID, Producer->ElementID);
    Links.push_back({Producer->ElementID, Consumer.ElementID,
                     Producer->FirstComponent, Consumer.FirstComponent,
                     Consumer.ComponentCount, Consumer.RowCount});
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
