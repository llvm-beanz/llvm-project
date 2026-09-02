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

#include <cassert>
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
  /// How many invocations `Data` was sized for, i.e. the valid range of
  /// every accessor's `Invocation` argument. One for a per-patch
  /// (`PatchInput`/`PatchOutput`) block.
  uint32_t InvocationCount = 0;

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
    assert(Off + sizeof(uint32_t) <= Data.size() &&
           "StageStorage::readRaw: computed offset is out of bounds -- a "
           "stale ElementID/Component/Invocation/Row, or a producer/"
           "consumer signature mismatch, would read past the storage "
           "buildStageStorage sized for this element");
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
    assert(Off + sizeof(uint32_t) <= Data.size() &&
           "StageStorage::writeRaw: computed offset is out of bounds -- a "
           "stale ElementID/Component/Invocation/Row, or a producer/"
           "consumer signature mismatch, would write past the storage "
           "buildStageStorage sized for this element");
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
/// sources it from its invocation record rather than from `DataOffset` --
/// *unless* \p AllInputSystemValuesAreStorageBacked (roadmap H5h), for a
/// geometry stage's own `gl_in[]`-shaped input signature: every one of its
/// system-value members (`gl_in[].gl_Position`/`gl_PointSize`/
/// `gl_ClipDistance`/`gl_CullDistance`, but not `PrimitiveID`/
/// `InvocationID`, which the wrapper still sources from
/// `FemeGeometryInvocation` regardless) is addressed with a genuinely
/// dynamic per-vertex-in-primitive index (`GeometryWrapper.cpp`'s
/// `lowerGeometryInputLoad`), unlike every other stage's own fixed-field
/// system values, so it always needs real storage.
///
/// A per-patch direction (`PatchInput`/`PatchOutput`) is exactly the
/// \p InvocationCount == 1 case: one patch's worth of storage, addressed by
/// row/component alone.
llvm::Expected<StageStorage>
buildStageStorage(const EntrySignature &Sig, SignatureDirection Direction,
                  uint32_t InvocationCount,
                  bool AllInputSystemValuesAreStorageBacked = false);

/// Copies every element of \p From's invocations `[0, From.InvocationCount)`
/// into \p To's invocations starting at \p DestBase. Both blocks must have
/// been built by `buildStageStorage` from the same signature and direction,
/// so their `Elements` tables agree on everything but their invocation
/// strides; this is a per-scalar copy rather than a `memcpy` precisely
/// because those strides differ (the layout is
/// row-major-then-component-major with the invocation index innermost).
///
/// This is what concatenating many patches' domain-stage outputs into one
/// flat rasterizable block needs (roadmap H4, `feme::graphics::
/// executeDraws`), where each patch's own point count is only known after
/// its tessellation factors have been computed.
void appendStageInvocations(const StageStorage &From, StageStorage &To,
                            uint32_t DestBase);

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
