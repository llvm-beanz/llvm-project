//===- Patch.cpp - Bounded patch storage for the control stage -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Patch.h"

#include "llvm/Support/raw_ostream.h"

using namespace feme::graphics;

PatchRecord::PatchRecord(uint32_t InputControlPointCount,
                         uint32_t OutputControlPointCount,
                         uint32_t ControlPointScalarCount,
                         uint32_t PatchConstantScalarCount)
    : InputControlPointCount(InputControlPointCount),
      OutputControlPointCount(OutputControlPointCount),
      ControlPointScalarCount(ControlPointScalarCount),
      PatchConstantScalarCount(PatchConstantScalarCount),
      ControlPointData(static_cast<size_t>(OutputControlPointCount) *
                           ControlPointScalarCount,
                       0.0f),
      PatchConstantData(PatchConstantScalarCount, 0.0f) {}

bool PatchRecord::writeControlPoint(uint32_t ControlPoint, uint32_t ScalarIndex,
                                    float Value) {
  if (ControlPoint >= OutputControlPointCount ||
      ScalarIndex >= ControlPointScalarCount)
    return false;
  ControlPointData[ControlPoint * ControlPointScalarCount + ScalarIndex] =
      Value;
  return true;
}

float PatchRecord::readControlPoint(uint32_t ControlPoint,
                                    uint32_t ScalarIndex) const {
  if (ControlPoint >= OutputControlPointCount ||
      ScalarIndex >= ControlPointScalarCount)
    return 0.0f;
  return ControlPointData[ControlPoint * ControlPointScalarCount + ScalarIndex];
}

bool PatchRecord::writePatchConstant(uint32_t ScalarIndex, float Value) {
  if (ScalarIndex >= PatchConstantScalarCount)
    return false;
  PatchConstantData[ScalarIndex] = Value;
  return true;
}

float PatchRecord::readPatchConstant(uint32_t ScalarIndex) const {
  if (ScalarIndex >= PatchConstantScalarCount)
    return 0.0f;
  return PatchConstantData[ScalarIndex];
}

bool feme::graphics::validatePatchControlPointCounts(
    uint32_t InputControlPointCount, uint32_t OutputControlPointCount,
    llvm::raw_ostream *ErrOS) {
  bool Valid = true;
  auto Check = [&](uint32_t Count, const char *Which) {
    if (Count >= 1 && Count <= MaxPatchControlPoints)
      return;
    Valid = false;
    if (ErrOS)
      *ErrOS << "feme-graphics-patch: " << Which << " control point count "
             << Count << " is out of range [1, " << MaxPatchControlPoints
             << "]\n";
  };
  Check(InputControlPointCount, "input");
  Check(OutputControlPointCount, "output");
  return Valid;
}
