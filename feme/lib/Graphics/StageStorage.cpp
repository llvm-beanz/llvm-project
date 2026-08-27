//===- StageStorage.cpp - Host-owned structure-of-arrays stage storage --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/StageStorage.h"

#include "feme/Target/CPU/CompiledStage.h"

#include "llvm/Support/Error.h"

#include <algorithm>
#include <cassert>

using namespace llvm;

namespace feme::graphics {

namespace {

uint32_t scalarKindFor(SignatureComponentType Ty) {
  switch (Ty) {
  case SignatureComponentType::Float:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::Float);
  case SignatureComponentType::SInt:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::SInt);
  case SignatureComponentType::UInt:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::UInt);
  case SignatureComponentType::Bool:
    return static_cast<uint32_t>(cpu::StageLayoutScalarKind::Bool);
  }
  llvm_unreachable("unhandled SignatureComponentType");
}

} // namespace

Expected<EntrySignature> getStageSignature(const cpu::CompiledStage &Stage) {
  std::vector<uint8_t> Bytes = Stage.getArtifactInfo().Signature;
  if (Bytes.empty())
    return createStringError(inconvertibleErrorCode(),
                             "compiled stage has no attached signature "
                             "metadata; the executor cannot bind its "
                             "inputs/outputs");
  return parseSignature(Bytes);
}

Expected<StageStorage> buildStageStorage(const EntrySignature &Sig,
                                         SignatureDirection Direction,
                                         uint32_t InvocationCount) {
  StageStorage Storage;
  Storage.InvocationCount = InvocationCount;
  uint32_t MaxID = 0;
  bool Any = false;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.Direction != Direction)
      continue;
    Any = true;
    MaxID = std::max(MaxID, Elt.ElementID);
  }
  if (!Any)
    return Storage;

  Storage.Elements.assign(MaxID + 1, cpu::FemeStageElement{});
  uint64_t Offset = 0;
  for (const SignatureElement &Elt : Sig.Elements) {
    if (Elt.Direction != Direction)
      continue;

    cpu::FemeStageElement &E = Storage.Elements[Elt.ElementID];
    E.ElementID = Elt.ElementID;
    E.FirstComponent = Elt.FirstComponent;
    E.ComponentCount = Elt.ComponentCount;
    E.RowCount = Elt.RowCount;
    E.Interpolation = static_cast<uint32_t>(Elt.Interpolation);
    E.Frequency = static_cast<uint32_t>(Elt.Frequency);
    E.SystemValue = static_cast<uint32_t>(Elt.SystemValue);
    if (Elt.SystemValue != SignatureSystemValue::None)
      E.Flags |= cpu::FEME_STAGE_ELEMENT_SYSTEM_VALUE;
    // A system-value *input* is sourced from the invocation record by the
    // compiled wrapper, not from this layout's `DataOffset` (see
    // VertexWrapper.cpp/FragmentWrapper.cpp's `lowerVertexInputLoad`/
    // `lowerFragmentInputLoad`), so it needs no storage. An output is
    // always written through stage storage regardless of `SystemValue`
    // (e.g. `SV_Position` -- see `lowerVertexOutputStore`), so only skip
    // allocating storage for an input.
    if (Elt.SystemValue != SignatureSystemValue::None &&
        Direction == SignatureDirection::Input)
      continue;
    if (Elt.BitWidth != 32)
      return createStringError(inconvertibleErrorCode(),
                               "stage element %u has a %u-bit scalar; only "
                               "32-bit elements are implemented yet",
                               Elt.ElementID, Elt.BitWidth);

    E.ScalarKind = scalarKindFor(Elt.ComponentType);
    E.BitWidth = 32;
    E.InvocationStride = 4;
    E.ComponentStride = InvocationCount * 4;
    E.RowStride = E.ComponentStride * Elt.ComponentCount;
    E.DataOffset = Offset;
    Offset += (uint64_t)Elt.RowCount * Elt.ComponentCount * InvocationCount * 4;
  }
  Storage.Data.assign(Offset, 0);
  return Storage;
}

void appendStageInvocations(const StageStorage &From, StageStorage &To,
                            uint32_t DestBase) {
  assert(From.Elements.size() == To.Elements.size() &&
         "appendStageInvocations needs two blocks of the same signature");
  for (const cpu::FemeStageElement &E : From.Elements) {
    if (E.BitWidth == 0)
      continue;
    for (uint32_t Row = 0; Row != E.RowCount; ++Row)
      for (uint32_t C = 0; C != E.ComponentCount; ++C)
        for (uint32_t I = 0; I != From.InvocationCount; ++I)
          To.writeRaw(E.ElementID, E.FirstComponent + C, DestBase + I,
                      From.readRaw(E.ElementID, E.FirstComponent + C, I, Row),
                      Row);
  }
}

const SignatureElement *findElement(const EntrySignature &Sig,
                                    SignatureDirection Direction,
                                    SignatureSystemValue SysVal) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Direction && Elt.SystemValue == SysVal)
      return &Elt;
  return nullptr;
}

const SignatureElement *findElementByLocation(const EntrySignature &Sig,
                                              SignatureDirection Direction,
                                              uint32_t Location,
                                              uint32_t Index) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Direction &&
        Elt.SystemValue == SignatureSystemValue::None &&
        Elt.Location == Location && Elt.Index == Index)
      return &Elt;
  return nullptr;
}

} // namespace feme::graphics
