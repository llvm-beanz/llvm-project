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
//      - A raised wave intrinsic (other than `wave.getlaneindex`, handled as
//        a per-lane-varying builtin above) becomes a canonical
//        `feme.cpu.wave.*` call (see feme::cpu::WaveCalls) over the
//        widened operand(s) and the wave's entry mask -- `WaveLoweringPass`
//        lowers the actual cross-lane reduction/scan/broadcast arithmetic,
//        not this pass, mirroring the resource-call/builtin split above.
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

#include "GroupShared.h"
#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/MaskIntrinsics.h"
#include "feme/Transforms/CPU/ResourceCalls.h"
#include "feme/Transforms/CPU/WaveCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/VectorUtils.h"
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

/// Which raised wave intrinsic \p ID canonicalizes to a `feme.cpu.wave.*`
/// call (see feme::cpu::WaveCalls); `std::nullopt` for anything else,
/// including `wave.getlaneindex` (a `BuiltinCallKind` instead -- see
/// `classifyBuiltin` above) and `QuadOp`'s `llvm.dx.quad.read.*` family
/// (raised, per roadmap step R4, but not yet lowered -- quad/derivative
/// support is an explicit v1 non-goal, see feme/docs/FeMeCPUDesign.md's
/// "Non-Goals").
std::optional<WaveCallKind> classifyWaveCall(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dx_wave_get_lane_count:
  case Intrinsic::spv_wave_get_lane_count:
    return WaveCallKind::GetLaneCount;
  case Intrinsic::dx_wave_is_first_lane:
  case Intrinsic::spv_wave_is_first_lane:
    return WaveCallKind::IsFirstLane;
  case Intrinsic::dx_wave_any:
  case Intrinsic::spv_wave_any:
    return WaveCallKind::Any;
  case Intrinsic::dx_wave_all:
  case Intrinsic::spv_wave_all:
    return WaveCallKind::All;
  case Intrinsic::dx_wave_all_equal:
  case Intrinsic::spv_wave_all_equal:
    return WaveCallKind::AllEqual;
  case Intrinsic::dx_wave_readlane:
  case Intrinsic::spv_wave_readlane:
    return WaveCallKind::ReadLane;
  case Intrinsic::dx_wave_active_countbits:
  case Intrinsic::spv_wave_active_countbits:
    return WaveCallKind::ActiveCountBits;
  case Intrinsic::dx_wave_prefix_bit_count:
    return WaveCallKind::PrefixBitCount;
  case Intrinsic::dx_wave_ballot:
    return WaveCallKind::Ballot;
  // Signed/unsigned addition and multiplication are bit-identical in two's
  // complement, so each signed/unsigned pair shares one `WaveCallKind` (see
  // `WaveCallKind::ActiveSum`'s comment).
  case Intrinsic::dx_wave_reduce_sum:
  case Intrinsic::dx_wave_reduce_usum:
  case Intrinsic::spv_wave_reduce_sum:
    return WaveCallKind::ActiveSum;
  case Intrinsic::dx_wave_product:
  case Intrinsic::dx_wave_uproduct:
  case Intrinsic::spv_wave_product:
    return WaveCallKind::ActiveProduct;
  case Intrinsic::dx_wave_reduce_max:
  case Intrinsic::spv_wave_reduce_max:
    return WaveCallKind::ActiveMax;
  case Intrinsic::dx_wave_reduce_umax:
  case Intrinsic::spv_wave_reduce_umax:
    return WaveCallKind::ActiveUMax;
  case Intrinsic::dx_wave_reduce_min:
  case Intrinsic::spv_wave_reduce_min:
    return WaveCallKind::ActiveMin;
  case Intrinsic::dx_wave_reduce_umin:
  case Intrinsic::spv_wave_reduce_umin:
    return WaveCallKind::ActiveUMin;
  case Intrinsic::dx_wave_reduce_and:
  case Intrinsic::spv_wave_reduce_and:
    return WaveCallKind::ActiveBitAnd;
  case Intrinsic::dx_wave_reduce_or:
  case Intrinsic::spv_wave_reduce_or:
    return WaveCallKind::ActiveBitOr;
  case Intrinsic::dx_wave_reduce_xor:
  case Intrinsic::spv_wave_reduce_xor:
    return WaveCallKind::ActiveBitXor;
  case Intrinsic::dx_wave_prefix_sum:
  case Intrinsic::dx_wave_prefix_usum:
  case Intrinsic::spv_wave_prefix_sum:
    return WaveCallKind::PrefixSum;
  case Intrinsic::dx_wave_prefix_product:
  case Intrinsic::dx_wave_prefix_uproduct:
  case Intrinsic::spv_wave_prefix_product:
    return WaveCallKind::PrefixProduct;
  default:
    return std::nullopt;
  }
}

bool isGroupIdCall(Intrinsic::ID ID) {
  return ID == Intrinsic::dx_group_id || ID == Intrinsic::spv_group_id;
}

/// Whether \p ID is trivially widenable to a vector-typed overload with the
/// same, single overloaded type shared by its return and every argument:
/// `llvm::isTriviallyVectorizable`'s target-independent intrinsics, plus the
/// handful of homogeneous, `LLVMMatchType`-shaped unary DXIL/SPIR-V math
/// intrinsics `feme::dxil::OpRaisingPass`'s `DirectOps` table raises that
/// utility does not itself know about (see `widenElementwise`).
bool isElementwiseVectorizableIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dx_frac:
  case Intrinsic::spv_frac:
  case Intrinsic::dx_rsqrt:
  case Intrinsic::spv_rsqrt:
  case Intrinsic::dx_saturate:
  case Intrinsic::spv_saturate:
    return true;
  default:
    return isTriviallyVectorizable(ID);
  }
}

/// Returns the identity element `Id` for \p Op such that `Op(old, Id) ==
/// old` for every `old` -- i.e. the value a masked-off lane's `atomicrmw`
/// should contribute so it becomes a no-op instead of a real, unmasked
/// modification (see `FunctionWidener::widenMaskedAtomicRMW`). Every
/// `llvm::AtomicRMWInst::BinOp` HLSL's `Interlocked*` builtins actually
/// lower to (`Add`/`Sub`/`And`/`Or`/`Xor`/`Max`/`Min`/`UMax`/`UMin`) has one
/// (`FAdd`/`FSub`/`FMax`/`FMin`/`FMaximum`/`FMinimum`/`USubCond`/`USubSat`
/// do too, for whatever future front end produces them); `std::nullopt` for
/// `Xchg` (handled separately -- see `widenMaskedAtomicRMW`) and the three
/// operations (`Nand`, `UIncWrap`, `UDecWrap`) whose result depends on
/// `old` in a way no single operand value can leave unchanged for every
/// `old`.
std::optional<Constant *> getAtomicRMWIdentity(AtomicRMWInst::BinOp Op,
                                               Type *Ty) {
  switch (Op) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::USubCond:
  case AtomicRMWInst::USubSat:
    return Constant::getNullValue(Ty);
  case AtomicRMWInst::And:
  case AtomicRMWInst::UMin:
    return Constant::getAllOnesValue(Ty);
  case AtomicRMWInst::Max:
    return ConstantInt::get(Ty,
                            APInt::getSignedMinValue(Ty->getIntegerBitWidth()));
  case AtomicRMWInst::Min:
    return ConstantInt::get(Ty,
                            APInt::getSignedMaxValue(Ty->getIntegerBitWidth()));
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
    return ConstantFP::get(Ty, 0.0);
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::FMaximum:
  case AtomicRMWInst::FMaximumNum:
    return ConstantFP::getInfinity(Ty, /*Negative=*/true);
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::FMinimum:
  case AtomicRMWInst::FMinimumNum:
    return ConstantFP::getInfinity(Ty, /*Negative=*/false);
  case AtomicRMWInst::Xchg:
  case AtomicRMWInst::Nand:
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap:
  case AtomicRMWInst::BAD_BINOP:
    return std::nullopt;
  }
  llvm_unreachable("unhandled AtomicRMWInst::BinOp");
}

/// Widens a single acyclic, uniform-control-flow function to \p WaveSize
/// lanes. See the file comment above for the algorithm.
class FunctionWidener {
  /// The function being widened, up until `buildWidenedFunction` splices its
  /// body into `NewF` and erases it; null from that point on, so that a use
  /// of the old, freed function after widening has started asserts instead
  /// of silently reading freed memory.
  Function *OldF;
  /// The module's context, cached because it outlives `OldF` and so stays
  /// usable for the diagnostics `widen*` helpers emit after
  /// `buildWidenedFunction` has erased it.
  LLVMContext &Ctx;
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
  /// A divergent, vector-typed `insertelement` chain (in the *old* function)
  /// -> its decomposed widened form: one `<W x elemT>` per vector lane (see
  /// `widenInsertElement`, and "Vectors become components, not nested
  /// vectors" in "Phase 4: Widening").
  DenseMap<Value *, SmallVector<Value *, 4>> WidenedVectorComponents;

  SmallVector<Instruction *, 16> ToErase;

  /// Set by any `widen*` helper that diagnoses an unsupported construct via
  /// `emitError` partway through Pass 2 of `widen()` below (unlike
  /// `checkSupportedControlFlow`/`checkVectorDecompositionSupported`, which
  /// run to completion *before* any widening starts and can simply return
  /// `false`). `LLVMContext::emitError` only reports a diagnostic -- it
  /// does not itself stop execution -- so `widen()` must check this flag
  /// after every instruction it widens and bail out immediately once it is
  /// set, before continuing to build widened uses of (or replace uses of)
  /// a value that was left without its usual `Widened`/`ToErase` entry.
  bool HadError = false;

public:
  FunctionWidener(Function &OldF, unsigned WaveSize, UniformityInfo &UI)
      : OldF(&OldF), Ctx(OldF.getContext()), WaveSize(WaveSize), UI(UI),
        NumThreads(getThreadGroupSize(OldF)) {}

  /// Returns the widened function, or nullptr if \p OldF has a divergent
  /// branch left unhandled by `feme::cpu::LinearizePass` (a diagnostic is
  /// emitted; \p OldF is left untouched).
  Function *widen();

private:
  bool checkSupportedControlFlow();
  bool checkVectorDecompositionSupported();
  Function *buildWidenedFunction();
  Value *getWidened(Value *V, IRBuilderBase &Builder);
  PHINode *createWidenedPHIStub(PHINode &PN);
  void fillWidenedPHIIncoming(PHINode &PN, PHINode &NewPN);
  void widenBuiltin(CallInst &CI, BuiltinCallKind Kind, IRBuilder<> &Builder);
  void widenWaveCall(CallInst &CI, WaveCallKind Kind, IRBuilder<> &Builder);
  void replaceGroupIdCall(CallInst &CI);
  void widenResourceCall(CallInst &CI, const MatchedResourceCall &Matched,
                         IRBuilder<> &Builder);
  void widenMaskAny(CallInst &CI, IRBuilder<> &Builder);
  void widenMaskedLoad(CallInst &CI, const MatchedMaskedMemOp &Matched,
                       IRBuilder<> &Builder);
  void widenMaskedStore(CallInst &CI, const MatchedMaskedMemOp &Matched,
                        IRBuilder<> &Builder);
  void widenMaskedAtomicRMW(CallInst &CI, const MatchedMaskedAtomicRMW &Matched,
                            IRBuilder<> &Builder);
  void widenInsertElement(InsertElementInst &IE, IRBuilder<> &Builder);
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
  for (BasicBlock &BB : *OldF) {
    auto *BI = dyn_cast<CondBrInst>(BB.getTerminator());
    if (BI && UI.isDivergentTerminator(BI)) {
      Ctx.emitError(
          "feme-cpu-simdize: function '" + OldF->getName() +
          "' has a divergent branch; the divergence transform "
          "(feme::cpu::LinearizePass) did not remove it, or produced a "
          "shape this pass cannot widen");
      return false;
    }
  }
  return true;
}

bool FunctionWidener::checkVectorDecompositionSupported() {
  // "Vectors become components, not nested vectors" in "Phase 4: Widening"
  // describes decomposing a divergent `<N x T>` (or aggregate) value into
  // `N` separate `<W x T>` components -- LLVM has no `<W x <N x T>>`. Full
  // decomposition (arbitrary `extractelement`/`shufflevector`/`phi`/`select`
  // of vector type, and aggregates of any kind) is not yet implemented; what
  // *is* supported, narrower than the design, is the one shape a
  // typed-buffer store actually produces (see `raiseTypedBufferStore` in
  // OpRaising.cpp): a chain of constant-index `insertelement`s assembling a
  // vector from scalar components, consumed only by another link of that
  // same chain or by a matched resource-store call's stored-value operand
  // (see `widenInsertElement`/`widenResourceCall` below). Verify every
  // divergent vector value matches that shape up front and bail with a
  // diagnostic, matching every other precondition this pass checks before
  // mutating anything, rather than let a later step build an invalid nested
  // vector type and assert.
  for (Instruction &I : instructions(*OldF)) {
    if (!UI.isDivergentAtDef(&I))
      continue;

    if (I.getType()->isAggregateType()) {
      Ctx.emitError("feme-cpu-simdize: function '" + OldF->getName() +
                    "' has a divergent value '" + I.getName() +
                    "' of aggregate type; component decomposition is not yet "
                    "supported (roadmap milestone 7 deviation)");
      return false;
    }
    if (!I.getType()->isVectorTy())
      continue;

    auto *IE = dyn_cast<InsertElementInst>(&I);
    if (!IE || !isa<ConstantInt>(IE->getOperand(2))) {
      Ctx.emitError(
          "feme-cpu-simdize: function '" + OldF->getName() +
          "' has a divergent value '" + I.getName() +
          "' of vector type; only a constant-index insertelement chain is "
          "supported (roadmap milestone 7 deviation)");
      return false;
    }
    for (User *U : IE->users()) {
      if (auto *UserIE = dyn_cast<InsertElementInst>(U))
        if (UserIE->getOperand(0) == IE)
          continue;
      if (auto *UserCI = dyn_cast<CallInst>(U)) {
        std::optional<MatchedResourceCall> Matched = matchResourceCall(*UserCI);
        if (Matched && Matched->StoredValue == IE)
          continue;
      }
      Ctx.emitError(
          "feme-cpu-simdize: function '" + OldF->getName() +
          "' has a divergent vector value '" + IE->getName() +
          "' used outside a supported insertelement-chain/resource-store "
          "pattern; component decomposition is not yet supported for this "
          "use (roadmap milestone 7 deviation)");
      return false;
    }
  }
  return true;
}

Function *FunctionWidener::buildWidenedFunction() {
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *MaskTy = FixedVectorType::get(Type::getInt1Ty(Ctx), WaveSize);

  SmallVector<Type *, 8> ParamTypes(OldF->getFunctionType()->params());
  ParamTypes.append({I32Ty, I32Ty, I32Ty, I32Ty, MaskTy, PtrTy});

  FunctionType *NewTy = FunctionType::get(OldF->getReturnType(), ParamTypes,
                                          OldF->getFunctionType()->isVarArg());
  Function *F =
      Function::Create(NewTy, OldF->getLinkage(), OldF->getAddressSpace(), "",
                       OldF->getParent());
  F->copyAttributesFrom(OldF);
  F->setComdat(OldF->getComdat());
  F->splice(F->begin(), OldF);

  for (auto [OldArg, NewArg] : llvm::zip(OldF->args(), F->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = F->arg_begin() + OldF->arg_size();
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

  F->takeName(OldF);
  OldF->replaceAllUsesWith(F);
  OldF->eraseFromParent();
  OldF = nullptr;
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

void FunctionWidener::widenWaveCall(CallInst &CI, WaveCallKind Kind,
                                    IRBuilder<> &Builder) {
  // Every wave op but `GetLaneCount` reduces over exactly the wave's
  // currently-active lanes (see "Phase 5: Wave and Builtin Lowering"), so
  // the wave's entry mask is the first canonical operand for every other
  // kind.
  Value *WideMask =
      Kind == WaveCallKind::GetLaneCount ? nullptr : Env.EntryMask;

  Value *WideOperand = nullptr;
  if (Kind != WaveCallKind::GetLaneCount && Kind != WaveCallKind::IsFirstLane)
    WideOperand = getWidened(CI.getArgOperand(0), Builder);

  Value *WideLaneIndex = nullptr;
  if (Kind == WaveCallKind::ReadLane)
    WideLaneIndex = getWidened(CI.getArgOperand(1), Builder);

  CallInst *NewCall = createWaveCall(Builder, Kind, WaveSize, WideMask,
                                     WideOperand, WideLaneIndex, CI.getName());

  // A divergent result (`IsFirstLane`/`PrefixBitCount`/`PrefixSum`/
  // `PrefixProduct`, see `isDivergentWaveCallResult`) is itself widened,
  // exactly like a builtin or resource-call result; a uniform one stands
  // in directly for the old scalar call the same way `widenMaskAny` RAUWs
  // its reduction.
  //
  // `ReadLane` is neither unconditionally: unlike the other rows in that
  // table, its actual divergence depends on its specific operands (its
  // lane index need not be uniform, see `WaveCallKind::ReadLane`'s
  // comment), so `feme::cpu::WaveTTIImpl` leaves it at the generic
  // operand-divergence rule rather than a fixed classification -- this
  // call's own `UI.isDivergentAtDef` result is what actually decides,
  // not a static per-`Kind` table entry. `createWaveCall` always builds a
  // genuinely wide `<W x T>` result for `ReadLane` (the lowering needs
  // that shape regardless), so the uniform case still needs one lane
  // extracted back to the scalar type `CI`'s existing (uniform) users
  // expect.
  bool ResultDivergent = Kind == WaveCallKind::ReadLane
                             ? UI.isDivergentAtDef(&CI)
                             : isDivergentWaveCallResult(Kind);
  if (ResultDivergent) {
    Widened[&CI] = NewCall;
  } else if (Kind == WaveCallKind::ReadLane) {
    Value *Scalar = Builder.CreateExtractElement(NewCall, uint64_t{0});
    Scalar->takeName(&CI);
    CI.replaceAllUsesWith(Scalar);
  } else {
    CI.replaceAllUsesWith(NewCall);
  }
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
  // A vector-typed stored value ("Vectors become components, not nested
  // vectors" in "Phase 4: Widening") is decomposed into one `<W x elemT>`
  // per lane component rather than a single `<W x T>` (see
  // `widenInsertElement`); it counts as divergent for this call exactly
  // when its components were, i.e. it was recorded in
  // `WidenedVectorComponents` (a uniform vector stays whole, and identical
  // for every lane, like any other uniform operand).
  bool StoredValueIsVector =
      Matched.StoredValue && Matched.StoredValue->getType()->isVectorTy();
  bool StoredValueDivergent =
      Matched.StoredValue &&
      (StoredValueIsVector ? WidenedVectorComponents.count(Matched.StoredValue)
                           : Widened.count(Matched.StoredValue));

  // A divergent governing mask (see "masked feme.cpu.resource.* call") needs
  // scalarization exactly as much as a divergent address/value operand does:
  // even if every lane that's still active would compute the same address
  // and value (as in a resource write inside a masked loop whose address
  // does not itself depend on the lane), a deactivated lane must still be
  // prevented from touching memory at all.
  bool AnyDivergent = Widened.count(Matched.DescriptorIndex) ||
                      Widened.count(Matched.Offset) || StoredValueDivergent ||
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

  Value *WideStoredValue = nullptr;
  SmallVector<Value *, 4> WideStoredComponents;
  if (Matched.StoredValue && StoredValueIsVector && StoredValueDivergent)
    WideStoredComponents = WidenedVectorComponents.lookup(Matched.StoredValue);
  else if (Matched.StoredValue && !StoredValueIsVector)
    WideStoredValue = getWidened(Matched.StoredValue, Builder);

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
    if (Matched.StoredValue) {
      if (StoredValueIsVector) {
        if (StoredValueDivergent) {
          Value *LaneVector = PoisonValue::get(Matched.StoredValue->getType());
          for (unsigned Component = 0,
                        NumComponents = WideStoredComponents.size();
               Component != NumComponents; ++Component) {
            Value *LaneScalar = Builder.CreateExtractElement(
                WideStoredComponents[Component], Builder.getInt32(Lane),
                "lane.value.elt");
            LaneVector = Builder.CreateInsertElement(
                LaneVector, LaneScalar, Builder.getInt32(Component));
          }
          CallArgs.push_back(LaneVector);
        } else {
          // Uniform vector: identical for every lane, so it needs no
          // per-lane extraction.
          CallArgs.push_back(Matched.StoredValue);
        }
      } else {
        CallArgs.push_back(Builder.CreateExtractElement(
            WideStoredValue, Builder.getInt32(Lane), "lane.value"));
      }
    }
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

void FunctionWidener::widenMaskedLoad(CallInst &CI,
                                      const MatchedMaskedMemOp &Matched,
                                      IRBuilder<> &Builder) {
  // Every masked load lowers to `llvm.masked.gather` over a `<W x ptr>`
  // vector of addresses -- correct whether that vector turns out to hold
  // the same pointer in every lane (`feme::cpu::LinearizePass`'s uniform
  // address case) or a genuinely different one per lane, so it is used
  // uniformly here rather than special-casing either. The "Mask
  // representation between phases" table's finer per-case lowerings (a
  // broadcast scalar load for a wave-invariant uniform address, a real
  // `llvm.masked.load` for a contiguous divergent address) are pure
  // performance work this milestone defers -- see the roadmap's "General
  // performance work" item -- `llvm.masked.gather` is correct, if not
  // optimal, for all of them.
  Value *WideMask = getWidened(Matched.Mask, Builder);
  Value *EffectiveMask =
      Builder.CreateAnd(Env.EntryMask, WideMask, "masked.mask");
  Value *WidePtr = getWidened(Matched.Ptr, Builder);
  Value *WidePassthru = getWidened(Matched.ValueOperand, Builder);

  Value *Result = Builder.CreateMaskedGather(
      FixedVectorType::get(CI.getType(), WaveSize), WidePtr,
      Align(Matched.Align ? Matched.Align : 1), EffectiveMask, WidePassthru,
      CI.getName());
  Widened[&CI] = Result;
  ToErase.push_back(&CI);
}

void FunctionWidener::widenMaskedStore(CallInst &CI,
                                       const MatchedMaskedMemOp &Matched,
                                       IRBuilder<> &Builder) {
  // See `widenMaskedLoad` above: `llvm.masked.scatter` over a `<W x ptr>`
  // vector of addresses is correct for a uniform or a divergent address
  // alike, at the cost of the same deferred performance work.
  Value *WideMask = getWidened(Matched.Mask, Builder);
  Value *EffectiveMask =
      Builder.CreateAnd(Env.EntryMask, WideMask, "masked.mask");
  Value *WidePtr = getWidened(Matched.Ptr, Builder);
  Value *WideVal = getWidened(Matched.ValueOperand, Builder);

  Builder.CreateMaskedScatter(WideVal, WidePtr,
                              Align(Matched.Align ? Matched.Align : 1),
                              EffectiveMask);
  ToErase.push_back(&CI);
}

void FunctionWidener::widenScalarizedFallback(Instruction &I,
                                              IRBuilder<> &Builder) {
  // The generic, "always applicable" fallback ("Scalarization fallback" in
  // "Phase 4: Widening"): extract each operand's per-lane value, clone `I`
  // once per lane with those scalar operands substituted, and reassemble a
  // result vector from the per-lane results (if `I` produces one). This is
  // what makes widening total -- it never has to reject an unsupported
  // divergent opcode. An `AtomicRMWInst` no longer reaches this fallback
  // when it needs masking (`feme::cpu::LinearizePass`'s `maskMemoryOps` now
  // rewrites one under a divergent mask into `feme.cpu.masked.atomicrmw`,
  // widened by `widenMaskedAtomicRMW` below instead); an `AtomicRMWInst`
  // with no divergent operand at all (so never masked, and not divergent
  // enough to be widened in the first place) and an `AtomicCmpXchgInst`
  // (whose `{T, i1}` result is an aggregate `feme::cpu::SIMDizePass`
  // already rejects before this would run) are this fallback's only
  // remaining atomic-instruction callers.
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
      Clone->setOperand(OpIdx,
                        Builder.CreateExtractElement(
                            WideOps[OpIdx], Builder.getInt32(Lane), "lane.op"));
    Builder.Insert(Clone, I.getName() + ".lane");
    if (Result)
      Result =
          Builder.CreateInsertElement(Result, Clone, Builder.getInt32(Lane));
  }

  if (Result)
    Widened[&I] = Result;
  ToErase.push_back(&I);
}

void FunctionWidener::widenMaskedAtomicRMW(
    CallInst &CI, const MatchedMaskedAtomicRMW &Matched, IRBuilder<> &Builder) {
  // Masks a scalarized `atomicrmw`'s per-lane execution (roadmap milestone
  // 7's "Scalarization fallback does not mask per-lane execution"
  // deviation, feme/docs/FeMeCPUDesign.md's Status section): rather than
  // real per-lane control flow -- which the widening driver in `widen()`
  // cannot support mid-block (it walks each block's original instruction
  // list once, so splitting a block during widening would strand whatever
  // followed the split point outside that walk) -- a masked-off lane's
  // `atomicrmw` still executes, but with its value operand replaced by
  // `Op`'s identity element (`getAtomicRMWIdentity`), making the memory
  // access a real but observably-inert no-op. `Xchg` has no such identity
  // (any value it writes is observable), so a masked-off lane instead
  // writes back the value already there: a plain (non-atomic) load of the
  // same address immediately beforehand is safe only because dispatch is
  // still sequential, one lane at a time (see the "Dispatch is sequential,
  // not thread-pooled" P1 narrowing in feme/docs/Roadmap.md's §1.6) -- a
  // genuinely concurrent lane could observe a torn or stale value between
  // that load and this lane's `atomicrmw`, the same caveat thread-pooling
  // will need to revisit this for. `Nand`/`UIncWrap`/`UDecWrap` have no
  // identity and no such substitute either (see `getAtomicRMWIdentity`'s
  // comment) -- HLSL's `Interlocked*` builtins never produce them, so this
  // is diagnosed rather than silently wrong.
  Value *WideMask = getWidened(Matched.Mask, Builder);
  Value *EffectiveMask =
      Builder.CreateAnd(Env.EntryMask, WideMask, "atomicrmw.mask");
  Value *WidePtr = getWidened(Matched.Ptr, Builder);
  Value *WideVal = getWidened(Matched.Val, Builder);

  Type *ValTy = Matched.Val->getType();
  std::optional<Constant *> Identity = getAtomicRMWIdentity(Matched.Op, ValTy);
  if (!Identity && Matched.Op != AtomicRMWInst::Xchg) {
    Ctx.emitError("feme-cpu-simdize: function '" + NewF->getName() +
                  "' has a divergent atomicrmw '" +
                  AtomicRMWInst::getOperationName(Matched.Op) +
                  "' with no maskable identity element (roadmap milestone 7 "
                  "deviation)");
    HadError = true;
    return;
  }

  Value *Result = PoisonValue::get(FixedVectorType::get(ValTy, WaveSize));
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *LaneMask = Builder.CreateExtractElement(
        EffectiveMask, Builder.getInt32(Lane), "lane.mask");
    Value *LanePtr = Builder.CreateExtractElement(
        WidePtr, Builder.getInt32(Lane), "lane.ptr");
    Value *LaneVal = Builder.CreateExtractElement(
        WideVal, Builder.getInt32(Lane), "lane.val");
    Value *IdentityVal = Identity
                             ? static_cast<Value *>(*Identity)
                             : Builder.CreateLoad(ValTy, LanePtr, "lane.old");
    Value *MaskedVal =
        Builder.CreateSelect(LaneMask, LaneVal, IdentityVal, "lane.rmw.val");
    Value *LaneResult =
        Builder.CreateAtomicRMW(Matched.Op, LanePtr, MaskedVal,
                                Align(Matched.Align ? Matched.Align : 1),
                                AtomicOrdering::SequentiallyConsistent);
    Result =
        Builder.CreateInsertElement(Result, LaneResult, Builder.getInt32(Lane));
  }

  Widened[&CI] = Result;
  ToErase.push_back(&CI);
}

void FunctionWidener::widenInsertElement(InsertElementInst &IE,
                                         IRBuilder<> &Builder) {
  // Decompose a divergent `insertelement` into its widened per-component
  // form (see `checkVectorDecompositionSupported`'s file comment): start
  // from the base's own components (an all-null placeholder for the
  // chain's first link, whose base is the `poison`/`undef`
  // `raiseTypedBufferStore` always starts from), fill in the inserted
  // element's widened value at its constant index, and record the result
  // for the next link (or `widenResourceCall`) to consume -- this
  // instruction itself never gets a single widened `<W x T>` replacement.
  auto *VecTy = cast<FixedVectorType>(IE.getType());
  SmallVector<Value *, 4> Components;
  if (auto It = WidenedVectorComponents.find(IE.getOperand(0));
      It != WidenedVectorComponents.end())
    Components = It->second;
  else
    Components.resize(VecTy->getNumElements(), nullptr);

  uint64_t Index = cast<ConstantInt>(IE.getOperand(2))->getZExtValue();
  Components[Index] = getWidened(IE.getOperand(1), Builder);

  WidenedVectorComponents[&IE] = std::move(Components);
  ToErase.push_back(&IE);
}

void FunctionWidener::widenElementwise(Instruction &I, IRBuilder<> &Builder) {
  if (auto *CI = dyn_cast<CallInst>(&I)) {
    // A divergent call to a "trivially vectorizable" LLVM intrinsic (see
    // `isElementwiseVectorizableIntrinsic` above) whose signature is a
    // single overloaded type shared by its return and every argument --
    // exactly the shape of a simple elementwise math libcall like
    // `llvm.sqrt.f32`, `llvm.log2.f32`, or `llvm.dx.frac.f32` ("Call to a
    // math libcall" in "Phase 4: Widening") -- widens directly to that
    // intrinsic's vector-typed overload, letting the host's own vectorized
    // math library/scalarizer handle it, rather than the generic
    // scalarization fallback below (whose per-lane clone would otherwise
    // try to broadcast/extract the callee itself, one of `I.operands()`).
    // Any other divergent call -- including a vectorizable intrinsic with a
    // non-overloaded operand, e.g. `llvm.powi`'s integer exponent -- remains
    // unsupported.
    Function *Callee = CI->getCalledFunction();
    Intrinsic::ID ID =
        Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
    bool Homogeneous = ID != Intrinsic::not_intrinsic &&
                       llvm::all_of(CI->args(), [&](const Value *Arg) {
                         return Arg->getType() == I.getType();
                       });
    if (ID != Intrinsic::not_intrinsic &&
        isElementwiseVectorizableIntrinsic(ID) && Homogeneous) {
      Type *WideTy = FixedVectorType::get(I.getType(), WaveSize);
      // `OldF` has already been spliced into `NewF` and erased from its
      // module by `buildWidenedFunction` by the time this runs, so its
      // parent module is null; look the declaration up in `NewF`'s module
      // instead.
      Function *WideCallee =
          Intrinsic::getOrInsertDeclaration(NewF->getParent(), ID, {WideTy});
      SmallVector<Value *, 4> WideArgs;
      for (Value *Arg : CI->args())
        WideArgs.push_back(getWidened(Arg, Builder));
      Value *NewCall =
          Builder.CreateCall(WideCallee, WideArgs, I.getName() + ".wide");
      Widened[&I] = NewCall;
      ToErase.push_back(&I);
      return;
    }
    Ctx.emitError("feme-cpu-simdize: unsupported divergent call to '" +
                  Twine(Callee ? Callee->getName() : "<indirect>") +
                  "' (roadmap milestone 7 does not cover a generic vector-call "
                  "rewrite)");
    HadError = true;
    return;
  }

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
    if (std::optional<MatchedMaskedMemOp> Matched = matchMaskedLoad(*CI)) {
      widenMaskedLoad(*CI, *Matched, Builder);
      return true;
    }
    if (std::optional<MatchedMaskedMemOp> Matched = matchMaskedStore(*CI)) {
      widenMaskedStore(*CI, *Matched, Builder);
      return true;
    }
    if (std::optional<MatchedMaskedAtomicRMW> Matched =
            matchMaskedAtomicRMW(*CI)) {
      widenMaskedAtomicRMW(*CI, *Matched, Builder);
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
    if (std::optional<WaveCallKind> Kind = classifyWaveCall(ID)) {
      widenWaveCall(*CI, *Kind, Builder);
      return true;
    }
  }

  // An `atomicrmw` always needs `widenElementwise`'s scalarization, even
  // when its own operands classify as uniform: unlike a pure computation or
  // an idempotent uniform `store` (every lane writing the identical value
  // to the identical address, so one execution and `W` give the same final
  // memory content), an atomic read-modify-write's effect accumulates --
  // running it once instead of once per active lane silently undercounts
  // (see the P0 "masked" fix in `widenMaskedAtomicRMW`/
  // `getAtomicRMWIdentity` above, and `feme/test/Tools/feme-run/HLSL/
  // histogram.hlsl`, the roadmap step R2 regression test this fixes: a
  // groupshared counter every lane increments unconditionally is uniform
  // by every operand's own value, but must still execute once per lane).
  // `AtomicCmpXchgInst` is not included here: its `{T, i1}` aggregate
  // result already has no widening support regardless of uniformity (see
  // `checkVectorDecompositionSupported`), so forcing it through the
  // generic vector-result fallback below would fail differently instead.
  if (isa<AtomicRMWInst>(I)) {
    widenElementwise(I, Builder);
    return true;
  }

  if (!UI.isDivergentAtDef(&I))
    return true; // Uniform: leave it exactly as it is.

  if (isa<CondBrInst>(I) || isa<UncondBrInst>(I) || isa<ReturnInst>(I))
    return true; // Handled/verified by checkSupportedControlFlow already.

  if (auto *IE = dyn_cast<InsertElementInst>(&I)) {
    widenInsertElement(*IE, Builder);
    return true;
  }

  widenElementwise(I, Builder);
  return true;
}

Function *FunctionWidener::widen() {
  if (!checkSupportedControlFlow())
    return nullptr;
  if (!checkVectorDecompositionSupported())
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
      // A `widen*` helper above may have diagnosed an unsupported
      // construct (see `HadError`'s comment) and returned without giving
      // `I` its usual `Widened`/`ToErase` entry. Bail out immediately
      // instead of letting pass 3 or the erasure loop below dereference
      // that missing entry.
      if (HadError)
        return nullptr;
    }
  }

  // Pass 3: fill in every widened PHI's incoming values, now that every
  // instruction anywhere in the function (including one reachable only
  // through a backedge) has its final widened form.
  for (PHINode *PN : DivergentPHIs)
    fillWidenedPHIIncoming(*PN, *cast<PHINode>(Widened[PN]));

  // Every instruction being erased may still be used by another
  // soon-to-be-erased instruction: a loop header's old scalar `phi` and its
  // own backedge value can each hold a use of the other (the `phi`'s
  // incoming-from-latch operand uses the backedge value; that value's own
  // defining instruction may in turn use the `phi`) -- an honest cycle in
  // the old, soon-to-be-fully-replaced IR that no erasure order alone can
  // resolve. More generally, nothing about `NewF`'s block layout guarantees
  // a "uses before defs" erasure order either: `feme::cpu::LinearizePass`'s
  // "Flow"-style merge blocks routinely land earlier in a function's block
  // list than a cycle-exit block whose value they still use (LLVM requires
  // a def to dominate its uses, not to precede them in a function's block
  // list). Sever every remaining use of a to-be-erased instruction's result
  // up front, across the whole set, before erasing anything -- every read
  // of an old value that widening still needed (a widened `phi`'s old
  // incoming values, in pass 3 above; a resource call's stored-value
  // operand; ...) has already happened by this point, so nothing is lost,
  // and it makes every remaining erasure order equally safe.
  for (Instruction *I : ToErase)
    if (!I->getType()->isVoidTy())
      I->replaceAllUsesWith(PoisonValue::get(I->getType()));
  for (Instruction *I : llvm::reverse(ToErase))
    I->eraseFromParent();

  // Canonicalize every groupshared (`addrspace(3)`) global's uses into a
  // `getelementptr` off `wave_groupshared`, now that widening has settled
  // -- see GroupShared.h's file comment for why this must run after the
  // walk above rather than before it, and roadmap milestone 9 for why it
  // lives here at all (Phase 6, `feme::cpu::EntryWrapperPass`, does the
  // actual allocation once every access has been canonicalized this way).
  GroupSharedLayout GSLayout = computeGroupSharedLayout(*NewF->getParent());
  if (!GSLayout.Offsets.empty() &&
      !rewriteGroupSharedGlobals(*NewF, Env.GroupShared, GSLayout))
    return nullptr;

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
