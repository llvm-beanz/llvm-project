//===- TaskPayloadWrapper.cpp - Task stage payload store lowering --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See TaskPayloadWrapper.h for this pass's scope and roadmap H6c-a-b's own
// design notes.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/TaskPayloadWrapper.h"

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
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme;
using namespace feme::cpu;

namespace {

constexpr StringLiteral PayloadParamName = "task_payload";
constexpr StringLiteral MaxPayloadBytesParamName = "task_max_payload_bytes";
/// (Roadmap H6s) `FemeTaskArgs::MeshGroupCount`, the trailing parameter
/// this pass appends alongside `Payload`/`MaxPayloadBytes` for a
/// canonicalized `feme.stage.emit_mesh_tasks` call to write through.
constexpr StringLiteral MeshGroupCountParamName = "task_mesh_group_count";
/// (Roadmap H6t) `FemeTaskArgs::DrawID`, SPIR-V's `DrawIndex` builtin
/// (`gl_DrawID`): a task entry's one legitimate ordinary stage-IO input,
/// mirroring `MeshOutputWrapper.cpp`'s own `DrawIDParamName` (roadmap
/// H6p) exactly -- see that file's own comment for why this builtin is
/// workgroup-uniform rather than per-lane.
constexpr StringLiteral DrawIDParamName = "task_draw_id";

const SignatureElement *findElement(const EntrySignature &Sig,
                                    uint32_t ElementID,
                                    SignatureDirection Dir) {
  for (const SignatureElement &Elt : Sig.Elements)
    if (Elt.ElementID == ElementID && Elt.Direction == Dir)
      return &Elt;
  return nullptr;
}

/// This pass's own trailing wave-body parameters (see the file comment):
/// the payload's base pointer and its runtime-bound byte count.
struct TaskPayloadStageEnv {
  Value *Payload = nullptr;
  Value *MaxPayloadBytes = nullptr;
  /// (Roadmap H6s) `FemeTaskArgs::MeshGroupCount`: the `uint32_t*` this
  /// pass's `lowerEmitMeshTasks` writes the requested mesh dispatch's 3D
  /// group count through, addressing one contiguous 3-element block (see
  /// `RuntimeABI.h`'s own comment on the field).
  Value *MeshGroupCount = nullptr;
  /// (Roadmap H6t) `FemeTaskArgs::DrawID`, workgroup-uniform, threaded
  /// through unchanged from `EntryWrapper.cpp`'s own `Env.TaskDrawID`.
  Value *DrawID = nullptr;
};

std::optional<TaskPayloadStageEnv> getTaskPayloadStageEnv(Function &F) {
  TaskPayloadStageEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == PayloadParamName)
      Env.Payload = &Arg, Found = true;
    else if (Arg.getName() == MaxPayloadBytesParamName)
      Env.MaxPayloadBytes = &Arg, Found = true;
    else if (Arg.getName() == MeshGroupCountParamName)
      Env.MeshGroupCount = &Arg, Found = true;
    else if (Arg.getName() == DrawIDParamName)
      Env.DrawID = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

/// Appends this pass's own trailing parameters to \p F, splicing its body
/// into a freshly-created function -- the same "grow the signature,
/// preserve the body" shape every other stage wrapper's own
/// `append*StageParams` already uses (see e.g. `MeshOutputWrapper.cpp`'s
/// `appendMeshOutputParams`).
Function *appendTaskPayloadParams(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, I32Ty, PtrTy, I32Ty});

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
  (&*ArgIt++)->setName(PayloadParamName);
  (&*ArgIt++)->setName(MaxPayloadBytesParamName);
  (&*ArgIt++)->setName(MeshGroupCountParamName);
  (&*ArgIt++)->setName(DrawIDParamName);

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
/// `lowerEmitMeshTasks` below to identify the one lane that is truly
/// SPIR-V invocation 0 (`WaveLowering.cpp`'s `buildFlattenedThreadIdInGroup`
/// computes a flattened invocation id of `wave_index * WaveSize + lane`, so
/// invocation 0 is exactly `wave_index == 0 && lane == 0`) -- mirroring
/// `MeshOutputWrapper.cpp`'s own `getWaveIndexArg`.
Value *getWaveIndexArg(Function &F) {
  for (Argument &Arg : F.args())
    if (Arg.getName() == "wave_index")
      return &Arg;
  return nullptr;
}

/// Lowers one `feme.cpu.masked.task.payload.store` call: every active lane
/// stores its own value at `Env.Payload + Offset`. `Offset` is usually a
/// single compile-time constant shared by every lane of this call (per
/// `StageOpKind::TaskPayloadStore`'s own comment -- not a per-lane value
/// the way a mesh output store's `Vertex` operand is), the same
/// "every lane may write, the mask decides whose value survives" shape
/// `MeshOutputWrapper.cpp`'s `lowerMeshOutputStore` already uses, just
/// against one fixed address instead of one address per output slot. But
/// (roadmap L47/L49) `Offset` can also be a genuinely dynamic
/// per-invocation `Value` (e.g. `CanonicalizeStage.cpp`'s
/// `getTaskPayloadDynamicOffsetAccess`, for a shader indexing its payload
/// by `gl_LocalInvocationIndex`), in which case each lane may address a
/// completely different payload byte range -- this function keeps the
/// original shared-address fast path when `Offset` really is a
/// `ConstantInt` (byte-for-byte identical to its pre-L49 behavior), but
/// falls back to computing a fresh per-lane address/bounds check inside
/// the loop otherwise.
///
/// Defensively skips a store entirely (leaving `Env.Payload` at that
/// offset untouched) if `Offset` plus this value's own byte size would
/// exceed `Env.MaxPayloadBytes`: `Offset` is resolved against the
/// *shader's* own declared payload type at canonicalization time (roadmap
/// H6h/H6i), not against this particular dispatch's own bound, so an
/// oversized payload type is still caught here rather than corrupting
/// host memory beyond `Env.Payload`'s own allocation -- mirroring
/// `MeshOutputWrapper.cpp`'s own `clampSlotIndex` precedent for the same
/// "a canonicalized store's own operand is not yet validated against this
/// dispatch's real runtime bound" gap.
void lowerTaskPayloadStore(CallInst &CI, const TaskPayloadStageEnv &Env,
                          const DataLayout &DL) {
  IRBuilder<> Builder(&CI);
  Value *OffsetArg = CI.getArgOperand(0);
  Value *ValueArg = CI.getArgOperand(1);
  Value *MaskArg = CI.getArgOperand(2);

  auto *WideTy = dyn_cast<FixedVectorType>(ValueArg->getType());
  unsigned WaveSize = WideTy ? WideTy->getNumElements() : 1;
  Type *ScalarTy = WideTy ? WideTy->getElementType() : ValueArg->getType();
  uint64_t ByteSize = DL.getTypeStoreSize(ScalarTy).getFixedValue();

  auto *OffsetConst = dyn_cast<ConstantInt>(OffsetArg);
  Value *SharedAddr = nullptr, *SharedInBounds = nullptr;
  if (OffsetConst) {
    uint64_t Offset = OffsetConst->getZExtValue();
    Value *End = Builder.getInt32(static_cast<uint32_t>(Offset + ByteSize));
    SharedInBounds =
        Builder.CreateICmpULE(End, Env.MaxPayloadBytes, "payload.inbounds");
    SharedAddr = Builder.CreateInBoundsGEP(
        Builder.getInt8Ty(), Env.Payload,
        Builder.getInt32(static_cast<uint32_t>(Offset)), "payload.addr");
  }

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Addr = SharedAddr;
    Value *InBounds = SharedInBounds;
    if (!OffsetConst) {
      // (Roadmap L49) A genuinely dynamic, potentially per-lane-divergent
      // `Offset`: unlike the shared-address fast path above, each lane may
      // address a completely different payload byte range, so recompute a
      // fresh address/bounds check per lane rather than hoisting one
      // shared pair.
      Value *LaneOffset = extractLaneOrScalar(Builder, OffsetArg, Lane);
      Value *End = Builder.CreateAdd(
          LaneOffset, Builder.getInt32(static_cast<uint32_t>(ByteSize)));
      InBounds =
          Builder.CreateICmpULE(End, Env.MaxPayloadBytes, "payload.inbounds");
      Addr = Builder.CreateInBoundsGEP(Builder.getInt8Ty(), Env.Payload,
                                       LaneOffset, "payload.addr");
    }

    Value *Mask = extractLaneOrScalar(Builder, MaskArg, Lane);
    Value *EffectiveMask = Builder.CreateAnd(Mask, InBounds, "payload.mask");
    auto *MaskConst = dyn_cast<ConstantInt>(EffectiveMask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *LaneVal = extractLaneOrScalar(Builder, ValueArg, Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      Value *OldVal = Builder.CreateLoad(ScalarTy, Addr);
      LaneVal = Builder.CreateSelect(EffectiveMask, LaneVal, OldVal);
    }
    Builder.CreateStore(LaneVal, Addr);
  }
}

/// Lowers one `feme.cpu.masked.emit_mesh_tasks` call (roadmap H6s): writes
/// `Env.MeshGroupCount`'s three contiguous slots from the one lane that is
/// truly SPIR-V invocation 0 (`wave_index == 0 && Lane == 0`, see
/// `getWaveIndexArg`'s own comment). `EmitMeshTasksEXT`, like
/// `SetMeshOutputsEXT` (`MeshOutputWrapper.cpp`'s `lowerSetMeshOutputs`,
/// roadmap H85's own re-investigation), is spec'd to be called *only* from
/// invocation 0, but this test suite's own generated task shaders call it
/// unconditionally -- every other invocation reaches the same call with
/// its own un-set-by-the-shader-body `(0, 0, 0)` default, so the naive
/// "every active lane's value is idempotent" assumption this comment's own
/// prior revision made does not hold: whichever lane's write lands last in
/// iteration order wins, non-deterministically zeroing out invocation 0's
/// real dispatch request. Gating on the true flattened invocation id, not
/// merely the call site's own reachability mask, is what actually
/// implements the spec's "invocation 0 only" contract.
void lowerEmitMeshTasks(CallInst &CI, const TaskPayloadStageEnv &Env) {
  IRBuilder<> Builder(&CI);
  Value *GroupCountXArg = CI.getArgOperand(0);
  auto *WideTy = dyn_cast<FixedVectorType>(GroupCountXArg->getType());
  unsigned WaveSize = WideTy ? WideTy->getNumElements() : 1;
  Type *ScalarTy = WideTy ? WideTy->getElementType() : GroupCountXArg->getType();

  Value *Addrs[3];
  for (unsigned Dim = 0; Dim != 3; ++Dim)
    Addrs[Dim] = Builder.CreateInBoundsGEP(
        ScalarTy, Env.MeshGroupCount, Builder.getInt32(Dim),
        "mesh.group.count.addr");

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

    Value *Mask = extractLaneOrScalar(Builder, CI.getArgOperand(3), Lane);
    Mask = Builder.CreateAnd(Mask, IsWaveZero);
    auto *MaskConst = dyn_cast<ConstantInt>(Mask);
    if (MaskConst && MaskConst->isZero())
      continue;

    Value *LaneCounts[3];
    for (unsigned Dim = 0; Dim != 3; ++Dim)
      LaneCounts[Dim] =
          extractLaneOrScalar(Builder, CI.getArgOperand(Dim), Lane);
    if (!(MaskConst && MaskConst->isOne())) {
      for (unsigned Dim = 0; Dim != 3; ++Dim) {
        Value *OldCount = Builder.CreateLoad(ScalarTy, Addrs[Dim]);
        LaneCounts[Dim] =
            Builder.CreateSelect(Mask, LaneCounts[Dim], OldCount);
      }
    }
    for (unsigned Dim = 0; Dim != 3; ++Dim)
      Builder.CreateStore(LaneCounts[Dim], Addrs[Dim]);
  }
}

/// Lowers `feme.stage.input.load` for a task entry's one legitimate
/// stage-IO input, SPIR-V's `DrawIndex` builtin (`gl_DrawID`,
/// `SignatureSystemValue::DrawID`, roadmap H6t): workgroup-uniform,
/// mirroring `MeshOutputWrapper.cpp`'s own `lowerMeshInputLoad` (roadmap
/// H6p) exactly, including that `Row`/`Component`/`Vertex` (`CI`'s other
/// operands) carry no meaning for a whole-builtin scalar like this one and
/// are intentionally left unread.
Value *lowerTaskInputLoad(CallInst &CI, const WaveBodyEnv &WEnv,
                          const TaskPayloadStageEnv &Env) {
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);
  Value *Result = PoisonValue::get(CI.getType());
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *Active =
        Builder.CreateExtractElement(WEnv.EntryMask, Builder.getInt32(Lane));
    Value *LaneResult =
        Builder.CreateSelect(Active, Env.DrawID, Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers one `feme.stage.task.payload.load` call (roadmap L39): a task/
/// amplification entry's own bounded payload *read*, the store-side
/// counterpart of `lowerTaskPayloadStore` above -- newly reachable once
/// `CanonicalizeStage.cpp`'s task-payload fallback started fully
/// decomposing an aggregate-typed payload access into scalar leaves,
/// which surfaces a genuine `TaskPayloadLoad` here for the first time
/// whenever a task/amplification entry's own payload variable is read
/// back within the same invocation (e.g. `DispatchMesh`'s own payload
/// argument, which DXC/SPIRV-Tools passes by first reading the whole
/// payload struct back into a temporary and copying it into itself,
/// rather than the mesh-stage-only read this pass had never needed to
/// support before). Usually (`Offset` a real compile-time constant), a
/// task payload is workgroup-shared, not per-lane data -- every lane reads
/// the identical byte range `Offset` selects -- so this reads
/// `Env.Payload + Offset` once and broadcasts that single scalar to every
/// active lane's own result slot, mirroring
/// `MeshOutputWrapper.cpp`'s own `lowerMeshTaskPayloadLoad` shape exactly
/// (just against this stage's own `WaveBodyEnv`/`TaskPayloadStageEnv` pair
/// instead of `MeshOutputStageEnv`). But (roadmap L47/L49) `Offset` can
/// also be a genuinely dynamic per-invocation `Value`, in which case each
/// lane may read a completely different payload byte range instead of one
/// shared broadcast value -- this function keeps the shared-broadcast
/// fast path when `Offset` really is a `ConstantInt` (byte-for-byte
/// identical to its pre-L49 behavior), but reads a fresh per-lane scalar
/// otherwise.
Value *lowerTaskPayloadLoad(CallInst &CI, const WaveBodyEnv &WEnv,
                            const TaskPayloadStageEnv &Env) {
  Value *OffsetArg = CI.getArgOperand(0);
  unsigned WaveSize = cast<FixedVectorType>(CI.getType())->getNumElements();
  Type *ScalarTy = cast<VectorType>(CI.getType())->getElementType();
  IRBuilder<> Builder(&CI);

  auto *OffsetConst = dyn_cast<ConstantInt>(OffsetArg);
  Value *SharedScalar = nullptr;
  if (OffsetConst) {
    Value *Addr = Builder.CreateGEP(
        Builder.getInt8Ty(), Env.Payload,
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
          Builder.CreateGEP(Builder.getInt8Ty(), Env.Payload, LaneOffset);
      LaneScalar = Builder.CreateLoad(ScalarTy, Addr);
    }
    Value *LaneResult = Builder.CreateSelect(
        Active, LaneScalar, Constant::getNullValue(ScalarTy));
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }
  return Result;
}

/// Lowers every masked task payload store, `emit_mesh_tasks` call, and
/// `gl_DrawID` input load in \p F, or diagnoses and returns false if \p F
/// uses a `feme.stage.*` op this pass does not support (any op other than
/// `TaskPayloadStore`/`TaskPayloadLoad`/`EmitMeshTasks`/an `InputLoad` of
/// `gl_DrawID` -- roadmap H6t found that, mirroring
/// `MeshOutputWrapper.cpp`'s own H6p finding, a task entry point *does*
/// have one legitimate ordinary stage-IO input to read after all; roadmap
/// L39 found that it also needs to support reading its own payload back).
bool lowerTaskPayloadStageOps(Function &F, const WaveBodyEnv &WEnv,
                              const DataLayout &DL) {
  bool UsesStageOps = false;
  for (Instruction &I : instructions(F))
    if (auto *CI = dyn_cast<CallInst>(&I))
      UsesStageOps |= isStageOpCall(*CI) || isMaskedTaskPayloadStoreCall(*CI) ||
                      isMaskedEmitMeshTasksCall(*CI);
  if (!UsesStageOps)
    return true;

  std::optional<TaskPayloadStageEnv> Env = getTaskPayloadStageEnv(F);
  if (!Env)
    return false;

  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    if (isMaskedTaskPayloadStoreCall(*CI)) {
      lowerTaskPayloadStore(*CI, *Env, DL);
      CI->eraseFromParent();
      continue;
    }
    if (isMaskedEmitMeshTasksCall(*CI)) {
      lowerEmitMeshTasks(*CI, *Env);
      CI->eraseFromParent();
      continue;
    }
    StageOpKind Kind;
    if (isStageOpCall(*CI, &Kind) && Kind == StageOpKind::InputLoad) {
      if (!Sig) {
        F.getContext().emitError(
            "feme-cpu-wrap-task-payload: task payload wrapper requires "
            "attached feme.signature metadata to lower an input load");
        return false;
      }
      auto *EltID = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      const SignatureElement *Elt =
          EltID
              ? findElement(*Sig, static_cast<uint32_t>(EltID->getZExtValue()),
                            SignatureDirection::Input)
              : nullptr;
      if (!Elt) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-task-payload: input load references an "
                "unknown signature element");
        return false;
      }
      if (Elt->SystemValue != SignatureSystemValue::DrawID) {
        F.getContext().emitError(
            CI, "feme-cpu-wrap-task-payload: unsupported task stage input "
                "system value");
        return false;
      }
      Value *Result = lowerTaskInputLoad(*CI, WEnv, *Env);
      CI->replaceAllUsesWith(Result);
      CI->eraseFromParent();
      continue;
    }
    if (isStageOpCall(*CI, &Kind) && Kind == StageOpKind::TaskPayloadLoad) {
      Value *Result = lowerTaskPayloadLoad(*CI, WEnv, *Env);
      CI->replaceAllUsesWith(Result);
      CI->eraseFromParent();
      continue;
    }
    // Only a genuinely unlowered `feme.stage.*` call is this pass's own
    // problem to diagnose (see the function comment) -- everything else
    // still calling through `F` at this point (resource loads/stores,
    // ordinary masked memory ops, arithmetic feeding a payload store's or
    // `EmitMeshTasksEXT`'s own operands, etc.) is unrelated to task
    // payload/mesh-dispatch lowering and must be left alone rather than
    // rejected outright (roadmap H6s, mirroring `MeshOutputWrapperPass`'s
    // own H6g-b-d precedent exactly): the `UsesStageOps` gate above only
    // established that *some* call in `F` needs this pass's attention, not
    // that *every* call does.
    if (!isStageOpCall(*CI))
      continue;
    F.getContext().emitError(
        CI, "feme-cpu-wrap-task-payload: unexpected stage op left for the "
            "task payload wrapper");
    return false;
  }
  return true;
}

} // namespace

PreservedAnalyses TaskPayloadWrapperPass::run(Module &M,
                                              ModuleAnalysisManager &) {
  bool Changed = false;
  SmallVector<Function *, 4> Candidates;
  for (Function &F : M)
    if (!F.isDeclaration() &&
        feme::getShaderStage(F) == feme::ShaderStage::Amplification)
      Candidates.push_back(&F);

  for (Function *F : Candidates) {
    if (!getWaveBodyEnv(*F))
      continue;
    const DataLayout &DL = F->getDataLayout();
    Function *Body = appendTaskPayloadParams(*F);
    // `appendTaskPayloadParams` splices `F`'s body into a brand-new
    // function and erases `F`, so any `WaveBodyEnv` captured against the
    // old function's now-destroyed `Argument`s would dangle -- re-derive
    // it against `Body`, whose spliced-in parameters keep every original
    // `WaveBodyEnv`-recognized name (`wave_entry_mask` et al.) intact
    // (mirroring `MeshOutputWrapperPass::run`'s own precedent exactly).
    std::optional<WaveBodyEnv> WEnv = getWaveBodyEnv(*Body);
    assert(WEnv && "getWaveBodyEnv succeeded before appendTaskPayloadParams "
                   "but failed after");
    if (lowerTaskPayloadStageOps(*Body, *WEnv, DL))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
