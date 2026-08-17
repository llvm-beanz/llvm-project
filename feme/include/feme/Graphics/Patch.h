//===- Patch.h - Bounded patch storage for the control stage -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::PatchRecord, roadmap R34's "patch
// storage": the bounded per-patch storage "Patch and geometry wrappers" in
// feme/docs/FeMeGraphicsDesign.md describes -- "The patch wrapper allocates
// one bounded patch record, invokes the control stage with workgroup
// barrier semantics, validates tessellation factors, and hands the record
// to the tessellator."
//
// This is the host-side storage and validation only. "Workgroup barrier
// semantics" needs no new code here: `feme::cpu`'s existing groupshared/
// barrier lowering (`feme/lib/Transforms/CPU/{GroupShared,BarrierCalls}.cpp`)
// is already stage-agnostic -- it lowers `feme.cpu.barrier`/groupshared
// globals for whatever invocation group a wrapper batches, not specifically
// a `ShaderStage::Compute` one -- so a control-stage wrapper reuses it
// directly once it exists (see the design's own "reuses the compute
// workgroup/barrier ... lowering" language for the analogous mesh stage).
// Compiling a real hull/domain entry point into an invokable batch through
// that machinery, and feeding a `PatchRecord` from it into
// `feme::graphics::tessellate` (Tessellator.h), is the remaining,
// documented follow-up: this milestone's storage and validation are usable
// standalone (and unit tested standalone) ahead of it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PATCH_H
#define FEME_GRAPHICS_PATCH_H

#include "feme/Graphics/Tessellator.h"

#include <cstdint>
#include <vector>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace feme::graphics {

/// Direct3D/Vulkan's shared bound on a patch's input and output control
/// point counts.
constexpr uint32_t MaxPatchControlPoints = 32;

/// One patch's bounded storage: input/output control points, per-patch
/// constants, and the tessellation factors + normalized tessellator state
/// the control (hull) stage produces for it. Scalar values are stored
/// opaquely (as `float` bit patterns are for any component type) -- which
/// scalar of which control point/patch-constant signature element a given
/// index names is the compiled control stage's own knowledge, recorded in
/// its `EntrySignature`/`FemeStageLayout`, not this storage's concern.
///
/// A `PatchRecord` never outlives the draw work that consumes it, matching
/// "Patch records never outlive the draw work that consumes them" in
/// FeMeGraphicsDesign.md.
class PatchRecord {
public:
  /// Constructs an empty patch record for \p InputControlPointCount input
  /// and \p OutputControlPointCount output control points, each holding
  /// \p ControlPointScalarCount scalars, plus \p PatchConstantScalarCount
  /// per-patch scalars. Every count is independent of the others (an output
  /// control point count need not match the input one, matching a hull
  /// shader's own freedom to do so).
  PatchRecord(uint32_t InputControlPointCount, uint32_t OutputControlPointCount,
              uint32_t ControlPointScalarCount,
              uint32_t PatchConstantScalarCount);

  uint32_t getInputControlPointCount() const { return InputControlPointCount; }
  uint32_t getOutputControlPointCount() const {
    return OutputControlPointCount;
  }

  /// Writes scalar \p ScalarIndex of output control point \p ControlPoint.
  /// Returns false (leaving the record unmodified) if either index is out
  /// of range instead of writing out of bounds -- a hull shader's control
  /// point index and signature element layout are both validated ahead of
  /// codegen (`feme::graphics::ValidateStagePass`), but this storage layer
  /// checks independently rather than trusting that validation transitively.
  bool writeControlPoint(uint32_t ControlPoint, uint32_t ScalarIndex,
                         float Value);
  float readControlPoint(uint32_t ControlPoint, uint32_t ScalarIndex) const;

  /// Writes/reads scalar \p ScalarIndex of the per-patch constant storage.
  bool writePatchConstant(uint32_t ScalarIndex, float Value);
  float readPatchConstant(uint32_t ScalarIndex) const;

  TessFactors &getFactors() { return Factors; }
  const TessFactors &getFactors() const { return Factors; }

  TessellatorDomain Domain = TessellatorDomain::Triangle;
  TessPartitioning Partitioning = TessPartitioning::Integer;
  TessOutputPrimitive OutputPrimitive = TessOutputPrimitive::TriangleCcw;

private:
  uint32_t InputControlPointCount;
  uint32_t OutputControlPointCount;
  uint32_t ControlPointScalarCount;
  uint32_t PatchConstantScalarCount;
  /// Row-major `OutputControlPointCount` x `ControlPointScalarCount`.
  std::vector<float> ControlPointData;
  std::vector<float> PatchConstantData;
  TessFactors Factors;
};

/// Whether \p InputControlPointCount/\p OutputControlPointCount are both
/// within `[1, MaxPatchControlPoints]`, per both APIs' shared patch-size
/// limit. Reports the violation to \p ErrOS (if non-null).
bool validatePatchControlPointCounts(uint32_t InputControlPointCount,
                                     uint32_t OutputControlPointCount,
                                     llvm::raw_ostream *ErrOS = nullptr);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PATCH_H
