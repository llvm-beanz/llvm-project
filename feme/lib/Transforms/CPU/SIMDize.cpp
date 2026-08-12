//===- SIMDize.cpp - CPU target Phase 4: widening ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 4's widening algorithm, for a straight-line,
// uniform-control-flow wave body (an acyclic CFG with no divergent branch --
// loops and divergent control flow are milestones 6/7, see SIMDize.h):
//
//  1. Compute `feme::cpu::computeWaveUniformity` on the function as given
//     (Phase 3, the linearizer, does not exist yet; every branch this
//     milestone accepts is uniform by construction). Bail (leaving the
//     function untouched, with a diagnostic) if the CFG has a cycle or a
//     divergent branch -- this pass's whole simplification depends on both
//     being absent.
//  2. Build the widened function: the same signature plus the wave-body
//     interface parameters ("Wave-body interface" in "Phase 4: Widening"),
//     with the body spliced across unchanged (same technique
//     `feme::cpu::ResourceLoweringPass::addResourceEnvParams` uses).
//  3. Walk every instruction once, in reverse post-order (sufficient for an
//     acyclic CFG: every value is defined before it is used): a divergent
//     instruction gets a widened `<W x T>` replacement built from its
//     operands' widened forms (broadcasting a uniform operand at the point
//     it's first needed); a uniform instruction is left completely alone.
//     Three cases don't fit that generic rule and are special-cased:
//      - The per-lane-varying builtins (thread id, ...) become
//        `feme.cpu.builtin.*` calls (see feme::cpu::BuiltinCalls) -- Phase 5
//        lowers the arithmetic, not this pass.
//      - A `feme.cpu.resource.*` call (Phase 3 would normally mask these,
//        but Phase 3 does not exist yet, so `feme::cpu::ResourceLoweringPass`'s
//        own output can already appear here) with any divergent operand is
//        scalarized: `W` unrolled scalar calls to the same callee,
//        extracting per-lane operands and reassembling a loaded result into
//        a vector (see "Widening" table's "masked feme.cpu.resource.* call"
//        row).
//      - `llvm.{dx,spv}.group.id` is uniform (a group's id is the same for
//        every lane) and is simply replaced by the corresponding wave-body
//        `GroupID` parameter component.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SIMDize.h"

#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace feme::cpu {

std::optional<WaveBodyEnv> getWaveBodyEnv(Function &F) {
  WaveBodyEnv Env;
  bool Found = false;
  for (Argument &Arg : F.args()) {
    if (Arg.getName() == "wave_group_id_x")
      Env.GroupIDX = &Arg, Found = true;
    else if (Arg.getName() == "wave_group_id_y")
      Env.GroupIDY = &Arg, Found = true;
    else if (Arg.getName() == "wave_group_id_z")
      Env.GroupIDZ = &Arg, Found = true;
    else if (Arg.getName() == "wave_index")
      Env.WaveIndex = &Arg, Found = true;
    else if (Arg.getName() == "wave_entry_mask")
      Env.EntryMask = &Arg, Found = true;
    else if (Arg.getName() == "wave_groupshared")
      Env.GroupShared = &Arg, Found = true;
  }
  if (!Found)
    return std::nullopt;
  return Env;
}

} // namespace feme::cpu

namespace {

/// A shader entry point's thread group dimensions, from `hlsl.numthreads`
/// (see feme::dxil::MetadataRaisingPass). Defaults to `{1, 1, 1}` if absent
/// or malformed, matching a single-invocation dispatch rather than failing:
/// every later computation this pass does with it degrades gracefully to
/// "one thread" in that case.
std::array<uint32_t, 3> getThreadGroupSize(const Function &F) {
  std::array<uint32_t, 3> Size{1, 1, 1};
  if (!F.hasFnAttribute("hlsl.numthreads"))
    return Size;
  StringRef NumThreads = F.getFnAttribute("hlsl.numthreads").getValueAsString();
  SmallVector<StringRef, 3> Components;
  NumThreads.split(Components, ',');
  if (Components.size() != 3)
    return Size;
  std::array<uint32_t, 3> Result;
  for (unsigned I = 0; I != 3; ++I)
    if (!llvm::to_integer(Components[I], Result[I], 10))
      return Size;
  return Result;
}

/// The wave size `SIMDizePass` should widen \p F to: the pass's own
/// constructor option if given, else \p F's `feme.cpu.wavesize` attribute
/// (see feme::Driver, "Wave Size Selection"), else `feme::cpu::MinWaveSize`.
unsigned resolveWaveSizeForFunction(const Function &F, unsigned OptionWaveSize) {
  if (OptionWaveSize)
    return OptionWaveSize;
  if (F.hasFnAttribute("feme.cpu.wavesize")) {
    unsigned W;
    if (!F.getFnAttribute("feme.cpu.wavesize")
             .getValueAsString()
             .getAsInteger(10, W))
      return W;
  }
  return MinWaveSize;
}

/// Which per-lane-varying raised builtin \p ID is, if any (see
/// feme::cpu::BuiltinCallKind); `std::nullopt` for anything else, including
/// `llvm.{dx,spv}.group.id` (uniform, handled separately -- see the file
/// comment above).
std::optional<BuiltinCallKind> classifyBuiltin(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dx_thread_id:
  case Intrinsic::spv_thread_id:
    return BuiltinCallKind::ThreadId;
  case Intrinsic::dx_thread_id_in_group:
  case Intrinsic::spv_thread_id_in_group:
    return BuiltinCallKind::ThreadIdInGroup;
  case Intrinsic::dx_flattened_thread_id_in_group:
  case Intrinsic::spv_flattened_thread_id_in_group:
    return BuiltinCallKind::FlattenedThreadIdInGroup;
  case Intrinsic::dx_wave_getlaneindex:
    return BuiltinCallKind::LaneIndex;
  default:
    return std::nullopt;
  }
}

bool isGroupIdCall(Intrinsic::ID ID) {
  return ID == Intrinsic::dx_group_id || ID == Intrinsic::spv_group_id;
}

/// Widens a single acyclic, uniform-control-flow function to \p WaveSize
/// lanes. See the file comment above for the algorithm.
class FunctionWidener {
  Function &OldF;
  unsigned WaveSize;
  UniformityInfo &UI;
  Function *NewF = nullptr;
  WaveBodyEnv Env;
  std::array<uint32_t, 3> NumThreads;

  /// Divergent value (in the *old* function) -> its `<W x T>` replacement
  /// (in the new one).
  DenseMap<Value *, Value *> Widened;
  /// Uniform value -> the broadcast `<W x T>` `getWidened` has already built
  /// for it, memoized so a value used divergently more than once doesn't
  /// grow a broadcast per use.
  DenseMap<Value *, Value *> Broadcasts;

  SmallVector<Instruction *, 16> ToErase;

public:
  FunctionWidener(Function &OldF, unsigned WaveSize, UniformityInfo &UI)
      : OldF(OldF), WaveSize(WaveSize), UI(UI),
        NumThreads(getThreadGroupSize(OldF)) {}

  /// Returns the widened function, or nullptr if \p OldF's CFG isn't the
  /// acyclic, uniform-control-flow shape this milestone supports (a
  /// diagnostic is emitted; \p OldF is left untouched).
  Function *widen(CycleInfo &CI);

private:
  bool checkSupportedControlFlow(CycleInfo &CI);
  Function *buildWidenedFunction();
  Value *getWidened(Value *V, IRBuilderBase &Builder);
  void widenPHI(PHINode &PN);
  void widenBuiltin(CallInst &CI, BuiltinCallKind Kind, IRBuilder<> &Builder);
  void replaceGroupIdCall(CallInst &CI);
  void widenResourceCall(CallInst &CI, const MatchedResourceCall &Matched,
                         IRBuilder<> &Builder);
  void widenElementwise(Instruction &I, IRBuilder<> &Builder);
  bool widenInstruction(Instruction &I, IRBuilder<> &Builder);
};

bool FunctionWidener::checkSupportedControlFlow(CycleInfo &CI) {
  if (!CI.toplevel_cycles().empty()) {
    OldF.getContext().emitError(
        "feme-cpu-simdize: function '" + OldF.getName() +
        "' has a loop; only acyclic, uniform control flow is supported "
        "(roadmap milestone 4)");
    return false;
  }
  for (BasicBlock &BB : OldF) {
    auto *BI = dyn_cast<CondBrInst>(BB.getTerminator());
    if (BI && UI.isDivergentTerminator(BI)) {
      OldF.getContext().emitError(
          "feme-cpu-simdize: function '" + OldF.getName() +
          "' has a divergent branch; the divergence transform is not yet "
          "implemented (roadmap milestone 6)");
      return false;
    }
  }
  return true;
}

Function *FunctionWidener::buildWidenedFunction() {
  LLVMContext &Ctx = OldF.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *MaskTy = FixedVectorType::get(Type::getInt1Ty(Ctx), WaveSize);

  SmallVector<Type *, 8> ParamTypes(OldF.getFunctionType()->params());
  ParamTypes.append({I32Ty, I32Ty, I32Ty, I32Ty, MaskTy, PtrTy});

  FunctionType *NewTy = FunctionType::get(OldF.getReturnType(), ParamTypes,
                                         OldF.getFunctionType()->isVarArg());
  Function *F = Function::Create(NewTy, OldF.getLinkage(),
                                 OldF.getAddressSpace(), "", OldF.getParent());
  F->copyAttributesFrom(&OldF);
  F->setComdat(OldF.getComdat());
  F->splice(F->begin(), &OldF);

  for (auto [OldArg, NewArg] : llvm::zip(OldF.args(), F->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = F->arg_begin() + OldF.arg_size();
  Env.GroupIDX = &*ArgIt++;
  Env.GroupIDX->setName("wave_group_id_x");
  Env.GroupIDY = &*ArgIt++;
  Env.GroupIDY->setName("wave_group_id_y");
  Env.GroupIDZ = &*ArgIt++;
  Env.GroupIDZ->setName("wave_group_id_z");
  Env.WaveIndex = &*ArgIt++;
  Env.WaveIndex->setName("wave_index");
  Env.EntryMask = &*ArgIt++;
  Env.EntryMask->setName("wave_entry_mask");
  Env.GroupShared = &*ArgIt++;
  Env.GroupShared->setName("wave_groupshared");

  F->takeName(&OldF);
  OldF.replaceAllUsesWith(F);
  OldF.eraseFromParent();
  return F;
}

Value *FunctionWidener::getWidened(Value *V, IRBuilderBase &Builder) {
  if (auto It = Widened.find(V); It != Widened.end())
    return It->second;
  if (auto It = Broadcasts.find(V); It != Broadcasts.end())
    return It->second;

  Value *Splat;
  if (auto *C = dyn_cast<Constant>(V)) {
    Splat = ConstantVector::getSplat(ElementCount::getFixed(WaveSize), C);
  } else if (auto *I = dyn_cast<Instruction>(V)) {
    IRBuilder<> B(I->getParent(), std::next(I->getIterator()));
    Splat = B.CreateVectorSplat(WaveSize, V, V->getName() + ".splat");
  } else {
    // An `Argument`: broadcast at the widened function's entry, which
    // dominates every use.
    IRBuilder<> B(&*NewF->getEntryBlock().getFirstInsertionPt());
    Splat = B.CreateVectorSplat(WaveSize, V, V->getName() + ".splat");
  }
  Broadcasts[V] = Splat;
  return Splat;
}

void FunctionWidener::widenPHI(PHINode &PN) {
  if (!UI.isDivergentAtDef(&PN))
    return;

  Type *WideTy = FixedVectorType::get(PN.getType(), WaveSize);
  PHINode *NewPN = PHINode::Create(WideTy, PN.getNumIncomingValues(),
                                   PN.getName() + ".wide");
  NewPN->insertBefore(PN.getIterator());
  // Deferred: the incoming values are widened lazily below, once every
  // predecessor block has actually been processed (this pass never visits a
  // block before all of its non-back-edge predecessors, since the CFG this
  // milestone accepts is acyclic -- see `checkSupportedControlFlow`).
  for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
    IRBuilder<> IncomingBuilder(PN.getIncomingBlock(I)->getTerminator());
    NewPN->addIncoming(getWidened(PN.getIncomingValue(I), IncomingBuilder),
                       PN.getIncomingBlock(I));
  }
  Widened[&PN] = NewPN;
  ToErase.push_back(&PN);
}

void FunctionWidener::widenBuiltin(CallInst &CI, BuiltinCallKind Kind,
                                  IRBuilder<> &Builder) {
  BuiltinCallEnv BEnv;
  BEnv.GroupIDX = Env.GroupIDX;
  BEnv.GroupIDY = Env.GroupIDY;
  BEnv.GroupIDZ = Env.GroupIDZ;
  BEnv.WaveIndex = Env.WaveIndex;

  unsigned Component = 0;
  if (Kind == BuiltinCallKind::ThreadId ||
      Kind == BuiltinCallKind::ThreadIdInGroup)
    Component = static_cast<unsigned>(
        cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue());

  CallInst *NewCall =
      createBuiltinCall(Builder, Kind, BEnv, WaveSize, NumThreads[0],
                        NumThreads[1], NumThreads[2], Component, CI.getName());
  Widened[&CI] = NewCall;
  ToErase.push_back(&CI);
}

void FunctionWidener::replaceGroupIdCall(CallInst &CI) {
  unsigned Component = static_cast<unsigned>(
      cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue());
  Value *Replacement =
      Component == 0 ? Env.GroupIDX : Component == 1 ? Env.GroupIDY : Env.GroupIDZ;
  CI.replaceAllUsesWith(Replacement);
  ToErase.push_back(&CI);
}

void FunctionWidener::widenResourceCall(CallInst &CI,
                                        const MatchedResourceCall &Matched,
                                        IRBuilder<> &Builder) {
  bool AnyDivergent =
      Widened.count(Matched.DescriptorIndex) || Widened.count(Matched.Offset) ||
      (Matched.StoredValue && Widened.count(Matched.StoredValue));
  if (!AnyDivergent)
    return; // Every operand is uniform: leave the scalar call as-is.

  // Scalarize: call the same scalar callee once per lane, feeding it that
  // lane's extracted operand values, ANDing the always-true scalar mask
  // operand with this wave's entry mask so an inactive lane (e.g. a partial
  // last wave) never touches memory (see "masked feme.cpu.resource.* call"
  // in "Phase 4: Widening").
  Function *Callee = CI.getCalledFunction();
  Value *WideDescriptorIndex = getWidened(Matched.DescriptorIndex, Builder);
  Value *WideOffset = getWidened(Matched.Offset, Builder);
  Value *WideStoredValue =
      Matched.StoredValue ? getWidened(Matched.StoredValue, Builder) : nullptr;

  Value *Result = nullptr;
  if (!Matched.StoredValue)
    Result = PoisonValue::get(FixedVectorType::get(CI.getType(), WaveSize));

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *LaneMask = Builder.CreateExtractElement(
        Env.EntryMask, Builder.getInt32(Lane), "lane.mask");
    Value *LaneDescriptorIndex = Builder.CreateExtractElement(
        WideDescriptorIndex, Builder.getInt32(Lane), "lane.desc");
    Value *LaneOffset = Builder.CreateExtractElement(
        WideOffset, Builder.getInt32(Lane), "lane.offset");

    SmallVector<Value *, 6> CallArgs;
    CallArgs.push_back(CI.getArgOperand(0)); // ResourceHeap
    CallArgs.push_back(CI.getArgOperand(1)); // ResourceHeapCount
    CallArgs.push_back(LaneDescriptorIndex);
    CallArgs.push_back(LaneOffset);
    if (WideStoredValue)
      CallArgs.push_back(Builder.CreateExtractElement(
          WideStoredValue, Builder.getInt32(Lane), "lane.value"));
    CallArgs.push_back(LaneMask);

    Value *LaneResult = Builder.CreateCall(Callee, CallArgs);
    if (Result)
      Result = Builder.CreateInsertElement(Result, LaneResult,
                                           Builder.getInt32(Lane));
  }

  if (Result)
    Widened[&CI] = Result;
  ToErase.push_back(&CI);
}

void FunctionWidener::widenElementwise(Instruction &I, IRBuilder<> &Builder) {
  SmallVector<Value *, 4> WideOps;
  for (Value *Op : I.operands())
    WideOps.push_back(getWidened(Op, Builder));

  Value *NewI = nullptr;
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    NewI = Builder.CreateBinOp(BO->getOpcode(), WideOps[0], WideOps[1],
                              I.getName() + ".wide");
  } else if (auto *Cmp = dyn_cast<CmpInst>(&I)) {
    NewI = Builder.CreateCmp(Cmp->getPredicate(), WideOps[0], WideOps[1],
                            I.getName() + ".wide");
  } else if (auto *Cast = dyn_cast<CastInst>(&I)) {
    Type *WideTy = FixedVectorType::get(I.getType(), WaveSize);
    NewI = Builder.CreateCast(Cast->getOpcode(), WideOps[0], WideTy,
                             I.getName() + ".wide");
  } else if (isa<SelectInst>(&I)) {
    NewI = Builder.CreateSelect(WideOps[0], WideOps[1], WideOps[2],
                               I.getName() + ".wide");
  } else if (auto *UO = dyn_cast<UnaryOperator>(&I)) {
    NewI =
        Builder.CreateUnOp(UO->getOpcode(), WideOps[0], I.getName() + ".wide");
  } else {
    OldF.getContext().emitError(
        "feme-cpu-simdize: unsupported divergent instruction '" +
        Twine(I.getOpcodeName()) + "' (roadmap milestone 7)");
    return;
  }
  Widened[&I] = NewI;
  ToErase.push_back(&I);
}

bool FunctionWidener::widenInstruction(Instruction &I, IRBuilder<> &Builder) {
  if (auto *CI = dyn_cast<CallInst>(&I)) {
    if (std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI)) {
      widenResourceCall(*CI, *Matched, Builder);
      return true;
    }
    Intrinsic::ID ID = CI->getCalledFunction()
                          ? CI->getCalledFunction()->getIntrinsicID()
                          : Intrinsic::not_intrinsic;
    if (isGroupIdCall(ID)) {
      replaceGroupIdCall(*CI);
      return true;
    }
    if (std::optional<BuiltinCallKind> Kind = classifyBuiltin(ID)) {
      widenBuiltin(*CI, *Kind, Builder);
      return true;
    }
  }

  if (!UI.isDivergentAtDef(&I))
    return true; // Uniform: leave it exactly as it is.

  if (isa<CondBrInst>(I) || isa<UncondBrInst>(I) || isa<ReturnInst>(I))
    return true; // Handled/verified by checkSupportedControlFlow already.

  widenElementwise(I, Builder);
  return true;
}

Function *FunctionWidener::widen(CycleInfo &CI) {
  if (!checkSupportedControlFlow(CI))
    return nullptr;

  NewF = buildWidenedFunction();

  ReversePostOrderTraversal<Function *> RPOT(NewF);
  for (BasicBlock *BB : RPOT) {
    for (Instruction &PN : make_early_inc_range(*BB)) {
      if (auto *Phi = dyn_cast<PHINode>(&PN))
        widenPHI(*Phi);
      else
        break; // PHIs are always at the top of a block.
    }
    for (Instruction &I : make_early_inc_range(*BB)) {
      if (isa<PHINode>(I))
        continue;
      IRBuilder<> Builder(&I);
      widenInstruction(I, Builder);
    }
  }

  for (Instruction *I : llvm::reverse(ToErase))
    I->eraseFromParent();

  return NewF;
}

} // namespace

PreservedAnalyses SIMDizePass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  // Snapshot the functions to widen before mutating the module:
  // `FunctionWidener::widen` replaces a function with a new one (different
  // signature) appended at the end of `M`'s function list, which a
  // `make_early_inc_range` over that same list would otherwise walk into
  // and re-widen.
  SmallVector<Function *, 4> Entries;
  for (Function &F : M)
    if (!F.isDeclaration() && F.hasFnAttribute("hlsl.shader"))
      Entries.push_back(&F);

  for (Function *F : Entries) {
    unsigned W = resolveWaveSizeForFunction(*F, WaveSize);
    DominatorTree DT(*F);
    CycleInfo CI;
    CI.compute(*F);
    UniformityInfo UI = computeWaveUniformity(*F, DT, CI);

    FunctionWidener Widener(*F, W, UI);
    if (Widener.widen(CI))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
