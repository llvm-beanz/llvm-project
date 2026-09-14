//===- MeshOutputWrapper.cpp - Mesh stage output store lowering ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See MeshOutputWrapper.h for this pass's scope and roadmap H6c-a-a's own
// design notes.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/MeshOutputWrapper.h"

#include "StageArgsLayout.h"
#include "StageMaskCalls.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::cpu;

namespace {

constexpr StringLiteral VertexOutputLayoutParamName =
    "mesh_vertex_output_layout";
constexpr StringLiteral VertexOutputsParamName = "mesh_vertex_outputs";
constexpr StringLiteral PrimitiveOutputLayoutParamName =
    "mesh_primitive_output_layout";
constexpr StringLiteral PrimitiveOutputsParamName = "mesh_primitive_outputs";
constexpr StringLiteral MaxOutputVerticesParamName = "mesh_max_output_vertices";
constexpr StringLiteral MaxOutputPrimitivesParamName =
    "mesh_max_output_primitives";
constexpr StringLiteral ActualVertexCountParamName = "mesh_actual_vertex_count";
constexpr StringLiteral ActualPrimitiveCountParamName =
    "mesh_actual_primitive_count";
/// (Roadmap H6p) `FemeMeshArgs::DrawID`, SPIR-V's `DrawIndex` builtin
/// (`gl_DrawID`) -- the one legitimate stage-IO *input* a mesh entry point
/// actually has, unlike this pass's file comment's original "a mesh entry
/// point has no ordinary stage-IO input to read" assumption.
constexpr StringLiteral DrawIDParamName = "mesh_draw_id";
/// (Roadmap H29r) `FemeMeshArgs::PrimitiveIndices`, the flat primitive-major
/// `MaxOutputPrimitives * getVerticesPerPrimitive(OutputTopology)` array a
/// lowered `SignatureSystemValue::PrimitiveIndices` output store writes
/// through -- see `lowerMeshPrimitiveIndicesStore`.
constexpr StringLiteral PrimitiveIndicesParamName = "mesh_primitive_indices";
/// (Roadmap L30) `FemeMeshArgs::Payload`, the read-only byte buffer
/// holding the bound task stage's own payload (or null if no task stage is
/// bound), threaded through unchanged from `EntryWrapper.cpp`'s own
/// `Env.MeshPayload` -- the load-side counterpart of
/// `TaskPayloadWrapper.cpp`'s own `PayloadParamName`, just read-only here
/// since only a task entry ever writes it.
constexpr StringLiteral PayloadParamName = "mesh_payload";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

/// The mesh-specific wave-body parameters this pass appends (see the file
/// comment): both output arrays' own layout/base-pointer pairs, plus each
/// array's declared maximum slot count, used to clamp a store's dynamic
/// `Vertex` operand into range (see `lowerMeshOutputStore`'s own comment).
struct MeshOutputStageEnv {
  Value *VertexOutputLayout = nullptr;
  Value *VertexOutputs = nullptr;
  Value *PrimitiveOutputLayout = nullptr;
  Value *PrimitiveOutputs = nullptr;
  Value *MaxOutputVertices = nullptr;
  Value *MaxOutputPrimitives = nullptr;
  /// (Roadmap H6c-a-a-i) `FemeMeshArgs::ActualVertexCount`/
  /// `ActualPrimitiveCount`, the single-scalar pointers a lowered
  /// `feme.stage.set_mesh_outputs` writes through -- see the file
  /// comment.
  Value *ActualVertexCount = nullptr;
  Value *ActualPrimitiveCount = nullptr;
  /// (Roadmap H6p) `FemeMeshArgs::DrawID`, workgroup-uniform, threaded
  /// through unchanged from `EntryWrapper.cpp`'s own `MeshDrawID`.
  Value *DrawID = nullptr;
  /// (Roadmap H29r) `FemeMeshArgs::PrimitiveIndices`.
  Value *PrimitiveIndices = nullptr;
  /// (Roadmap L30) `FemeMeshArgs::Payload`, the bound task stage's own
  /// payload bytes this mesh entry's own `feme.stage.task.payload.load`
  /// calls read through -- see `PayloadParamName`'s own comment.
  Value *Payload = nullptr;
};

std::optional<MeshOutputStageEnv> getMeshOutputStageEnv(Function &F) {
  MeshOutputStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == VertexOutputLayoutParamName)
      Env.VertexOutputLayout = &Arg, Found = true;
    else if (Arg.getName() == VertexOutputsParamName)
      Env.VertexOutputs = &Arg, Found = true;
    else if (Arg.getName() == PrimitiveOutputLayoutParamName)
      Env.PrimitiveOutputLayout = &Arg, Found = true;
    else if (Arg.getName() == PrimitiveOutputsParamName)
      Env.PrimitiveOutputs = &Arg, Found = true;
    else if (Arg.getName() == MaxOutputVerticesParamName)
      Env.MaxOutputVertices = &Arg, Found = true;
    else if (Arg.getName() == MaxOutputPrimitivesParamName)
      Env.MaxOutputPrimitives = &Arg, Found = true;
    else if (Arg.getName() == ActualVertexCountParamName)
      Env.ActualVertexCount = &Arg, Found = true;
    else if (Arg.getName() == ActualPrimitiveCountParamName)
      Env.ActualPrimitiveCount = &Arg, Found = true;
    else if (Arg.getName() == DrawIDParamName)
      Env.DrawID = &Arg, Found = true;
    else if (Arg.getName() == PrimitiveIndicesParamName)
      Env.PrimitiveIndices = &Arg, Found = true;
    else if (Arg.getName() == PayloadParamName)
      Env.Payload = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

/// Appends this pass's own trailing parameters (see the file comment) to
/// \p F, splicing its body into a freshly-created function the same way
/// every other stage wrapper's own `append*StageParams` does (see e.g.
/// `GeometryWrapper.cpp`'s `appendGeometryStageParams`).
Function *appendMeshOutputParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append(
      {PtrTy, PtrTy, PtrTy, PtrTy, I32Ty, I32Ty, PtrTy, PtrTy, I32Ty, PtrTy,
       PtrTy});

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
  (&*ArgIt++)->setName(VertexOutputLayoutParamName);
  (&*ArgIt++)->setName(VertexOutputsParamName);
  (&*ArgIt++)->setName(PrimitiveOutputLayoutParamName);
  (&*ArgIt++)->setName(PrimitiveOutputsParamName);
  (&*ArgIt++)->setName(MaxOutputVerticesParamName);
  (&*ArgIt++)->setName(MaxOutputPrimitivesParamName);
  (&*ArgIt++)->setName(ActualVertexCountParamName);
  (&*ArgIt++)->setName(ActualPrimitiveCountParamName);
  (&*ArgIt++)->setName(DrawIDParamName);
  (&*ArgIt++)->setName(PrimitiveIndicesParamName);
  (&*ArgIt++)->setName(PayloadParamName);

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

/// Finds \p F's own `wave_index` parameter (named by
/// `feme::cpu::SIMDizePass`, see `SIMDize.cpp`'s `Env.WaveIndex->setName`):
/// this wave's index within its shader entry's group, used by
/// `lowerSetMeshOutputs` below to identify the one lane that is truly
/// SPIR-V invocation 0 (`WaveLowering.cpp`'s `buildFlattenedThreadIdInGroup`
/// computes a flattened invocation id of `wave_index * WaveSize + lane`, so
/// invocation 0 is exactly `wave_index == 0 && lane == 0`).
Value *getWaveIndexArg(Function &F) {
  for (Argument &Arg : F.args())
    if (Arg.getName() == "wave_index")
      return &Arg;
  return nullptr;
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

/// Computes the byte address of element \p ElementID's `(Row, Component)`
/// scalar within output slot \p SlotIndex of the structure-of-arrays block
/// based at \p BasePtr, laid out per \p LayoutArg -- mirroring every other
/// stage wrapper's own `computeStageStorageAddress`, just parameterized on
/// which of the two mesh output arrays (`VertexOutputs`/`PrimitiveOutputs`)
/// the caller has already chosen.
Value *computeMeshOutputAddress(IRBuilder<> &Builder, Value *LayoutArg,
                                Value *BasePtr, unsigned ElementID,
                                const SignatureElement &Elt, Value *Row,
                                Value *Component, Value *SlotIndex) {
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
      Builder.CreateMul(Builder.CreateZExt(SlotIndex, I64Ty),
                        Builder.CreateZExt(InvocationStride, I64Ty)));
  Value *Bytes = Builder.CreateBitCast(BasePtr, PointerType::get(Ctx, 0));
  return Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Bytes, ByteOffset);
}

/// Clamps \p SlotIndex into `[0, Max)`, defensively: unlike an ordinary
/// vertex/fragment invocation's own flat index (always in range by
/// construction), a mesh output store's `Vertex` operand is the compiled
/// entry point's own runtime value. Clamping to the declared *maximum*
/// here is what keeps an out-of-range write from corrupting host memory
/// beyond `VertexOutputs`/`PrimitiveOutputs`'s own bounds; a *tighter*
/// bound against the workgroup's real declared output count (now written
/// through `ActualVertexCount`/`ActualPrimitiveCount` by
/// `lowerSetMeshOutputs`, roadmap H6c-a-a-i) is left to a future row, since
/// nothing here threads that value back into an earlier-lowered store --
/// `feme::graphics::Meshlet::assembleMeshlet` (roadmap H6d) is what
/// actually re-validates a real workgroup's output against it.
Value *clampSlotIndex(IRBuilder<> &Builder, Value *SlotIndex, Value *Max) {
  Value *Zero = Builder.getInt32(0);
  Value *MaxIndex = Builder.CreateSub(Max, Builder.getInt32(1), "max.index");
  Value *MaxIsZero = Builder.CreateICmpEQ(Max, Zero, "max.iszero");
  Value *SafeMaxIndex =
      Builder.CreateSelect(MaxIsZero, Zero, MaxIndex, "safe.max.index");
  return Builder.CreateBinaryIntrinsic(Intrinsic::umin, SlotIndex, SafeMaxIndex,
                                       nullptr, "slot.clamped");
}

/// Lowers `feme.cpu.masked.stage.output.store` for a mesh entry's per-vertex
/// or per-primitive output element \p Elt (chosen by
/// `Elt.Frequency`, see the file comment): stores each active lane's value
/// at its own dynamic output slot (the call's `Vertex` operand, clamped per
/// `clampSlotIndex`) of `VertexOutputs`/`PrimitiveOutputs`.
void lowerMeshOutputStore(CallInst &CI, const SignatureElement &Elt,
                          const MeshOutputStageEnv &MEnv) {
  IRBuilder<> Builder(&CI);
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  bool PerPrimitive = Elt.Frequency == SignatureFrequency::PerPrimitive;
  Value *Layout =
      PerPrimitive ? MEnv.PrimitiveOutputLayout : MEnv.VertexOutputLayout;
  Value *Base = PerPrimitive ? MEnv.PrimitiveOutputs : MEnv.VertexOutputs;
  Value *Max = PerPrimitive ? MEnv.MaxOutputPrimitives : MEnv.MaxOutputVertices;
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Slot = extractLaneOrScalar(Builder, CI.getArgOperand(4), Lane);
    Value *ClampedSlot = clampSlotIndex(Builder, Slot, Max);
    Value *Addr = computeMeshOutputAddress(Builder, Layout, Base, Elt.ElementID,
                                           Elt, Row, Component, ClampedSlot);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// Lowers `feme.cpu.masked.stage.output.store` for a mesh entry's
/// `SignatureSystemValue::PrimitiveIndices` element (roadmap H29r): SPIR-V's
/// `gl_PrimitiveTriangleIndicesEXT`/`PrimitiveLineIndicesEXT`/
/// `PrimitivePointIndicesEXT`. Unlike every other mesh output element, this
/// one has no structure-of-arrays attribute storage of its own at all -- it
/// lives in `FemeMeshArgs::PrimitiveIndices`, a flat primitive-major
/// `MaxOutputPrimitives * VerticesPerPrimitive` `uint32_t` array, so the
/// address is a plain `Slot * VerticesPerPrimitive + Component` index rather
/// than a layout-driven byte offset.
///
/// `VerticesPerPrimitive` is `Elt.ComponentCount` (3/2/1 for triangles/lines/
/// points), which `CanonicalizeStage.cpp`'s own classification already
/// derived from the declared builtin, so the topology never has to be
/// threaded in separately.
void lowerMeshPrimitiveIndicesStore(CallInst &CI, const SignatureElement &Elt,
                                    const MeshOutputStageEnv &MEnv) {
  IRBuilder<> Builder(&CI);
  Type *I32Ty = Builder.getInt32Ty();
  unsigned WaveSize =
      cast<FixedVectorType>(CI.getArgOperand(3)->getType())->getNumElements();
  Value *VerticesPerPrim = Builder.getInt32(Elt.ComponentCount);
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Slot = extractLaneOrScalar(Builder, CI.getArgOperand(4), Lane);
    Value *ClampedSlot =
        clampSlotIndex(Builder, Slot, MEnv.MaxOutputPrimitives);
    Value *RelComponent = Builder.CreateSub(
        Component, Builder.getInt32(Elt.FirstComponent), "component.rel");
    Value *ClampedComponent =
        clampSlotIndex(Builder, RelComponent, VerticesPerPrim);
    Value *Index =
        Builder.CreateAdd(Builder.CreateMul(ClampedSlot, VerticesPerPrim),
                          ClampedComponent, "prim.index.slot");
    Value *Addr = Builder.CreateInBoundsGEP(I32Ty, MEnv.PrimitiveIndices, Index,
                                            "prim.index.addr");
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (LaneVal->getType() != I32Ty)
      LaneVal = Builder.CreateZExtOrTrunc(LaneVal, I32Ty);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(I32Ty, Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// Lowers `feme.cpu.masked.set_mesh_outputs` (roadmap H6c-a-a-i): writes
/// `MEnv.ActualVertexCount`/`ActualPrimitiveCount` from the one lane that is
/// truly SPIR-V invocation 0 (`wave_index == 0 && Lane == 0`, see
/// `getWaveIndexArg`'s own comment). `SetMeshOutputsEXT` is spec'd to be
/// called *only* from invocation 0 (GL_EXT_mesh_shader: "must be called by
/// invocation 0 of the workgroup, with values that are the same across all
/// invocations of the workgroup that execute it"), but this test suite's
/// own generated shaders (roadmap H85's own re-investigation) routinely
/// call it unconditionally, with every other invocation passing its own
/// *default*, un-set-by-the-shader-body value -- since `Linearize`/
/// `SIMDize` derive this call's mask purely from ordinary control-flow
/// reachability (every invocation reaches an unconditional call site), the
/// naive "every active lane's value is idempotent" assumption this
/// comment's own prior revision made does not hold: every other invocation
/// that reaches the call is just as "active" as invocation 0 and
/// overwrites its real value with a stale default, non-deterministically
/// depending on lane iteration order. Gating on the true flattened
/// invocation id, not merely the call site's own reachability mask, is
/// what actually implements the spec's "invocation 0 only" contract.
void lowerSetMeshOutputs(CallInst &CI, const MeshOutputStageEnv &MEnv) {
  IRBuilder<> Builder(&CI);
  Value *VertexCountArg = CI.getArgOperand(0);
  auto *WideTy = dyn_cast<FixedVectorType>(VertexCountArg->getType());
  unsigned WaveSize = WideTy ? WideTy->getNumElements() : 1;
  Type *ScalarTy =
      WideTy ? WideTy->getElementType() : VertexCountArg->getType();
  Value *WaveIndex = getWaveIndexArg(*CI.getFunction());
  Value *IsWaveZero =
      WaveIndex ? Builder.CreateICmpEQ(WaveIndex, Builder.getInt32(0))
                : Builder.getTrue();
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    // Only `Lane == 0` can ever be the flattened invocation 0 (see this
    // function's own comment); every other lane is unconditionally a
    // no-op here, regardless of what the call site's own mask says.
    if (Lane != 0)
      continue;

    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Mask = Builder.CreateAnd(Mask, IsWaveZero);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *LaneVertexCount =
        extractLaneOrScalar(Builder, CI.getArgOperand(0), Lane);
    Value *LanePrimitiveCount =
        extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVertexCount =
          Builder.CreateLoad(ScalarTy, MEnv.ActualVertexCount);
      Value *OldPrimitiveCount =
          Builder.CreateLoad(ScalarTy, MEnv.ActualPrimitiveCount);
      LaneVertexCount =
          Builder.CreateSelect(Mask, LaneVertexCount, OldVertexCount);
      LanePrimitiveCount =
          Builder.CreateSelect(Mask, LanePrimitiveCount, OldPrimitiveCount);
    }
    Builder.CreateStore(LaneVertexCount, MEnv.ActualVertexCount);
    Builder.CreateStore(LanePrimitiveCount, MEnv.ActualPrimitiveCount);
  }
}

/// Lowers `feme.stage.input.load` for a mesh entry's one legitimate
/// stage-IO input, SPIR-V's `DrawIndex` builtin (`gl_DrawID`,
/// `SignatureSystemValue::DrawID`, roadmap H6p): unlike a vertex entry's
/// own per-invocation `VertexID`/`InstanceID`, a mesh workgroup's `DrawID`
/// is workgroup-uniform (`FemeMeshArgs::DrawID`, the same value for every
/// invocation in the workgroup), so this simply broadcasts `MEnv.DrawID`
/// to every active lane -- mirroring `FragmentWrapper.cpp`'s
/// `lowerFragmentInputLoad`'s own per-lane masked-select-and-insert shape,
/// but with no varying storage of its own to read: `Row`/`Component`/
/// `Vertex` (`CI`'s other operands) carry no meaning for a whole-builtin
/// scalar like this one, so they are intentionally left unread.
Value *lowerMeshInputLoad(CallInst &CI, const WaveBodyEnv &WEnv,
                          const MeshOutputStageEnv &MEnv) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult =
        Builder.CreateSelect(Active, MEnv.DrawID, Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers `feme.stage.task.payload.load` (roadmap L30): a mesh entry's own
/// bounded payload read, the load-side counterpart of
/// `TaskPayloadWrapper.cpp`'s masked payload store. Usually (`Offset` a
/// real compile-time constant) a task payload is workgroup-shared, not
/// per-lane data -- every lane reads the identical byte range `Offset`
/// selects -- so this reads `MEnv.Payload + Offset` once and broadcasts
/// that single scalar to every active lane's own result slot, mirroring
/// `lowerMeshInputLoad`'s own per-lane broadcast shape immediately above
/// exactly, just reading through the payload buffer instead of
/// `MEnv.DrawID`. But (roadmap L47/L49) `Offset` can also be a genuinely
/// dynamic per-invocation `Value` (mirroring
/// `TaskPayloadWrapper.cpp`'s own `lowerTaskPayloadLoad`'s identical
/// generalization), in which case each lane may read a completely
/// different payload byte range instead of one shared broadcast value --
/// this function keeps the shared-broadcast fast path when `Offset`
/// really is a `ConstantInt` (byte-for-byte identical to its pre-L49
/// behavior), but reads a fresh per-lane scalar otherwise.
Value *lowerMeshTaskPayloadLoad(CallInst &CI, const WaveBodyEnv &WEnv,
                                const MeshOutputStageEnv &MEnv) {
  Value *OffsetArg = CI.getArgOperand(0);
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);

  auto *OffsetConst = dyn_cast<ConstantInt>(OffsetArg);
  Value *SharedScalar = nullptr;
  if (OffsetConst) {
    Value *Addr = Builder.CreateGEP(
        Builder.getInt8Ty(), MEnv.Payload,
        Builder.getInt64(OffsetConst->getZExtValue()));
    SharedScalar = Builder.CreateLoad(ScalarTy, Addr);
  }

  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneScalar = SharedScalar;
    if (!OffsetConst) {
      // (Roadmap L49) A genuinely dynamic per-invocation `Offset`: each
      // lane may read a completely different payload byte range, so read
      // a fresh scalar per lane rather than broadcasting one shared read.
      Value *LaneOffset = extractLaneOrScalar(Builder, OffsetArg, Lane);
      Value *Addr =
          Builder.CreateGEP(Builder.getInt8Ty(), MEnv.Payload, LaneOffset);
      LaneScalar = Builder.CreateLoad(ScalarTy, Addr);
    }
    Value *LaneResult = Builder.CreateSelect(
        Active, LaneScalar, Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers every masked mesh output store and `set_mesh_outputs` call in
/// \p F, or diagnoses and returns false if \p F uses a `feme.stage.*` op
/// this pass does not support (anything other than `OutputStore`/
/// `SetMeshOutputs`/an `InputLoad` of `gl_DrawID`/a `TaskPayloadLoad`
/// (roadmap L30) -- `EmitMeshTasksEXT`
/// (canonicalized as of roadmap H6s) belongs to the *task* stage, not the
/// mesh stage, so `TaskPayloadWrapperPass` lowers it instead, never this
/// pass; roadmap H6p found that a mesh entry point *does* have one
/// legitimate ordinary stage-IO input to read after all, `gl_DrawID`,
/// correcting this comment's original assumption).
bool lowerMeshStageOps(Function &F, const WaveBodyEnv &WEnv) {
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedOutputStoreCall(*CI) ||
                      isMaskedSetMeshOutputsCall(*CI);
  if (!UsesStageOps)
    return true;

  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);
  if (!Sig) {
    F.getContext().emitError(
        "feme-cpu-wrap-mesh-output: mesh output wrapper requires attached "
        "feme.signature metadata");
    return false;
  }

  std::optional<MeshOutputStageEnv> MEnv = getMeshOutputStageEnv(F);
  if (!MEnv)
    return false;

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isMaskedOutputStoreCall(*CI)) {
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Output)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-mesh-output: masked output store references "
                "an unknown signature element");
        return false;
      }
      if (Elt->SystemValue == SignatureSystemValue::PrimitiveIndices)
        lowerMeshPrimitiveIndicesStore(*CI, *Elt, *MEnv);
      else
        lowerMeshOutputStore(*CI, *Elt, *MEnv);
      CI->eraseFromParent();
      continue;
    }
    if (isMaskedSetMeshOutputsCall(*CI)) {
      lowerSetMeshOutputs(*CI, *MEnv);
      CI->eraseFromParent();
      continue;
    }
    StageOpKind Kind;
    if (isStageOpCall(*CI, &Kind) && Kind == StageOpKind::InputLoad) {
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Input)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-mesh-output: input load references an "
                "unknown signature element");
        return false;
      }
      if (Elt->SystemValue != SignatureSystemValue::DrawID) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-mesh-output: unsupported mesh stage input "
                "system value");
        return false;
      }
      Value *Result = lowerMeshInputLoad(*CI, WEnv, *MEnv);
      CI->replaceAllUsesWith(Result);
      CI->eraseFromParent();
      continue;
    }
    if (isStageOpCall(*CI, &Kind) && Kind == StageOpKind::TaskPayloadLoad) {
      Value *Result = lowerMeshTaskPayloadLoad(*CI, WEnv, *MEnv);
      CI->replaceAllUsesWith(Result);
      CI->eraseFromParent();
      continue;
    }
    // Only a genuinely unlowered `feme.stage.*` call is this pass's own
    // problem to diagnose (see the function comment) -- everything else
    // still calling through `F` at this point (resource loads/stores,
    // ordinary masked memory ops, etc.) is unrelated to mesh output
    // lowering and must be left alone rather than rejected outright; the
    // `UsesStageOps` gate above only established that *some* call in `F`
    // needs this pass's attention, not that *every* call does.
    if (!isStageOpCall(*CI))
      continue;
    F.getContext().emitError(
        CI, "feme-cpu-wrap-mesh-output: unexpected stage op left for the "
            "mesh output wrapper");
    return false;
  }
  return true;
}

} // namespace

PreservedAnalyses MeshOutputWrapperPass::run(Module &M,
                                             ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Mesh)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendMeshOutputParams(*F);
    // `appendMeshOutputParams` splices `F`'s body into a brand-new
    // function and erases `F`, so any `WaveBodyEnv` captured against the
    // old function's now-destroyed `Argument`s would dangle -- re-derive
    // it against `Body`, whose spliced-in parameters keep every original
    // `WaveBodyEnv`-recognized name (`wave_entry_mask` et al.) intact.
    std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(*Body);
    assert(WEnv && "getWaveBodyEnv succeeded before appendMeshOutputParams "
                   "but failed after");
    if (lowerMeshStageOps(*Body, *WEnv))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
