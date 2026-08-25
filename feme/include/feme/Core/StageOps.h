//===- StageOps.h - Canonical `feme.stage.*` operation family --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the `feme.stage.*` canonical stage operation family
// described by the "Canonical stage operations" section of
// feme/docs/FeMeGraphicsDesign.md: a source-independent vocabulary for
// vertex/fragment-stage input/output access, discard/demote, helper-lane
// query, screen-space derivatives, quad reads, and pull-model interpolation.
//
// LLVM has no intrinsic for these yet, so -- as the design text notes --
// they are represented as ordinary named calls, the same shape DXIL's own
// `dx.op.*` calling convention uses, until an upstream intrinsic exists.
// Each operation is identified by a `feme.stage.*` callee name, optionally
// suffixed with a type-mangling string (`getOrInsertStageOp`) for the
// operations overloaded on a value type, exactly as `dx.op.*` calls are
// (e.g. `dx.op.loadInput.f32`).
//
// This started as roadmap R20, scoped to the vertex and fragment stages:
// "Only operations required by implemented stages are legal" (patch,
// stream-emission, mesh-output and ray operations were later milestones).
// Roadmap R34 adds the geometry stage's `StreamEmit`/`StreamCut`; patch
// input/output access reuses the existing `InputLoad`/`OutputStore` ops
// (a hull/domain stage's control-point/patch-constant elements are
// ordinary signature elements, distinguished by `SignatureDirection`/
// `SignatureFrequency`, not a separate op family). Mesh-output and ray
// operations remain later milestones.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_STAGEOPS_H
#define FEME_CORE_STAGEOPS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/InstrTypes.h"

#include <cstdint>
#include <optional>

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
class Type;
class Value;
} // namespace llvm

namespace feme {

/// One operation in the `feme.stage.*` family. Every enumerator has exactly
/// one `feme.stage.*` callee-name spelling (`getStageOpName`).
enum class StageOpKind : uint8_t {
  /// `feme.stage.input.load(element, row, component, vertex)`: reads one
  /// scalar component of a signature input element.
  InputLoad,
  /// `feme.stage.output.store(element, row, component, value, vertex)`:
  /// writes one scalar component of a signature output element.
  OutputStore,
  /// `feme.stage.discard(condition)`: kills the invocation when
  /// \c condition is true, clearing both its live and side-effect masks.
  Discard,
  /// `feme.stage.demote(condition)`: demotes the invocation to a helper
  /// lane when \c condition is true, clearing only its side-effect mask
  /// (SPIR-V's `OpDemoteToHelperInvocation`; DXIL has no separate op, since
  /// its `discard` already only ever clears live state a hull/hardware
  /// rasterizer still needs for derivatives).
  Demote,
  /// `feme.stage.is_helper()`: whether the invocation is (already, or now)
  /// a helper lane.
  IsHelper,
  /// `feme.stage.derivative.x.fine(value)` / `.y.fine` / `.x.coarse` /
  /// `.y.coarse`: screen-space partial derivatives.
  DerivativeXFine,
  DerivativeYFine,
  DerivativeXCoarse,
  DerivativeYCoarse,
  /// `feme.stage.quad.read(value, direction)`: reads \c value from another
  /// invocation in the same 2x2 quad, per \c direction (0 = across X, 1 =
  /// across Y, 2 = across the diagonal -- matching DXIL's `QuadOp`
  /// direction encoding).
  QuadRead,
  /// `feme.stage.interpolate.at.centroid(element, component)`: the pull
  /// model's evaluate-at-centroid (HLSL `EvaluateAttributeCentroid`,
  /// SPIR-V `InterpolateAtCentroid`).
  InterpolateAtCentroid,
  /// `feme.stage.interpolate.at.sample(element, component, sample)`:
  /// evaluate-at-sample (`EvaluateAttributeAtSample`/`InterpolateAtSample`).
  InterpolateAtSample,
  /// `feme.stage.interpolate.at.offset(element, component, offsetX,
  /// offsetY)`: evaluate-at-offset (`EvaluateAttributeSnapped`/
  /// `InterpolateAtOffset`). The offset is two scalar operands rather than
  /// one vector, matching how the other operands here are already
  /// scalarized (mirroring DXIL's `EvalSnapped`, whose two offset operands
  /// are likewise separate `i32`s, not a packed vector).
  InterpolateAtOffset,
  /// `feme.stage.stream.emit(stream)`: the geometry stage's `emit`
  /// operation -- appends a snapshot of the current output signature values
  /// to output stream \c stream as one vertex record (see "Tessellation and
  /// geometry stage model" in feme/docs/FeMeGraphicsDesign.md and
  /// `feme::graphics::GeometryStreamBuilder`). Side-effecting even when no
  /// framebuffer write occurs.
  StreamEmit,
  /// `feme.stage.stream.cut(stream)`: the geometry stage's `cut` operation
  /// -- terminates the current primitive strip on output stream \c stream
  /// without emitting a vertex.
  StreamCut,
  /// `feme.stage.subpass.load(attachment_index, component)`: roadmap F8a's
  /// dynamic-rendering-local-read consumption of a SPIR-V `subpassInput`
  /// (`OpTypeImage` with `Dim::SubpassData`, read through `OpImageRead`'s
  /// subpass-local form). Reads one scalar component of the
  /// currently-bound color/depth/stencil render-target attachment mapped to
  /// \c attachment_index at the invocation's own fragment location -- not a
  /// descriptor-set image, and per-lane always `f32` -- like `InputLoad`,
  /// it is marked overloaded (StageOps.cpp's table) purely so
  /// `feme::cpu::SIMDizePass`'s widened `<W x f32>` form gets a distinct
  /// symbol from the scalar declaration `feme::spirv::SubpassLoadPattern`
  /// (SPIRVToLLVMPatterns.cpp) creates, not because a real shader ever
  /// requests a different result type (see FragmentWrapper.cpp's
  /// `lowerFragmentSubpassLoad`).
  SubpassLoad,
  // Keep last: the number of stage op kinds, for range checks.
  NumStageOpKinds,
};

/// Whether \p Kind's value operand/result participates in the type-mangling
/// suffix `getOrInsertStageOp` appends (true for every op with a generic
/// "value" operand or result; false for the fixed-signature ops --
/// `Discard`, `Demote`, `IsHelper` -- that never vary by type).
bool isStageOpKindOverloaded(StageOpKind Kind);

/// The `feme.stage.*` callee-name prefix for \p Kind, before any
/// `getOrInsertStageOp` type-mangling suffix.
llvm::StringRef getStageOpName(StageOpKind Kind);

/// The stage op \p F's name identifies, or `std::nullopt` if \p F's name is
/// not one of `getStageOpName`'s spellings (e.g. \p F is not a
/// `feme.stage.*` declaration at all).
std::optional<StageOpKind> getStageOpKind(const llvm::Function &F);

/// Whether \p CI calls a `feme.stage.*` function, and (if \p Kind is
/// non-null) which one.
bool isStageOpCall(const llvm::CallInst &CI, StageOpKind *Kind = nullptr);

/// Declares (or finds an existing declaration of) the `feme.stage.*`
/// function for \p Kind with the given result/argument types. Two calls
/// with a different value-operand/result type (e.g. an `i32` vs. a `float`
/// input load) get distinct declarations, mangled by
/// `isStageOpKindOverloaded`'s type -- the same reason `dx.op.*` calls are
/// themselves per-type overloaded (e.g. `dx.op.loadInput.f32`).
llvm::FunctionCallee getOrInsertStageOp(llvm::Module &M, StageOpKind Kind,
                                        llvm::Type *ResultTy,
                                        llvm::ArrayRef<llvm::Type *> ArgTys);

/// \name Builders
///
/// One builder per `StageOpKind`, each taking the operation's operands in
/// the order `StageOpKind`'s own comment documents. `ElementID`/`Row`/
/// `Component`/`Vertex`/`Sample` are ordinary `i32` values (typically, but
/// not necessarily, constants -- `feme::graphics::ValidateStagePass`
/// diagnoses a non-constant one rather than these builders rejecting it).
///@{
llvm::CallInst *createStageInputLoad(llvm::IRBuilderBase &B,
                                     llvm::Type *ResultTy, uint32_t ElementID,
                                     llvm::Value *Row, llvm::Value *Component,
                                     llvm::Value *Vertex,
                                     const llvm::Twine &Name = "");

llvm::CallInst *createStageOutputStore(llvm::IRBuilderBase &B,
                                       uint32_t ElementID, llvm::Value *Row,
                                       llvm::Value *Component, llvm::Value *Val,
                                       llvm::Value *Vertex);

llvm::CallInst *createStageDiscard(llvm::IRBuilderBase &B,
                                   llvm::Value *Condition);

llvm::CallInst *createStageDemote(llvm::IRBuilderBase &B,
                                  llvm::Value *Condition);

llvm::CallInst *createStageIsHelper(llvm::IRBuilderBase &B);

/// \p Kind must be one of the four `Derivative*` kinds.
llvm::CallInst *createStageDerivative(llvm::IRBuilderBase &B, StageOpKind Kind,
                                      llvm::Value *Val);

llvm::CallInst *createStageQuadRead(llvm::IRBuilderBase &B, llvm::Value *Val,
                                    uint8_t Direction);

llvm::CallInst *createStageInterpolateAtCentroid(llvm::IRBuilderBase &B,
                                                 llvm::Type *ResultTy,
                                                 uint32_t ElementID,
                                                 llvm::Value *Component);

llvm::CallInst *createStageInterpolateAtSample(llvm::IRBuilderBase &B,
                                               llvm::Type *ResultTy,
                                               uint32_t ElementID,
                                               llvm::Value *Component,
                                               llvm::Value *Sample);

llvm::CallInst *
createStageInterpolateAtOffset(llvm::IRBuilderBase &B, llvm::Type *ResultTy,
                               uint32_t ElementID, llvm::Value *Component,
                               llvm::Value *OffsetX, llvm::Value *OffsetY);

/// `feme.stage.stream.emit(stream)`, where \p Stream is the output stream
/// index (an ordinary `i32` constant, typically 0 unless the geometry stage
/// declares multiple output streams).
llvm::CallInst *createStageStreamEmit(llvm::IRBuilderBase &B,
                                      uint32_t Stream);

/// `feme.stage.stream.cut(stream)`.
llvm::CallInst *createStageStreamCut(llvm::IRBuilderBase &B, uint32_t Stream);

/// `feme.stage.subpass.load(attachment_index, component)`; both operands
/// are `i32` compile-time constants (see `StageOpKind::SubpassLoad`'s
/// comment). Always returns `f32`.
llvm::CallInst *createStageSubpassLoad(llvm::IRBuilderBase &B,
                                       uint32_t AttachmentIndex,
                                       uint32_t Component);
///@}

/// Reads back \p CI's operand \p Idx as a constant `i32`/`i8`/`i1`, or
/// `std::nullopt` if it is not a constant integer (a non-constant element
/// ID, row, or component is a validation failure, not a crash -- see
/// `feme::graphics::ValidateStagePass`).
std::optional<uint64_t> getStageOpConstantOperand(const llvm::CallInst &CI,
                                                  unsigned Idx);

} // namespace feme

#endif // FEME_CORE_STAGEOPS_H
