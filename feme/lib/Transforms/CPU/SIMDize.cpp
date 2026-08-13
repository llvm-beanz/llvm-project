//===- SIMDize.cpp - CPU target Phase 4: widening ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 4's widening algorithm, generalized by milestone 7 to a
// wave body that has already been through `feme::cpu::LinearizePass` (Phase
// 3): a CFG with no divergent branch, but now possibly a loop whose backedge
// is gated by `feme.cpu.mask.any` of a loop-carried mask.
//
//  1. Compute `feme::cpu::computeWaveUniformity` on the function as given.
//     Bail (leaving the function untouched, with a diagnostic) if a
//     divergent branch remains -- this pass's whole simplification depends
//     on control flow itself being uniform; a loop is fine as long as it has
//     none (see "Loops with divergent exits" in "Phase 3").
//  2. Build the widened function: the same signature plus the wave-body
//     interface parameters ("Wave-body interface" in "Phase 4: Widening"),
//     with the body spliced across unchanged (same technique
//     `feme::cpu::ResourceLoweringPass::addResourceEnvParams` uses).
//  3. Walk every instruction: a divergent instruction gets a widened
//     `<W x T>` replacement built from its operands' widened forms
//     (broadcasting a uniform operand at the point it's first needed); a
//     uniform instruction is left completely alone. Every divergent `phi`
//     across the whole function gets its (empty) widened replacement first,
//     in its own pass, so a loop header `phi`'s backedge incoming value --
//     not yet widened when the header is first reached in reverse post-order
//     -- resolves to the real widened value rather than a stale broadcast of
//     the soon-to-be-erased scalar one; the incoming values themselves are
//     filled in a third and final pass. A handful of cases don't fit the
//     generic elementwise rule and are special-cased:
//      - The per-lane-varying builtins (thread id, ...) become
//        `feme.cpu.builtin.*` calls (see feme::cpu::BuiltinCalls) -- Phase 5
//        lowers the arithmetic, not this pass.
//      - A `feme.cpu.resource.*` call with any divergent operand is
//        scalarized: `W` unrolled scalar calls to the same callee,
//        extracting per-lane operands and reassembling a loaded result into
//        a vector (see "Widening" table's "masked feme.cpu.resource.* call"
//        row), ANDing the call's own (possibly divergent, once
//        `feme::cpu::LinearizePass` has masked it) governing mask into the
//        wave's entry mask.
//      - `feme.cpu.mask.any` is uniform (`feme::cpu::WaveTTIImpl` classifies
//        it that way) but its operand isn't: it is lowered in place to
//        `llvm.vector.reduce.or` over the widened operand, the real
//        cross-lane reduction it stands in for.
//      - `llvm.{dx,spv}.group.id` is uniform (a group's id is the same for
//        every lane) and is simply replaced by the corresponding wave-body
//        `GroupID` parameter component.
//
// Any divergent instruction the elementwise rule and these special cases
// don't otherwise cover -- atomics, chiefly -- falls back to a generic,
// per-lane scalarization: `W` clones of the instruction, each fed its
// lane's extracted scalar operands, with the per-lane results reassembled
// into a vector (see `FunctionWidener::widenScalarizedFallback`, "the
// scalarization fallback" in roadmap milestone 7). This is what makes
// widening total: it never has to reject an unsupported divergent opcode.
// A divergent call not otherwise recognized (e.g. a math libcall) is the
// one exception -- its callee is one of its own operands, which the
// generic fallback does not know to leave alone -- so it remains an error.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SIMDize.h"

#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/MaskIntrinsics.h"
#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
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
unsigned resolveWaveSizeForFunction(const Function &F,
                                    unsigned OptionWaveSize) {
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

  /// Returns the widened function, or nullptr if \p OldF has a divergent
  /// branch left unhandled by `feme::cpu::LinearizePass` (a diagnostic is
  /// emitted; \p OldF is left untouched).
  Function *widen();

private:
  bool checkSupportedControlFlow();
  Function *buildWidenedFunction();
  Value *getWidened(Value *V, IRBuilderBase &Builder);
  PHINode *createWidenedPHIStub(PHINode &PN);
  void fillWidenedPHIIncoming(PHINode &PN, PHINode &NewPN);
  void widenBuiltin(CallInst &CI, BuiltinCallKind Kind, IRBuilder<> &Builder);
  void replaceGroupIdCall(CallInst &CI);
  void widenResourceCall(CallInst &CI, const MatchedResourceCall &Matched,
                         IRBuilder<> &Builder);
  void widenMaskAny(CallInst &CI, IRBuilder<> &Builder);
  void widenElementwise(Instruction &I, IRBuilder<> &Builder);
  void widenScalarizedFallback(Instruction &I, IRBuilder<> &Builder);
  bool widenInstruction(Instruction &I, IRBuilder<> &Builder);
};

bool FunctionWidener::checkSupportedControlFlow() {
  // A loop is supported as of roadmap milestone 7 provided it has no
  // divergent branch left in it: `feme::cpu::LinearizePass`'s
  // `LoopLinearizer` turns a loop's own divergent exit check into an
  // unconditional continuation gated by a loop-carried mask, with the
  // backedge condition itself made uniform (`feme.cpu.mask.any`, classified
  // `AlwaysUniform` by `feme::cpu::WaveTTIImpl`) -- so the divergent-branch
  // check below is what actually decides whether a cycle is widenable, not
  // the mere presence of one.
  for (BasicBlock &BB : OldF) {
    auto *BI = dyn_cast<CondBrInst>(BB.getTerminator());
    if (BI && UI.isDivergentTerminator(BI)) {
      OldF.getContext().emitError(
          "feme-cpu-simdize: function '" + OldF.getName() +
          "' has a divergent branch; the divergence transform "
          "(feme::cpu::LinearizePass) did not remove it, or produced a "
          "shape this pass cannot widen");
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
  } else if (auto *PN = dyn_cast<PHINode>(V)) {
    // A `phi`'s broadcast cannot be inserted right after it the way any
    // other instruction's can: another `phi` may follow it in the same
    // block (every `phi` must stay grouped at the block's top), and, for a
    // loop header specifically, "right after" could even be read as before
    // the block's other incoming edges are done executing. The first
    // non-`phi` insertion point is always valid: it dominates every
    // instruction in the block, including a divergent one this same call
    // might be widening operands for.
    IRBuilder<> B(&*PN->getParent()->getFirstInsertionPt());
    Splat = B.CreateVectorSplat(WaveSize, V, V->getName() + ".splat");
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

PHINode *FunctionWidener::createWidenedPHIStub(PHINode &PN) {
  Type *WideTy = FixedVectorType::get(PN.getType(), WaveSize);
  PHINode *NewPN = PHINode::Create(WideTy, PN.getNumIncomingValues(),
                                   PN.getName() + ".wide");
  NewPN->insertBefore(PN.getIterator());
  Widened[&PN] = NewPN;
  ToErase.push_back(&PN);
  return NewPN;
}

void FunctionWidener::fillWidenedPHIIncoming(PHINode &PN, PHINode &NewPN) {
  // Filling every widened PHI's incoming values is deferred to its own pass
  // over the whole function (see `widen` below), run only after every
  // instruction (including one reached solely through a loop's backedge)
  // has its final widened form in `Widened`. A loop header's PHI has an
  // incoming value from its latch that is not widened yet when the header
  // is first reached in reverse post-order -- building its broadcast
  // eagerly here, as milestone 4's acyclic-only version of this function
  // did, would broadcast the *old*, soon-to-be-erased scalar value instead
  // of referencing the real widened one (roadmap milestone 7).
  for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
    IRBuilder<> IncomingBuilder(PN.getIncomingBlock(I)->getTerminator());
    NewPN.addIncoming(getWidened(PN.getIncomingValue(I), IncomingBuilder),
                      PN.getIncomingBlock(I));
  }
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
  Value *Replacement = Component == 0   ? Env.GroupIDX
                       : Component == 1 ? Env.GroupIDY
                                        : Env.GroupIDZ;
  CI.replaceAllUsesWith(Replacement);
  ToErase.push_back(&CI);
}

void FunctionWidener::widenResourceCall(CallInst &CI,
                                        const MatchedResourceCall &Matched,
                                        IRBuilder<> &Builder) {
  // A divergent governing mask (see "masked feme.cpu.resource.* call") needs
  // scalarization exactly as much as a divergent address/value operand does:
  // even if every lane that's still active would compute the same address
  // and value (as in a resource write inside a masked loop whose address
  // does not itself depend on the lane), a deactivated lane must still be
  // prevented from touching memory at all.
  bool AnyDivergent =
      Widened.count(Matched.DescriptorIndex) || Widened.count(Matched.Offset) ||
      (Matched.StoredValue && Widened.count(Matched.StoredValue)) ||
      Widened.count(Matched.Mask);
  if (!AnyDivergent)
    return; // Every operand is uniform: leave the scalar call as-is.

  // Scalarize: call the same scalar callee once per lane, feeding it that
  // lane's extracted operand values, ANDing the wave's entry mask with this
  // call's own governing mask (a real, possibly-divergent value once
  // `feme::cpu::LinearizePass` has masked a diamond arm or a loop iteration
  // -- the constant `true` `feme::cpu::ResourceLoweringPass` otherwise
  // leaves it as costs nothing to AND in) so an inactive lane never touches
  // memory (see "masked feme.cpu.resource.* call" in "Phase 4: Widening").
  Function *Callee = CI.getCalledFunction();
  Value *WideDescriptorIndex = getWidened(Matched.DescriptorIndex, Builder);
  Value *WideOffset = getWidened(Matched.Offset, Builder);
  Value *WideStoredValue =
      Matched.StoredValue ? getWidened(Matched.StoredValue, Builder) : nullptr;

  Value *LaneMaskBase = Env.EntryMask;
  if (!isa<Constant>(Matched.Mask)) {
    Value *WideCallMask = getWidened(Matched.Mask, Builder);
    LaneMaskBase =
        Builder.CreateAnd(Env.EntryMask, WideCallMask, "resource.mask");
  }

  Value *Result = nullptr;
  if (!Matched.StoredValue)
    Result = PoisonValue::get(FixedVectorType::get(CI.getType(), WaveSize));

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *LaneMask = Builder.CreateExtractElement(
        LaneMaskBase, Builder.getInt32(Lane), "lane.mask");
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

void FunctionWidener::widenMaskAny(CallInst &CI, IRBuilder<> &Builder) {
  // `feme.cpu.mask.any` is uniform (see `feme::cpu::WaveTTIImpl`), so the
  // generic `!UI.isDivergentAtDef` rule in `widenInstruction` would leave it
  // alone -- wrong here, since its operand is a divergent value about to be
  // replaced. Lower it in place to the real cross-lane reduction over the
  // widened mask ("Mask representation between phases" in
  // feme/docs/FeMeCPUDesign.md) and RAUW with the (uniform, scalar `i1`)
  // result directly, rather than recording it in `Widened`, since nothing
  // needs to broadcast a value that is already what every other use expects.
  Value *WideMask = getWidened(CI.getArgOperand(0), Builder);
  Value *Reduced = Builder.CreateOrReduce(WideMask);
  Reduced->takeName(&CI);
  CI.replaceAllUsesWith(Reduced);
  ToErase.push_back(&CI);
}

void FunctionWidener::widenScalarizedFallback(Instruction &I,
                                              IRBuilder<> &Builder) {
  // The generic, "always applicable" fallback ("Scalarization fallback" in
  // "Phase 4: Widening"): extract each operand's per-lane value, clone `I`
  // once per lane with those scalar operands substituted, and reassemble a
  // result vector from the per-lane results (if `I` produces one). This is
  // what makes widening total -- it never has to reject an unsupported
  // divergent opcode. Atomics are the main user (`AtomicRMWInst`,
  // `AtomicCmpXchgInst`); this milestone does not yet gate their per-lane
  // execution by an active-lane mask the way "masked feme.cpu.resource.*
  // call" already does, matching the existing, documented narrowing that
  // ordinary `load`/`store`/atomics under a divergent condition are not yet
  // rewritten into the masked intrinsic forms `feme::cpu::LinearizePass`
  // would otherwise have produced (see the Status section's milestone 6
  // deviation note in feme/docs/FeMeCPUDesign.md) -- a masked, scalarized
  // atomic is future work once that lands.
  SmallVector<Value *, 4> WideOps;
  for (Value *Op : I.operands())
    WideOps.push_back(getWidened(Op, Builder));

  bool HasResult = !I.getType()->isVoidTy();
  Value *Result =
      HasResult ? PoisonValue::get(FixedVectorType::get(I.getType(), WaveSize))
                : nullptr;

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Instruction *Clone = I.clone();
    for (unsigned OpIdx = 0, E = WideOps.size(); OpIdx != E; ++OpIdx)
      Clone->setOperand(OpIdx, Builder.CreateExtractElement(
                                   WideOps[OpIdx], Builder.getInt32(Lane),
                                   "lane.op"));
    Builder.Insert(Clone, I.getName() + ".lane");
    if (Result)
      Result =
          Builder.CreateInsertElement(Result, Clone, Builder.getInt32(Lane));
  }

  if (Result)
    Widened[&I] = Result;
  ToErase.push_back(&I);
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
  } else if (isa<CallBase>(&I)) {
    // A generic divergent call (e.g. a math libcall like `llvm.sin.f32`,
    // "Call to a math libcall" in "Phase 4: Widening") needs its own
    // handling -- the callee itself is one of `I.operands()`, which the
    // scalarization fallback below would otherwise try to broadcast/extract
    // like any other operand -- so it is not yet covered by either the
    // elementwise rule or the generic fallback.
    OldF.getContext().emitError(
        "feme-cpu-simdize: unsupported divergent call to '" +
        Twine(I.getOperand(I.getNumOperands() - 1)->getName()) +
        "' (roadmap milestone 7 does not cover a generic vector-call "
        "rewrite)");
    return;
  } else {
    widenScalarizedFallback(I, Builder);
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
    if (isMaskAnyCall(*CI)) {
      widenMaskAny(*CI, Builder);
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

Function *FunctionWidener::widen() {
  if (!checkSupportedControlFlow())
    return nullptr;

  NewF = buildWidenedFunction();

  ReversePostOrderTraversal<Function *> RPOT(NewF);

  // Pass 1: create every divergent PHI's widened stub, across the whole
  // function, before any other instruction is widened -- see
  // `fillWidenedPHIIncoming`'s comment for why a loop needs this to happen
  // strictly before pass 2, not interleaved with it the way milestone 4's
  // acyclic-only version of this function did.
  SmallVector<PHINode *, 8> DivergentPHIs;
  for (BasicBlock *BB : RPOT) {
    for (PHINode &PN : BB->phis()) {
      if (UI.isDivergentAtDef(&PN))
        DivergentPHIs.push_back(&PN);
    }
  }
  for (PHINode *PN : DivergentPHIs)
    createWidenedPHIStub(*PN);

  // Pass 2: widen every non-phi instruction. Reverse post-order is
  // sufficient here even for a loop body: only a `phi` can observe a value
  // defined later in this order (through a backedge), and every `phi` was
  // already given its final (empty) widened form in pass 1 above.
  for (BasicBlock *BB : RPOT) {
    for (Instruction &I : make_early_inc_range(*BB)) {
      if (isa<PHINode>(I))
        continue;
      IRBuilder<> Builder(&I);
      widenInstruction(I, Builder);
    }
  }

  // Pass 3: fill in every widened PHI's incoming values, now that every
  // instruction anywhere in the function (including one reachable only
  // through a backedge) has its final widened form.
  for (PHINode *PN : DivergentPHIs)
    fillWidenedPHIIncoming(*PN, *cast<PHINode>(Widened[PN]));

  // A loop header's old scalar `phi` and its own backedge value can each
  // hold a use of the other (the `phi`'s incoming-from-latch operand uses
  // the backedge value; that value's own defining instruction may in turn
  // use the `phi`) -- an honest cycle in the old, soon-to-be-fully-replaced
  // IR that no erasure order can resolve on its own. Every read of a
  // widened `phi`'s old incoming values happened above in pass 3, so it is
  // safe to sever them now: poison out each old `phi`'s operands before
  // erasing anything, which turns the rest of the old, dead subgraph back
  // into a normal acyclic one.
  for (PHINode *PN : DivergentPHIs)
    for (unsigned I = 0, E = PN->getNumIncomingValues(); I != E; ++I)
      PN->setIncomingValue(I, PoisonValue::get(PN->getIncomingValue(I)->getType()));

  // The remaining erasure order only needs "uses before defs" among what's
  // left, which `NewF`'s actual layout gives directly: a block always
  // precedes what it dominates, so walking the function once more and
  // erasing in reverse of that walk is safe.
  SmallPtrSet<Instruction *, 16> ToEraseSet(llvm::from_range, ToErase);
  SmallVector<Instruction *, 16> OrderedErase;
  for (BasicBlock &BB : *NewF)
    for (Instruction &I : BB)
      if (ToEraseSet.contains(&I))
        OrderedErase.push_back(&I);
  for (Instruction *I : llvm::reverse(OrderedErase))
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
    if (Widener.widen())
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
