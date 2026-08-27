//===- StageStorage.h - Host-owned structure-of-arrays stage storage -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares `feme::graphics::StageStorage`, the host-owned
// structure-of-arrays block one compiled stage's `Inputs`/`Outputs` pointer
// addresses, plus the `feme::cpu::FemeStageLayout` describing it. It was
// `feme::graphics::executeDraws`'s own private helper first (roadmap R32);
// it is shared here because every stage chained behind the vertex stage
// (the hull control-point phase, the patch-constant phase, the domain
// stage) needs exactly the same "build one stage's storage from its own
// `EntrySignature`" step.
//
// Scope: 32-bit scalars only, matching the executor's own long-standing
// restriction -- `buildStageStorage` returns an `Error` for anything else
// rather than laying out storage the compiled wrapper would misread.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_STAGESTORAGE_H
#define FEME_GRAPHICS_STAGESTORAGE_H

#include "feme/Core/Signature.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/Support/Error.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace feme::cpu {
class CompiledStage;
} // namespace feme::cpu

namespace feme::graphics {

/// Host-owned structure-of-arrays storage for one stage's input or output
/// block (`FemeVertexArgs::Inputs`/`Outputs`, or any other stage's
/// equivalent), plus the dense `FemeStageLayout` describing it. A multi-row
/// (matrix) element's rows are addressed with \p Row, which defaults to 0
/// for every scalar/vector (`RowCount == 1`) caller.
struct StageStorage {
  /// Dense `ElementID` -> `FemeStageElement` table (see `FemeStageLayout`).
  std::vector<cpu::FemeStageElement> Elements;
  /// The raw bytes `Inputs`/`Outputs` points at.
  std::vector<uint8_t> Data;

  /// The `FemeStageLayout` view of `Elements`. Only valid while this
  /// object is alive and `Elements` is not resized.
  cpu::FemeStageLayout layout() const {
    cpu::FemeStageLayout L{};
    L.Elements = Elements.data();
    L.ElementCount = static_cast<uint32_t>(Elements.size());
    return L;
  }

  uint32_t readRaw(uint32_t ElementID, uint32_t Component, uint32_t Invocation,
                   uint32_t Row = 0) const {
    const cpu::FemeStageElement &E = Elements[ElementID];
    uint64_t Off =
        E.DataOffset + (uint64_t)Row * E.RowStride +
        (uint64_t)(Component - E.FirstComponent) * E.ComponentStride +
        (uint64_t)Invocation * E.InvocationStride;
    uint32_t V;
    std::memcpy(&V, Data.data() + Off, sizeof(uint32_t));
    return V;
  }

  void writeRaw(uint32_t ElementID, uint32_t Component, uint32_t Invocation,
                uint32_t Value, uint32_t Row = 0) {
    const cpu::FemeStageElement &E = Elements[ElementID];
    uint64_t Off =
        E.DataOffset + (uint64_t)Row * E.RowStride +
        (uint64_t)(Component - E.FirstComponent) * E.ComponentStride +
        (uint64_t)Invocation * E.InvocationStride;
    std::memcpy(Data.data() + Off, &Value, sizeof(uint32_t));
  }

  float readFloat(uint32_t ElementID, uint32_t Component, uint32_t Invocation,
                  uint32_t Row = 0) const {
    uint32_t Bits = readRaw(ElementID, Component, Invocation, Row);
    float F;
    std::memcpy(&F, &Bits, sizeof(float));
    return F;
  }

  void writeFloat(uint32_t ElementID, uint32_t Component, uint32_t Invocation,
                  float Value, uint32_t Row = 0) {
    uint32_t Bits;
    std::memcpy(&Bits, &Value, sizeof(float));
    writeRaw(ElementID, Component, Invocation, Bits, Row);
  }
};

/// Reads \p Stage's serialized `feme::EntrySignature` metadata, or an
/// `Error` if it has none -- every stage the executor runs must have been
/// imported/authored with its signature attached (roadmap R17/R18).
llvm::Expected<EntrySignature>
getStageSignature(const cpu::CompiledStage &Stage);

/// Builds a `StageStorage` for every \p Direction-matching element of
/// \p Sig, sized for \p InvocationCount invocations. A system-value
/// *input* element gets a dense `FemeStageElement` entry (so the layout
/// stays dense by `ElementID`) but no storage, since the compiled wrapper
/// sources it from its invocation record rather than from `DataOffset`.
///
/// A per-patch direction (`PatchInput`/`PatchOutput`) is exactly the
/// \p InvocationCount == 1 case: one patch's worth of storage, addressed by
/// row/component alone.
llvm::Expected<StageStorage> buildStageStorage(const EntrySignature &Sig,
                                               SignatureDirection Direction,
                                               uint32_t InvocationCount);

/// The first \p Direction element of \p Sig naming system value \p SysVal,
/// or null if it declares none.
const SignatureElement *findElement(const EntrySignature &Sig,
                                    SignatureDirection Direction,
                                    SignatureSystemValue SysVal);

/// The first non-system-value \p Direction element of \p Sig at
/// \p Location/\p Index, or null if it declares none.
const SignatureElement *findElementByLocation(const EntrySignature &Sig,
                                              SignatureDirection Direction,
                                              uint32_t Location,
                                              uint32_t Index = 0);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_STAGESTORAGE_H
