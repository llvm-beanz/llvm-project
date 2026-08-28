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
constexpr StringLiteral MaxOutputVerticesParamName =
    "mesh_max_output_vertices";
constexpr StringLiteral MaxOutputPrimitivesParamName =
    "mesh_max_output_primitives";

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
  ParamTypes.append({PtrTy, PtrTy, PtrTy, PtrTy, I32Ty, I32Ty});

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
/// entry point's own runtime value, and nothing yet validates it against
/// the workgroup's *actual* (`SetMeshOutputsEXT`-declared) output count --
/// that op has no canonicalized `feme.stage.*` form yet (roadmap H6c-a-a's
/// own scope note, MeshOutputWrapper.h). Clamping to the declared *maximum*
/// here is what keeps an out-of-range write from corrupting host memory
/// beyond `VertexOutputs`/`PrimitiveOutputs`'s own bounds; a *tighter*
/// bound against the real declared count is left to whichever future row
/// wires `SetMeshOutputsEXT` itself.
Value *clampSlotIndex(IRBuilder<> &Builder, Value *SlotIndex, Value *Max) {
  Value *Zero = Builder.getInt32(0);
  Value *MaxIndex = Builder.CreateSub(Max, Builder.getInt32(1), "max.index");
  Value *MaxIsZero = Builder.CreateICmpEQ(Max, Zero, "max.iszero");
  Value *SafeMaxIndex =
      Builder.CreateSelect(MaxIsZero, Zero, MaxIndex, "safe.max.index");
  return Builder.CreateBinaryIntrinsic(Intrinsic::umin, SlotIndex,
                                       SafeMaxIndex, nullptr, "slot.clamped");
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
  Value *Layout = PerPrimitive ? MEnv.PrimitiveOutputLayout
                               : MEnv.VertexOutputLayout;
  Value *Base = PerPrimitive ? MEnv.PrimitiveOutputs : MEnv.VertexOutputs;
  Value *Max =
      PerPrimitive ? MEnv.MaxOutputPrimitives : MEnv.MaxOutputVertices;
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(5), Lane);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *Row = extractLaneOrScalar(Builder, CI.getArgOperand(1), Lane);
    Value *Component = extractLaneOrScalar(Builder, CI.getArgOperand(2), Lane);
    Value *Slot = extractLaneOrScalar(Builder, CI.getArgOperand(4), Lane);
    Value *ClampedSlot = clampSlotIndex(Builder, Slot, Max);
    Value *Addr = computeMeshOutputAddress(Builder, Layout, Base,
                                           Elt.ElementID, Elt, Row, Component,
                                           ClampedSlot);
    Value *LaneVal = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(LaneVal->getType(), Addr);
      LaneVal = Builder.CreateSelect(Mask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// Lowers every masked mesh output store in \p F, or diagnoses and returns
/// false if \p F uses a `feme.stage.*` op this pass does not support (any
/// op other than `OutputStore` -- a mesh entry point has no ordinary
/// stage-IO input to read, and `SetMeshOutputsEXT`/`EmitMeshTasksEXT` have
/// no canonicalized form yet, see the file comment).
bool lowerMeshStageOps(Function &F) {
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedOutputStoreCall(*CI);
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
      lowerMeshOutputStore(*CI, *Elt, *MEnv);
      CI->eraseFromParent();
      continue;
    }
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
    if (!F.isDeclaration() && feme::getShaderStage(F) == feme::ShaderStage::Mesh)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    Function *Body = appendMeshOutputParams(*F);
    if (lowerMeshStageOps(*Body))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
