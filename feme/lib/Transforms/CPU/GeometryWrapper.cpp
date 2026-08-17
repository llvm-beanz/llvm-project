//===- GeometryWrapper.cpp - CPU target geometry stage wrapper -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap R34's continuation, closing its last remaining "wrapper" open
// item (see DomainWrapper.cpp's and HullWrapper.cpp's file comments and
// agent_thoughts.md's prior sessions): compiling a real geometry entry point
// through the CPU lowering pipeline into an invokable `feme::cpu::
// CompiledStage` batch.
//
// A geometry shader is, in its wrapper shape, a vertex shader batched over
// assembled *primitives* rather than vertices: one independent invocation
// per input primitive, batched over `FemeGeometryArgs::PrimitiveCount` in
// waves of `<W x T>` exactly the way `feme::cpu::VertexWrapperPass` batches
// vertices. Two things are new relative to every earlier stage's wrapper:
//
//  - **Structure-of-arrays input over primitives, not vertices.** A
//    primitive has `FemeGeometryArgs::VerticesPerPrimitive` vertices (3 for
//    an ordinary triangle, 6 for a triangle-with-adjacency, and so on), and
//    `feme.stage.input.load`'s vertex-in-primitive operand may legally name
//    *any* of them -- unlike `HullWrapperPass`'s control-point phase (which
//    restricts a control point to its own input), a geometry shader
//    legitimately reads more than one input vertex (an adjacency triangle's
//    "opposite" vertices, for instance). `lowerGeometryInputLoad` therefore
//    places no restriction on that operand, addressing a flat
//    `primitiveIndex * VerticesPerPrimitive + vertexInPrimitive` slot of
//    `Inputs`, mirroring how `PatchConstantWrapperPass`/`DomainWrapperPass`
//    already read "any control point" of their own structure-of-arrays
//    blocks.
//  - **Bounded, variable-count output via `emit`/`cut` rather than one fixed
//    per-invocation result.** `feme.stage.output.store` still writes
//    ordinary per-invocation scratch storage (`Outputs`/`OutputLayout`,
//    addressed exactly like a vertex batch's own), but that storage is not
//    itself the stage's result: `feme.stage.stream.emit`
//    (`StageOpKind::StreamEmit`) is what snapshots the scratch storage's
///   *current* values into one bounded output record, and
//    `feme.stage.stream.cut` (`StageOpKind::StreamCut`) closes the strip
//    currently accumulating. `lowerGeometryStreamEmit`/`lowerGeometryStreamCut`
//    below record that record/strip-boundary sequence into three flat,
//    host-owned arrays (`FemeGeometryArgs::EmittedVertices`/
//    `EmittedVertexCounts`/`StripEndsAfter`) rather than calling back into a
//    live `feme::graphics::GeometryStreamBuilder` object from JIT-compiled
//    code (there is no existing precedent in this codebase for JIT-compiled
//    shader code calling back into an arbitrary host C++ object -- every
//    other stage's storage is flat, host-allocated memory the compiled code
//    only ever loads from or stores to). `feme::cpu::collectGeometryStreams`
//    (ResourceHeap.h/.cpp) is the host-side code that replays those flat
//    arrays, one real `GeometryStreamBuilder` per primitive, and merges them
//    via the already-tested `feme::graphics::mergeGeometryStreamsInLaneOrder`
//    -- so that function's own "driving it from a real widened invocation"
//    deferral, noted in GeometryStream.h's own comment, is what this session
//    closes, even though the two never literally share a live object.
//
// The `StripEndsAfter[slot]` boolean-per-emitted-vertex representation is
// exactly equivalent to replaying the invocation's actual `emit`/`cut` call
// sequence into a `GeometryStreamBuilder`: `cut` is a no-op unless at least
// one vertex has been emitted since the last cut (`GeometryStreamBuilder::
// cut`'s own doc comment), so recording "a cut happened immediately after
// emitted vertex I" -- which `lowerGeometryStreamCut` only ever does when at
// least one vertex has already been emitted -- reproduces the identical
// strip partition `collectGeometryStreams` reconstructs by calling `emit`
// for each recorded vertex and `cut` after any one flagged in
// `StripEndsAfter`.
//
// This milestone deliberately scopes down two shapes, both diagnosed rather
// than silently mishandled:
//
//  - **More than one output stream.** `feme.stage.stream.emit`/`.cut`
//    naming any stream other than 0, or an output element whose
//    `SignatureElement::Stream` is nonzero, is diagnosed: `FemeGeometryArgs`
//    only carries storage for stream 0.
//  - **A group-sync barrier.** Geometry invocations are independent (no
//    groupshared cooperation model for this stage, exactly like the domain
//    stage), so a surviving `..._with_group_sync` call is diagnosed the same
//    way `DomainWrapperPass`/`HullWrapperPass` diagnose their own.
//
// One further scope note: `feme.stage.stream.emit`/`.cut` are gated only by
// the wave's own entry mask (`WaveBodyEnv::EntryMask`), not by any finer
// control-flow-conditional side-effect mask the way `feme.stage.output.
// store` is (`Linearize.cpp` threads a per-store side-effect mask onto
// *that* op via `feme::cpu::createMaskedOutputStore`, but does not do the
// same for `StreamEmit`/`StreamCut` -- there is no masked variant of either
// to lower here). This is exact for a control-flow-uniform geometry shader
// (unconditional `emit`/`cut` calls, or ones only inside a loop every lane
// executes the same number of times -- the common "emit a fixed-size fan or
// strip" shape) and is documented rather than silently wrong for a shader
// whose `emit`/`cut` calls are themselves inside divergent control flow;
// closing that gap needs `Linearize.cpp`'s masking extended to
// `StreamEmit`/`StreamCut`, left for whenever a real shader needs it.
//
// Still deferred alongside this file, documented in HullWrapper.cpp and
// DomainWrapper.cpp: generalizing `EntryWrapperPass`'s barrier-region-
// splitting machinery to the control-point batch ABI, and wiring any
// compiled tessellation/geometry stage into `executeDraws`/`feme-render`.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/GeometryWrapper.h"

#include "BarrierCalls.h"
#include "StageArgsLayout.h"
#include "StageMaskCalls.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Target/CPU/RuntimeABI.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace feme;
using namespace feme::cpu;

namespace {

constexpr StringLiteral InputLayoutParamName = "stage_input_layout";
constexpr StringLiteral InputsParamName = "stage_inputs";
constexpr StringLiteral OutputLayoutParamName = "stage_output_layout";
constexpr StringLiteral OutputsParamName = "stage_outputs";
constexpr StringLiteral InvocationsParamName = "stage_geometry_invocations";
constexpr StringLiteral VerticesPerPrimitiveParamName =
    "stage_geometry_vertices_per_primitive";
constexpr StringLiteral MaxVerticesPerStreamParamName =
    "stage_geometry_max_vertices_per_stream";
constexpr StringLiteral OutputScalarsPerVertexParamName =
    "stage_geometry_output_scalars_per_vertex";
constexpr StringLiteral EmittedVerticesParamName =
    "stage_geometry_emitted_vertices";
constexpr StringLiteral EmittedVertexCountsParamName =
    "stage_geometry_emitted_vertex_counts";
constexpr StringLiteral StripEndsAfterParamName =
    "stage_geometry_strip_ends_after";

/// This milestone's own scope limit (see the file comment): only output
/// stream 0 has any storage.
constexpr uint32_t SupportedStream = 0;

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

struct GeometryStageEnv {
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
  Value *VerticesPerPrimitive = nullptr;
  Value *MaxVerticesPerStream = nullptr;
  Value *OutputScalarsPerVertex = nullptr;
  Value *EmittedVertices = nullptr;
  Value *EmittedVertexCounts = nullptr;
  Value *StripEndsAfter = nullptr;
};

std::optional<GeometryStageEnv> getGeometryStageEnv(Function &F) {
  GeometryStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == InputLayoutParamName)
      Env.InputLayout = &Arg, Found = true;
    else if (Arg.getName() == InputsParamName)
      Env.Inputs = &Arg, Found = true;
    else if (Arg.getName() == OutputLayoutParamName)
      Env.OutputLayout = &Arg, Found = true;
    else if (Arg.getName() == OutputsParamName)
      Env.Outputs = &Arg, Found = true;
    else if (Arg.getName() == InvocationsParamName)
      Env.Invocations = &Arg, Found = true;
    else if (Arg.getName() == VerticesPerPrimitiveParamName)
      Env.VerticesPerPrimitive = &Arg, Found = true;
    else if (Arg.getName() == MaxVerticesPerStreamParamName)
      Env.MaxVerticesPerStream = &Arg, Found = true;
    else if (Arg.getName() == OutputScalarsPerVertexParamName)
      Env.OutputScalarsPerVertex = &Arg, Found = true;
    else if (Arg.getName() == EmittedVerticesParamName)
      Env.EmittedVertices = &Arg, Found = true;
    else if (Arg.getName() == EmittedVertexCountsParamName)
      Env.EmittedVertexCounts = &Arg, Found = true;
    else if (Arg.getName() == StripEndsAfterParamName)
      Env.StripEndsAfter = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

Function *appendGeometryStageParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 16> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, I32Ty, I32Ty, I32Ty,
                     PtrTy, PtrTy, PtrTy});

  FunctionType *NewTy =
      FunctionType::get(F.getReturnType(), ParamTypes, F.isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  NewF->setComdat(F.getComdat());
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    NewF->setMetadata(Kind, Node);
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = NewF->arg_begin() + F.arg_size();
  (&*ArgIt++)->setName(InputLayoutParamName);
  (&*ArgIt++)->setName(InputsParamName);
  (&*ArgIt++)->setName(OutputLayoutParamName);
  (&*ArgIt++)->setName(OutputsParamName);
  (&*ArgIt++)->setName(InvocationsParamName);
  (&*ArgIt++)->setName(VerticesPerPrimitiveParamName);
  (&*ArgIt++)->setName(MaxVerticesPerStreamParamName);
  (&*ArgIt++)->setName(OutputScalarsPerVertexParamName);
  (&*ArgIt++)->setName(EmittedVerticesParamName);
  (&*ArgIt++)->setName(EmittedVertexCountsParamName);
  (&*ArgIt++)->setName(StripEndsAfterParamName);

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

Value *extractLaneOrScalar(IRBuilder<> &Builder, Value *V, unsigned Lane) {
  if (isa<FixedVectorType>(V->getType()))
    return Builder.CreateExtractElement(V, Builder.getInt32(Lane));
  return V;
}

Value *getFlatInvocationIndex(IRBuilder<> &Builder, const WaveBodyEnv &WEnv,
                              unsigned WaveSize, unsigned Lane) {
  Value *Base = Builder.CreateMul(WEnv.WaveIndex, Builder.getInt32(WaveSize),
                                  "flat.base");
  return Builder.CreateAdd(Base, Builder.getInt32(Lane), "flat.index");
}

Value *loadLayoutField(IRBuilder<> &Builder, Value *LayoutArg,
                       unsigned ElementID, unsigned Field, Type *FieldTy) {
  LLVMContext &Ctx = Builder.getContext();
  StructType *LayoutTy = getStageLayoutType(Ctx);
  StructType *ElementTy = getStageElementType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Value *ElementsRaw = loadStructField(Builder, LayoutTy, LayoutArg,
                                       StageLayoutFieldElements, PtrTy);
  Value *Elements =
      Builder.CreateBitCast(ElementsRaw, PointerType::get(Ctx, 0));
  Value *EntryPtr = Builder.CreateInBoundsGEP(ElementTy, Elements,
                                              Builder.getInt32(ElementID));
  Value *FieldPtr = Builder.CreateStructGEP(ElementTy, EntryPtr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

Value *computeStageStorageAddress(IRBuilder<> &Builder, Value *LayoutArg,
                                  Value *BasePtr, unsigned ElementID,
                                  const SignatureElement &Elt, Value *Row,
                                  Value *Component, Value *InvocationIndex) {
  LLVMContext &Ctx = Builder.getContext();
  Type *I32Ty = Builder.getInt32Ty();
  Type *I64Ty = Builder.getInt64Ty();
  Value *DataOffset = loadLayoutField(Builder, LayoutArg, ElementID,
                                      StageElementFieldDataOffset, I64Ty);
  Value *RowStride = loadLayoutField(Builder, LayoutArg, ElementID,
                                     StageElementFieldRowStride, I32Ty);
  Value *ComponentStride = loadLayoutField(
      Builder, LayoutArg, ElementID, StageElementFieldComponentStride, I32Ty);
  Value *InvocationStride = loadLayoutField(
      Builder, LayoutArg, ElementID, StageElementFieldInvocationStride, I32Ty);
  Value *RelComponent = Builder.CreateSub(
      Component, Builder.getInt32(Elt.FirstComponent), "component.rel");
  Value *ByteOffset = DataOffset;
  ByteOffset = Builder.CreateAdd(
      ByteOffset, Builder.CreateMul(Builder.CreateZExt(Row, I64Ty),
                                    Builder.CreateZExt(RowStride, I64Ty)));
  ByteOffset = Builder.CreateAdd(
      ByteOffset,
      Builder.CreateMul(Builder.CreateZExt(RelComponent, I64Ty),
                        Builder.CreateZExt(ComponentStride, I64Ty)));
  ByteOffset = Builder.CreateAdd(
      ByteOffset,
      Builder.CreateMul(Builder.CreateZExt(InvocationIndex, I64Ty),
                        Builder.CreateZExt(InvocationStride, I64Ty)));
  Value *Bytes = Builder.CreateBitCast(BasePtr, PointerType::get(Ctx, 0));
  return Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Bytes, ByteOffset);
}

/// Lowers an ordinary `feme.stage.input.load` reading an assembled input
/// primitive's vertex attributes. The vertex-in-primitive operand (`CI`'s
/// 4th argument) may be any value in `[0, VerticesPerPrimitive)`: a geometry
/// invocation legitimately reads more than one input vertex (see the file
/// comment).
Value *lowerGeometryInputLoad(CallInst &CI, const SignatureElement &Elt,
                              const WaveBodyEnv &WEnv,
                              const GeometryStageEnv &GEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);

  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *VertexInPrimitive =
        extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    Value *PrimitiveIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *SlotIndex = Builder.CreateAdd(
        Builder.CreateMul(PrimitiveIndex, GEnv.VerticesPerPrimitive),
        VertexInPrimitive, "geom.input.slot");
    Value *Addr = computeStageStorageAddress(Builder, GEnv.InputLayout,
                                             GEnv.Inputs, Elt.ElementID, Elt,
                                             Row, Component, SlotIndex);
    Value *LaneResult = Builder.CreateLoad(ScalarTy, Addr);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers a `feme.stage.input.load` of the `PrimitiveID` system value to a
/// read of this invocation's own `FemeGeometryInvocation` record.
Value *lowerGeometryPrimitiveID(CallInst &CI, const WaveBodyEnv &WEnv,
                                const GeometryStageEnv &GEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  LLVMContext &Ctx = CI.getContext();
  IRBuilder<> Builder(&CI);

  StructType *InvocationTy = getGeometryInvocationType(Ctx);
  Value *InvocationBase =
      Builder.CreateBitCast(GEnv.Invocations, PointerType::get(Ctx, 0));
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *InvocationPtr = Builder.CreateInBoundsGEP(
        InvocationTy, InvocationBase, InvocationIndex);
    Value *FieldPtr = Builder.CreateStructGEP(
        InvocationTy, InvocationPtr, GeometryInvocationFieldPrimitiveID);
    Value *LaneResult = Builder.CreateLoad(ScalarTy, FieldPtr);
    LaneResult = Builder.CreateSelect(Active, LaneResult,
                                      Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

void lowerGeometryOutputStore(CallInst &CI, const SignatureElement &Elt,
                              const WaveBodyEnv &WEnv,
                              const GeometryStageEnv &GEnv) {
  IRBuilder<> Builder(&CI);
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *InvocationIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *Addr = computeStageStorageAddress(Builder, GEnv.OutputLayout,
                                             GEnv.Outputs, Elt.ElementID, Elt,
                                             Row, Component, InvocationIndex);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// The constant \p Lane's component of \p V, for an operand a widened wave
/// body may present either as a scalar or as a lane-wise vector: the
/// `stream` operand of `feme.cpu.masked.stage.stream.emit`/`.cut` must be
/// resolvable at compile time (see the file comment's "more than one output
/// stream" scope note).
std::optional<uint64_t> getLaneConstantInt(Value *V, unsigned Lane) {
  if (auto *ScalarConst = dyn_cast<ConstantInt>(V))
    return ScalarConst->getZExtValue();
  auto *C = dyn_cast<Constant>(V);
  if (!C || !isa<FixedVectorType>(V->getType()))
    return std::nullopt;
  auto *LaneConst = dyn_cast_or_null<ConstantInt>(C->getAggregateElement(Lane));
  if (!LaneConst)
    return std::nullopt;
  return LaneConst->getZExtValue();
}

/// Lowers `feme.cpu.masked.stage.stream.emit(stream, mask)` -- the masked
/// form `LinearizePass` creates from `feme.stage.stream.emit(stream)`,
/// threading the per-lane side-effect mask exactly as it already does for
/// `feme.stage.output.store` (see the file comment): snapshots this
/// invocation's current output scratch storage into one bounded
/// emitted-vertex record, gated by \p CI's own \p mask operand rather than
/// only the wave's entry mask. Requires a constant `stream` operand naming
/// `SupportedStream` in every lane; returns false (having emitted a
/// diagnostic) otherwise.
bool lowerGeometryStreamEmit(CallInst &CI, const EntrySignature &Sig,
                             const WaveBodyEnv &WEnv,
                             const GeometryStageEnv &GEnv) {
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(1)->getType())->getNumElements();
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    std::optional<uint64_t> Stream =
        getLaneConstantInt(CI.getArgOperand(0), Lane);
    if (!Stream || *Stream != SupportedStream) {
      CI.getContext().emitError(
          &CI, "feme-cpu-wrap-geometry: only output stream 0 is supported");
      return false;
    }
  }

  SmallVector<const SignatureElement *, 8> OutputElements;
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == SignatureDirection::Output) {
      if (Elt.Stream != SupportedStream) {
        CI.getContext().emitError(
            &CI, "feme-cpu-wrap-geometry: only output stream 0 is supported");
        return false;
      }
      OutputElements.push_back(&Elt);
    }

  LLVMContext &Ctx = CI.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *FloatTy = Type::getFloatTy(Ctx);

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    // Every lane's logic is inserted immediately before `CI` itself: each
    // `SplitBlockAndInsertIfThen` call below splits the block right there
    // again, so this remains valid across every lane's iteration (unlike
    // manually building the branch, which does not relocate the
    // instructions still after the split point).
    IRBuilder<> Builder(&CI);
    Value *Active = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *PrimitiveIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *CountAddr = Builder.CreateInBoundsGEP(
        I32Ty, GEnv.EmittedVertexCounts, PrimitiveIndex);
    Value *CurrentCount = Builder.CreateLoad(I32Ty, CountAddr);
    Value *CanEmit = Builder.CreateAnd(
        Active, Builder.CreateICmpULT(CurrentCount, GEnv.MaxVerticesPerStream),
        "geom.emit.cond");

    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(CanEmit, &CI, /*Unreachable=*/false);
    IRBuilder<> ThenBuilder(ThenTerm);
    Value *SlotIndex = ThenBuilder.CreateAdd(
        ThenBuilder.CreateMul(PrimitiveIndex, GEnv.MaxVerticesPerStream),
        CurrentCount, "geom.emit.slot");
    Value *RowBase =
        ThenBuilder.CreateMul(SlotIndex, GEnv.OutputScalarsPerVertex);

    uint32_t ScalarOffset = 0;
    for (const SignatureElement *Elt : OutputElements) {
      for (uint32_t Row = 0; Row != Elt->RowCount; ++Row) {
        for (uint32_t Comp = 0; Comp != Elt->ComponentCount; ++Comp) {
          Value *Addr = computeStageStorageAddress(
              ThenBuilder, GEnv.OutputLayout, GEnv.Outputs, Elt->ElementID,
              *Elt, ThenBuilder.getInt32(Row),
              ThenBuilder.getInt32(Elt->FirstComponent + Comp), PrimitiveIndex);
          Value *Scalar = ThenBuilder.CreateLoad(FloatTy, Addr);
          Value *DestIndex = ThenBuilder.CreateAdd(
              RowBase, ThenBuilder.getInt32(ScalarOffset), "geom.emit.dest");
          Value *DestPtr = ThenBuilder.CreateInBoundsGEP(
              FloatTy, GEnv.EmittedVertices, DestIndex);
          ThenBuilder.CreateStore(Scalar, DestPtr);
          ++ScalarOffset;
        }
      }
    }

    Value *StripAddr = ThenBuilder.CreateInBoundsGEP(
        ThenBuilder.getInt8Ty(), GEnv.StripEndsAfter, SlotIndex);
    ThenBuilder.CreateStore(ThenBuilder.getInt8(0), StripAddr);
    Value *NextCount = ThenBuilder.CreateAdd(
        CurrentCount, ThenBuilder.getInt32(1), "geom.emit.next");
    ThenBuilder.CreateStore(NextCount, CountAddr);
  }
  return true;
}

/// Lowers `feme.cpu.masked.stage.stream.cut(stream, mask)`: closes the strip
/// currently accumulating on stream `SupportedStream` for this invocation, a
/// no-op if nothing has been emitted since the last cut (see the file
/// comment).
bool lowerGeometryStreamCut(CallInst &CI, const WaveBodyEnv &WEnv,
                            const GeometryStageEnv &GEnv) {
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(1)->getType())->getNumElements();
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    std::optional<uint64_t> Stream =
        getLaneConstantInt(CI.getArgOperand(0), Lane);
    if (!Stream || *Stream != SupportedStream) {
      CI.getContext().emitError(
          &CI, "feme-cpu-wrap-geometry: only output stream 0 is supported");
      return false;
    }
  }

  Type *I32Ty = Type::getInt32Ty(CI.getContext());

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    IRBuilder<> Builder(&CI);
    Value *Active = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *PrimitiveIndex =
        getFlatInvocationIndex(Builder, WEnv, WaveSize, Lane);
    Value *CountAddr = Builder.CreateInBoundsGEP(
        I32Ty, GEnv.EmittedVertexCounts, PrimitiveIndex);
    Value *CurrentCount = Builder.CreateLoad(I32Ty, CountAddr);
    Value *CanCut = Builder.CreateAnd(
        Active, Builder.CreateICmpUGT(CurrentCount, Builder.getInt32(0)),
        "geom.cut.cond");

    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(CanCut, &CI, /*Unreachable=*/false);
    IRBuilder<> ThenBuilder(ThenTerm);
    Value *LastVertex = ThenBuilder.CreateSub(
        CurrentCount, ThenBuilder.getInt32(1), "geom.cut.last");
    Value *SlotIndex = ThenBuilder.CreateAdd(
        ThenBuilder.CreateMul(PrimitiveIndex, GEnv.MaxVerticesPerStream),
        LastVertex, "geom.cut.slot");
    Value *StripAddr = ThenBuilder.CreateInBoundsGEP(
        ThenBuilder.getInt8Ty(), GEnv.StripEndsAfter, SlotIndex);
    ThenBuilder.CreateStore(ThenBuilder.getInt8(1), StripAddr);
  }
  return true;
}

bool lowerGeometryStageOps(Function &F) {
  // Geometry invocations are independent, so a surviving group-sync barrier
  // means a shape this wrapper does not implement (see the file comment).
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (std::optional<MatchedBarrier> Matched = matchBarrierCall(*CI)) {
      if (Matched->GroupSync) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-geometry: a group-sync barrier is not "
                "supported in the geometry stage's independent "
                "per-primitive invocations");
        return false;
      }
    }
  }

  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedOutputStoreCall(*CI) ||
                      isMaskedStreamEmitCall(*CI) || isMaskedStreamCutCall(*CI);
  if (!UsesStageOps)
    return true;
  if (!Sig) {
    F.getContext().emitError(
        "feme-cpu-wrap-geometry: geometry stage wrapper requires attached "
        "feme.signature metadata");
    return false;
  }

  std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(F);
  std::optional<GeometryStageEnv> GEnv = getGeometryStageEnv(F);
  if (!WEnv || !GEnv)
    return false;

  // Collected up front, rather than lowered while iterating `instructions(F)`
  // directly: `lowerGeometryStreamEmit`/`Cut` split basic blocks
  // (`SplitBlockAndInsertIfThen`), which would invalidate a live
  // whole-function instruction iterator.
  SmallVector<CallInst *, 16> Calls;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      Calls.push_back(CI);

  for (CallInst *CI : Calls) {
    if (isMaskedOutputStoreCall(*CI)) {
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Output)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-geometry: masked output store references an "
                "unknown signature element");
        return false;
      }
      lowerGeometryOutputStore(*CI, *Elt, *WEnv, *GEnv);
      CI->eraseFromParent();
      continue;
    }
    if (isMaskedStreamEmitCall(*CI)) {
      if (!lowerGeometryStreamEmit(*CI, *Sig, *WEnv, *GEnv))
        return false;
      CI->eraseFromParent();
      continue;
    }
    if (isMaskedStreamCutCall(*CI)) {
      if (!lowerGeometryStreamCut(*CI, *WEnv, *GEnv))
        return false;
      CI->eraseFromParent();
      continue;
    }

    StageOpKind Kind;
    if (!isStageOpCall(*CI, &Kind))
      continue;

    auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    if (!EltID) {
      F.getContext().emitError(
          CI,
          "feme-cpu-wrap-geometry: stage IO requires a constant element ID");
      return false;
    }

    switch (Kind) {
    case StageOpKind::InputLoad: {
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Input);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-geometry: input load refers to an unknown "
                "signature element");
        return false;
      }
      Value *Lowered = Elt->SystemValue == SignatureSystemValue::PrimitiveID
                           ? lowerGeometryPrimitiveID(*CI, *WEnv, *GEnv)
                           : lowerGeometryInputLoad(*CI, *Elt, *WEnv, *GEnv);
      CI->replaceAllUsesWith(Lowered);
      CI->eraseFromParent();
      break;
    }
    case StageOpKind::OutputStore: {
      const SignatureElement *Elt =
          findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                      SignatureDirection::Output);
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-geometry: output store refers to an unknown "
                "signature element");
        return false;
      }
      lowerGeometryOutputStore(*CI, *Elt, *WEnv, *GEnv);
      CI->eraseFromParent();
      break;
    }
    default:
      F.getContext().emitError(
          CI, "feme-cpu-wrap-geometry: unexpected stage op left for the "
              "geometry wrapper");
      return false;
    }
  }
  return true;
}

struct WrapperEnv {
  Value *ResourceHeap = nullptr;
  Value *ResourceHeapCount = nullptr;
  Value *SamplerHeap = nullptr;
  Value *SamplerHeapCount = nullptr;
  Value *RootConstants = nullptr;
  Value *RootConstantSize = nullptr;
  Value *ImageHeap = nullptr;
  Value *ImageHeapCount = nullptr;
  Value *InputLayout = nullptr;
  Value *Inputs = nullptr;
  Value *OutputLayout = nullptr;
  Value *Outputs = nullptr;
  Value *Invocations = nullptr;
  Value *VerticesPerPrimitive = nullptr;
  Value *MaxVerticesPerStream = nullptr;
  Value *OutputScalarsPerVertex = nullptr;
  Value *EmittedVertices = nullptr;
  Value *EmittedVertexCounts = nullptr;
  Value *StripEndsAfter = nullptr;
  Value *PrimitiveCount = nullptr;
};

WrapperEnv buildWrapperEnv(IRBuilder<> &Builder, StructType *ArgsTy,
                           Value *Args) {
  LLVMContext &Ctx = Builder.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Builder.getInt32Ty();
  WrapperEnv Env;
  Env.PrimitiveCount = loadStructField(Builder, ArgsTy, Args,
                                       GeometryArgsFieldPrimitiveCount, I32Ty);
  Env.VerticesPerPrimitive = loadStructField(
      Builder, ArgsTy, Args, GeometryArgsFieldVerticesPerPrimitive, I32Ty);
  Env.MaxVerticesPerStream = loadStructField(
      Builder, ArgsTy, Args, GeometryArgsFieldMaxVerticesPerStream, I32Ty);
  Env.OutputScalarsPerVertex = loadStructField(
      Builder, ArgsTy, Args, GeometryArgsFieldOutputScalarsPerVertex, I32Ty);
  Env.InputLayout = loadStructField(Builder, ArgsTy, Args,
                                    GeometryArgsFieldInputLayout, PtrTy);
  Env.Inputs =
      loadStructField(Builder, ArgsTy, Args, GeometryArgsFieldInputs, PtrTy);
  Env.OutputLayout = loadStructField(Builder, ArgsTy, Args,
                                     GeometryArgsFieldOutputLayout, PtrTy);
  Env.Outputs =
      loadStructField(Builder, ArgsTy, Args, GeometryArgsFieldOutputs, PtrTy);
  Env.Invocations = loadStructField(Builder, ArgsTy, Args,
                                    GeometryArgsFieldInvocations, PtrTy);
  Env.EmittedVertices = loadStructField(
      Builder, ArgsTy, Args, GeometryArgsFieldEmittedVertices, PtrTy);
  Env.EmittedVertexCounts = loadStructField(
      Builder, ArgsTy, Args, GeometryArgsFieldEmittedVertexCounts, PtrTy);
  Env.StripEndsAfter = loadStructField(Builder, ArgsTy, Args,
                                       GeometryArgsFieldStripEndsAfter, PtrTy);

  Value *ResourcesRaw =
      loadStructField(Builder, ArgsTy, Args, GeometryArgsFieldResources, PtrTy);
  StructType *ResourcesTy = getShaderResourcesType(Ctx);
  Value *Resources =
      Builder.CreateBitCast(ResourcesRaw, PointerType::get(Ctx, 0));
  Env.ResourceHeap = loadStructField(Builder, ResourcesTy, Resources,
                                     ShaderResourcesFieldResourceHeap, PtrTy);
  Env.ResourceHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldResourceHeapCount, I32Ty);
  Env.SamplerHeap = loadStructField(Builder, ResourcesTy, Resources,
                                    ShaderResourcesFieldSamplerHeap, PtrTy);
  Env.SamplerHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldSamplerHeapCount, I32Ty);
  Env.RootConstants = loadStructField(Builder, ResourcesTy, Resources,
                                      ShaderResourcesFieldRootConstants, PtrTy);
  Env.RootConstantSize =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldRootConstantSize, I32Ty);
  Env.ImageHeap = loadStructField(Builder, ResourcesTy, Resources,
                                  ShaderResourcesFieldImageHeap, PtrTy);
  Env.ImageHeapCount =
      loadStructField(Builder, ResourcesTy, Resources,
                      ShaderResourcesFieldImageHeapCount, I32Ty);
  return Env;
}

Function *buildWrapper(Function &Body) {
  if (!getWaveBodyEnv(Body))
    return nullptr;
  Module &M = *Body.getParent();
  LLVMContext &Ctx = M.getContext();
  unsigned WaveSize =
      cast<FixedVectorType>(getWaveBodyEnv(Body)->EntryMask->getType())
          ->getNumElements();

  StructType *ArgsTy = getGeometryArgsType(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  std::string WrapperName = getEntrySymbolName(Body.getName());
  Function *Wrapper =
      Function::Create(FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false),
                       GlobalValue::ExternalLinkage, WrapperName, M);
  Argument *Args = Wrapper->getArg(0);
  Args->setName("args");

  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Wrapper);
  BasicBlock *HeaderBB = BasicBlock::Create(Ctx, "wave.loop.header", Wrapper);
  BasicBlock *BodyBB = BasicBlock::Create(Ctx, "wave.loop.body", Wrapper);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "wave.loop.exit", Wrapper);

  IRBuilder<> Entry(EntryBB);
  WrapperEnv Env = buildWrapperEnv(Entry, ArgsTy, Args);
  Value *Waves = Entry.CreateUDiv(
      Entry.CreateAdd(Env.PrimitiveCount, Entry.getInt32(WaveSize - 1)),
      Entry.getInt32(WaveSize), "waves");
  Entry.CreateBr(HeaderBB);

  IRBuilder<> Header(HeaderBB);
  PHINode *W = Header.CreatePHI(I32Ty, 2, "w");
  W->addIncoming(Header.getInt32(0), EntryBB);
  Value *Cond = Header.CreateICmpULT(W, Waves, "wave.cond");
  Header.CreateCondBr(Cond, BodyBB, ExitBB);

  IRBuilder<> BodyIR(BodyBB);
  Value *Base = BodyIR.CreateMul(W, BodyIR.getInt32(WaveSize));
  Value *WideBase = BodyIR.CreateVectorSplat(WaveSize, Base);
  SmallVector<Constant *, 8> Lanes;
  for (unsigned I = 0; I != WaveSize; ++I)
    Lanes.push_back(BodyIR.getInt32(I));
  Value *Indices = BodyIR.CreateAdd(WideBase, ConstantVector::get(Lanes));
  Value *WideCount = BodyIR.CreateVectorSplat(WaveSize, Env.PrimitiveCount);
  Value *Mask = BodyIR.CreateICmpULT(Indices, WideCount, "wave.mask");

  SmallVector<Value *, 16> CallArgs;
  for (const Argument &Arg : Body.args()) {
    if (Arg.getName() == "resource_heap")
      CallArgs.push_back(Env.ResourceHeap);
    else if (Arg.getName() == "resource_heap_count")
      CallArgs.push_back(Env.ResourceHeapCount);
    else if (Arg.getName() == "sampler_heap")
      CallArgs.push_back(Env.SamplerHeap);
    else if (Arg.getName() == "sampler_heap_count")
      CallArgs.push_back(Env.SamplerHeapCount);
    else if (Arg.getName() == "root_constants")
      CallArgs.push_back(Env.RootConstants);
    else if (Arg.getName() == "root_constant_size")
      CallArgs.push_back(Env.RootConstantSize);
    else if (Arg.getName() == "image_heap")
      CallArgs.push_back(Env.ImageHeap);
    else if (Arg.getName() == "image_heap_count")
      CallArgs.push_back(Env.ImageHeapCount);
    else if (Arg.getName() == "wave_group_id_x" ||
             Arg.getName() == "wave_group_id_y" ||
             Arg.getName() == "wave_group_id_z")
      CallArgs.push_back(BodyIR.getInt32(0));
    else if (Arg.getName() == "wave_index")
      CallArgs.push_back(W);
    else if (Arg.getName() == "wave_entry_mask" ||
             Arg.getName() == "wave_sideeffect_mask")
      CallArgs.push_back(Mask);
    else if (Arg.getName() == "wave_groupshared")
      CallArgs.push_back(
          ConstantPointerNull::get(cast<PointerType>(Arg.getType())));
    else if (Arg.getName() == InputLayoutParamName)
      CallArgs.push_back(Env.InputLayout);
    else if (Arg.getName() == InputsParamName)
      CallArgs.push_back(Env.Inputs);
    else if (Arg.getName() == OutputLayoutParamName)
      CallArgs.push_back(Env.OutputLayout);
    else if (Arg.getName() == OutputsParamName)
      CallArgs.push_back(Env.Outputs);
    else if (Arg.getName() == InvocationsParamName)
      CallArgs.push_back(Env.Invocations);
    else if (Arg.getName() == VerticesPerPrimitiveParamName)
      CallArgs.push_back(Env.VerticesPerPrimitive);
    else if (Arg.getName() == MaxVerticesPerStreamParamName)
      CallArgs.push_back(Env.MaxVerticesPerStream);
    else if (Arg.getName() == OutputScalarsPerVertexParamName)
      CallArgs.push_back(Env.OutputScalarsPerVertex);
    else if (Arg.getName() == EmittedVerticesParamName)
      CallArgs.push_back(Env.EmittedVertices);
    else if (Arg.getName() == EmittedVertexCountsParamName)
      CallArgs.push_back(Env.EmittedVertexCounts);
    else if (Arg.getName() == StripEndsAfterParamName)
      CallArgs.push_back(Env.StripEndsAfter);
    else
      llvm_unreachable("unexpected parameter for GeometryWrapperPass");
  }
  BodyIR.CreateCall(&Body, CallArgs);
  Value *WNext = BodyIR.CreateAdd(W, BodyIR.getInt32(1), "w.next");
  BodyIR.CreateBr(HeaderBB);
  W->addIncoming(WNext, BodyBB);

  IRBuilder<>(ExitBB).CreateRetVoid();
  Body.setLinkage(GlobalValue::InternalLinkage);
  return Wrapper;
}

} // namespace

PreservedAnalyses GeometryWrapperPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Geometry)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendGeometryStageParams(*F);
    if (!lowerGeometryStageOps(*Body))
      continue;
    if (buildWrapper(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
