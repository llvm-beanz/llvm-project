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
#include "StageMaskCalls.h"
#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/StageOps.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/BuiltinCalls.h"
#include "feme/Transforms/CPU/ImageCalls.h"
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
    else if (Arg.getName() == "wave_sideeffect_mask")
      Env.SideEffectMask = &Arg, Found = true;
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
/// comment above). (V4) `llvm.spv.subgroup.local.invocation.id` -- the
/// `SPIRV_BuiltIn::SubgroupLocalInvocationId` a Vulkan shader's lane index
/// within its subgroup, per "Builtin and execution-shape mapping" in
/// feme/docs/FeMeVulkanDesign.md -- is the exact same per-lane value
/// `llvm.dx.wave.getlaneindex` already is, so it shares that
/// `BuiltinCallKind::LaneIndex` classification rather than needing its own.
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
  case Intrinsic::spv_subgroup_local_invocation_id:
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
/// "Non-Goals"). (V4) `llvm.spv.subgroup.size` -- Vulkan's
/// `SPIRV_BuiltIn::SubgroupSize` -- reports the same value
/// `llvm.{dx,spv}.wave.get.lane.count` already does (the pinned wave size,
/// per "Builtin and execution-shape mapping" in
/// feme/docs/FeMeVulkanDesign.md), so it shares `WaveCallKind::GetLaneCount`
/// rather than needing its own classification.
std::optional<WaveCallKind> classifyWaveCall(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dx_wave_get_lane_count:
  case Intrinsic::spv_wave_get_lane_count:
  case Intrinsic::spv_subgroup_size:
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

/// Whether \p Ty is a pointer into groupshared (`addrspace(3)`) memory --
/// the address space `feme::cpu::GroupSharedAddressSpace` names (see
/// GroupShared.h). A divergent access through one of these needs its own
/// widening rule (see `FunctionWidener::widenGroupSharedGEP`/`Load`/
/// `Store`/`AtomicRMW` below) rather than the generic elementwise/
/// scalarization rules: those build a broadcast `insertelement`/
/// `shufflevector` for any uniform *instruction* operand (a `Constant`
/// like a direct global reference folds away instead, `ConstantFolder`
/// having already done the equivalent job) that
/// `feme::cpu::rewriteGroupSharedGlobals` cannot see through when
/// canonicalizing the address space away afterwards -- the "divergent
/// index"/"access through a getelementptr" shapes roadmap milestone 9
/// narrowed (feme/docs/Roadmap.md's §1.6, closed by roadmap step R23).
bool isGroupSharedPointerType(Type *Ty) {
  auto *PtrTy = dyn_cast<PointerType>(Ty);
  return PtrTy && PtrTy->getAddressSpace() == GroupSharedAddressSpace;
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
  SmallVector<Value *, 4> getVectorComponents(Value *V, IRBuilderBase &Builder);
  PHINode *createWidenedPHIStub(PHINode &PN);
  void createWidenedVectorPHIStub(PHINode &PN);
  void fillWidenedPHIIncoming(PHINode &PN, PHINode &NewPN);
  void fillWidenedVectorPHIIncoming(PHINode &PN);
  void widenBuiltin(CallInst &CI, BuiltinCallKind Kind, IRBuilder<> &Builder);
  void widenWaveCall(CallInst &CI, WaveCallKind Kind, IRBuilder<> &Builder);
  void widenStageOp(CallInst &CI, feme::StageOpKind Kind, IRBuilder<> &Builder);
  void widenMaskedOutputStore(CallInst &CI, IRBuilder<> &Builder);
  void widenMaskedStreamEmit(CallInst &CI, IRBuilder<> &Builder);
  void widenMaskedStreamCut(CallInst &CI, IRBuilder<> &Builder);
  void widenMaskedTaskPayloadStore(CallInst &CI, IRBuilder<> &Builder);
  void widenReturnMasks(CallInst &CI, IRBuilder<> &Builder);
  void replaceGroupIdCall(CallInst &CI);
  void widenResourceCall(CallInst &CI, const MatchedResourceCall &Matched,
                         IRBuilder<> &Builder);
  void widenImageCall(CallInst &CI, const MatchedImageCall &Matched,
                      IRBuilder<> &Builder);
  void widenMaskAny(CallInst &CI, IRBuilder<> &Builder);
  void widenMaskedLoad(CallInst &CI, const MatchedMaskedMemOp &Matched,
                       IRBuilder<> &Builder);
  void widenMaskedStore(CallInst &CI, const MatchedMaskedMemOp &Matched,
                        IRBuilder<> &Builder);
  void widenMaskedAtomicRMW(CallInst &CI, const MatchedMaskedAtomicRMW &Matched,
                            IRBuilder<> &Builder);
  void widenGroupSharedGEP(GetElementPtrInst &GEP, IRBuilder<> &Builder);
  void widenGroupSharedLoad(LoadInst &LI, IRBuilder<> &Builder);
  void widenGroupSharedStore(StoreInst &SI, IRBuilder<> &Builder);
  void widenGroupSharedAtomicRMW(AtomicRMWInst &RMW, IRBuilder<> &Builder);
  void widenInsertElement(InsertElementInst &IE, IRBuilder<> &Builder);
  void widenExtractElement(ExtractElementInst &EE, IRBuilder<> &Builder);
  void widenShuffleVector(ShuffleVectorInst &SV, IRBuilder<> &Builder);
  void widenVectorSelect(SelectInst &SI, IRBuilder<> &Builder);
  void widenVectorElementwise(Instruction &I, IRBuilder<> &Builder);
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
  // `N` separate `<W x T>` components -- LLVM has no `<W x <N x T>>`.
  // Aggregates of any kind remain unimplemented (still diagnosed below), but
  // roadmap step C3 (feme/docs/Roadmap.md) closed the vector narrowing: the
  // producer shapes are now
  //
  //  - a chain of constant-index `insertelement`s assembling a vector from
  //    scalar components, the shape `raiseTypedBufferStore` in
  //    OpRaising.cpp produces (see `widenInsertElement`),
  //  - a vector-typed `feme.cpu.resource.*` load call (e.g. a typed-buffer
  //    load's `<N x T>` element), decomposed into `N` widened components
  //    directly as it is scalarized (see `widenResourceCall`'s per-lane
  //    loop), rather than one nested-vector `Widened` entry,
  //  - a `phi` of vector type, decomposed into `N` per-component `phi`s
  //    (see `createWidenedVectorPHIStub`/`fillWidenedVectorPHIIncoming`) --
  //    the shape `feme::cpu::LinearizePass`'s merge blocks give a value
  //    reconciled across a uniform diamond's arms,
  //  - a `select` of vector type with a scalar `i1` condition, decomposed
  //    into `N` per-component `select`s sharing that one widened condition
  //    (see `widenVectorSelect`) -- a `select` with a per-lane `<N x i1>`
  //    condition remains diagnosed, since none of the shapes that reach
  //    this pass need it, and
  //  - a `shufflevector` (its mask is always a compile-time constant in
  //    LLVM IR), decomposed at compile time into a selection among its two
  //    operands' own components with no runtime work at all (see
  //    `widenShuffleVector`) -- the common HLSL/GLSL swizzle shape, and
  //  - ordinary elementwise arithmetic/cast (`BinaryOperator`/
  //    `UnaryOperator`/`CastInst`) over a vector -- the "color = a + b"
  //    shape shader code is full of -- decomposed into `N` per-component
  //    scalar-element ops (see `widenVectorElementwise`), exactly the same
  //    rule `widenElementwise` already applies to a scalar-typed divergent
  //    value; a `CastInst` whose operand's element count would not line up
  //    component-for-component with the result (e.g. `bitcast <4 x i32> to
  //    <2 x i64>`) is excluded, unlike a `BinaryOperator`/`UnaryOperator`,
  //    whose operand and result element counts are always equal,
  //
  // each consumed only by another link of an insertelement chain, a matched
  // resource-store call's stored-value operand, an `extractelement` (a
  // constant index reads a component directly; a non-constant one chains
  // selects across every component instead, see `widenExtractElement`), a
  // vector-typed `select`'s true/false operand, a `shufflevector`'s vector
  // operand, a vector-typed `phi`'s incoming value, or another elementwise
  // arithmetic/cast operand. Verify every divergent vector value matches
  // one of those producer shapes, and every use of one matches one of the
  // consumer shapes, up front and bail with a diagnostic, matching every
  // other precondition this pass checks before mutating anything, rather
  // than let a later step build an invalid nested vector type and assert.
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

    // Both a constant-index and a non-constant-index `extractelement` are
    // supported *consumers* of a decomposed vector (validated from the
    // producer's side below, since every divergent vector-typed value in
    // this function is visited by this same loop); its own result is
    // scalar, so it does not fall through to the vector-producer checks
    // below.
    if (isa<ExtractElementInst>(&I))
      continue;

    if (!I.getType()->isVectorTy())
      continue;

    bool IsSupportedProducer = false;
    if (auto *IE = dyn_cast<InsertElementInst>(&I)) {
      IsSupportedProducer = isa<ConstantInt>(IE->getOperand(2));
    } else if (isa<PHINode>(&I)) {
      IsSupportedProducer = true;
    } else if (auto *SI = dyn_cast<SelectInst>(&I)) {
      IsSupportedProducer = SI->getCondition()->getType()->isIntegerTy(1);
    } else if (isa<ShuffleVectorInst>(&I)) {
      IsSupportedProducer = true;
    } else if (isa<BinaryOperator>(&I) || isa<UnaryOperator>(&I)) {
      // Ordinary elementwise arithmetic over a vector -- the common
      // "color = a + b" shape every shader is full of -- decomposes exactly
      // like a `phi`/`select`/`shufflevector`: one scalar-element op per
      // component instead of a single illegal `<W x <N x T>>` result (see
      // `widenVectorElementwise`). Every operand of a `BinaryOperator`/
      // `UnaryOperator` has the same element count as its result (an LLVM
      // IR requirement), so components always line up.
      IsSupportedProducer = true;
    } else if (auto *Cast = dyn_cast<CastInst>(&I)) {
      // A `CastInst`'s single operand need not share the result's element
      // count (`bitcast <4 x i32> to <2 x i64>`, unlike a `BinaryOperator`/
      // `UnaryOperator`): only accept the shapes `widenVectorElementwise`
      // can actually line up component-for-component -- a scalar operand
      // (impossible for a vector-typed cast result, kept for symmetry) or
      // one with the same element count (e.g. `sitofp <4 x i32> to
      // <4 x float>`, the common typed-load/store conversion).
      Value *Op = Cast->getOperand(0);
      IsSupportedProducer =
          !Op->getType()->isVectorTy() ||
          cast<FixedVectorType>(Op->getType())->getNumElements() ==
              cast<FixedVectorType>(I.getType())->getNumElements();
    } else if (auto *CI = dyn_cast<CallInst>(&I)) {
      std::optional<MatchedResourceCall> Matched = matchResourceCall(*CI);
      // A `feme.cpu.image.*` sample/load returns `<4 x float>` and is
      // decomposed into per-component wide vectors exactly like a typed
      // buffer load (see `widenImageCall`).
      IsSupportedProducer =
          (Matched && !Matched->StoredValue) || matchImageCall(*CI);
    }

    if (!IsSupportedProducer) {
      Ctx.emitError(
          "feme-cpu-simdize: function '" + OldF->getName() +
          "' has a divergent value '" + I.getName() +
          "' of vector type; only a constant-index insertelement chain, a "
          "phi, a scalar-condition select, a shufflevector, elementwise "
          "arithmetic/cast, or a resource/image load is supported (roadmap "
          "milestone 7 deviation)");
      return false;
    }

    for (User *U : I.users()) {
      if (auto *UserIE = dyn_cast<InsertElementInst>(U))
        if (UserIE->getOperand(0) == &I)
          continue;
      if (auto *UserCI = dyn_cast<CallInst>(U)) {
        std::optional<MatchedResourceCall> Matched = matchResourceCall(*UserCI);
        if (Matched && Matched->StoredValue == &I)
          continue;
      }
      if (isa<ExtractElementInst>(U))
        continue;
      if (auto *UserSel = dyn_cast<SelectInst>(U))
        if ((UserSel->getTrueValue() == &I || UserSel->getFalseValue() == &I) &&
            UserSel->getCondition()->getType()->isIntegerTy(1))
          continue;
      if (auto *UserShuffle = dyn_cast<ShuffleVectorInst>(U))
        if (UserShuffle->getOperand(0) == &I || UserShuffle->getOperand(1) == &I)
          continue;
      if (isa<PHINode>(U))
        continue;
      // A vector-typed elementwise arithmetic/cast user is itself visited
      // (and validated as a producer) by this same top-level loop, so
      // accept it here unconditionally rather than re-checking its operand
      // positions.
      if (U->getType()->isVectorTy() &&
          (isa<BinaryOperator>(U) || isa<UnaryOperator>(U) ||
           isa<CastInst>(U)))
        continue;
      Ctx.emitError(
          "feme-cpu-simdize: function '" + OldF->getName() +
          "' has a divergent vector value '" + I.getName() +
          "' used outside a supported insertelement-chain/resource-store/"
          "extractelement/select/shufflevector/phi/elementwise pattern; "
          "component decomposition is not yet supported for this use "
          "(roadmap milestone 7 deviation)");
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
  ParamTypes.append({I32Ty, I32Ty, I32Ty, I32Ty, MaskTy, MaskTy, PtrTy});

  FunctionType *NewTy = FunctionType::get(OldF->getReturnType(), ParamTypes,
                                          OldF->getFunctionType()->isVarArg());
  Function *F =
      Function::Create(NewTy, OldF->getLinkage(), OldF->getAddressSpace(), "",
                       OldF->getParent());
  F->copyAttributesFrom(OldF);
  F->setComdat(OldF->getComdat());
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  OldF->getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    F->setMetadata(Kind, Node);
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
  Env.SideEffectMask = &*ArgIt++;
  Env.SideEffectMask->setName("wave_sideeffect_mask");
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

SmallVector<Value *, 4>
FunctionWidener::getVectorComponents(Value *V, IRBuilderBase &Builder) {
  // The dual of `getWidened` for a vector-typed value: either read back an
  // already-decomposed divergent vector's components, or build the widened
  // form of each of a *uniform* vector's (constant or not) components
  // directly, one `getWidened` broadcast per lane of the vector itself --
  // exactly what `widenInsertElement`'s non-decomposed-base case used to do
  // inline before this helper was factored out to be shared by every other
  // producer of a decomposed vector (`phi`/`select`/`shufflevector`).
  if (auto It = WidenedVectorComponents.find(V);
      It != WidenedVectorComponents.end())
    return It->second;

  auto *VecTy = cast<FixedVectorType>(V->getType());
  if (isa<UndefValue>(V))
    return SmallVector<Value *, 4>(
        VecTy->getNumElements(),
        PoisonValue::get(
            FixedVectorType::get(VecTy->getElementType(), WaveSize)));

  SmallVector<Value *, 4> Components;
  for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I)
    Components.push_back(getWidened(
        Builder.CreateExtractElement(V, Builder.getInt32(I)), Builder));
  return Components;
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

void FunctionWidener::createWidenedVectorPHIStub(PHINode &PN) {
  // The vector analogue of `createWidenedPHIStub`: one `<W x elemT>` `phi`
  // stub per component, recorded in `WidenedVectorComponents` rather than a
  // single (illegal, nested-vector) `Widened` entry -- see
  // `checkVectorDecompositionSupported`'s file comment for why a divergent
  // vector `phi` is a supported producer shape.
  auto *VecTy = cast<FixedVectorType>(PN.getType());
  Type *WideElemTy = FixedVectorType::get(VecTy->getElementType(), WaveSize);
  SmallVector<Value *, 4> Components;
  for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I) {
    PHINode *NewPN = PHINode::Create(
        WideElemTy, PN.getNumIncomingValues(),
        PN.getName() + ".wide" + Twine(I));
    NewPN->insertBefore(PN.getIterator());
    Components.push_back(NewPN);
  }
  WidenedVectorComponents[&PN] = std::move(Components);
  ToErase.push_back(&PN);
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

void FunctionWidener::fillWidenedVectorPHIIncoming(PHINode &PN) {
  // The vector analogue of `fillWidenedPHIIncoming`, run in the same third
  // pass and for the same reason (a loop header's backedge value is not
  // widened yet during pass 1/2): fill each per-component stub `phi` from
  // the matching component of the incoming value's own widened form,
  // whether that incoming value is itself a decomposed divergent vector or
  // a uniform one `getVectorComponents` broadcasts on demand.
  SmallVector<Value *, 4> &Components = WidenedVectorComponents[&PN];
  for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
    IRBuilder<> IncomingBuilder(PN.getIncomingBlock(I)->getTerminator());
    SmallVector<Value *, 4> IncomingComponents =
        getVectorComponents(PN.getIncomingValue(I), IncomingBuilder);
    for (unsigned C = 0, CE = Components.size(); C != CE; ++C)
      cast<PHINode>(Components[C])
          ->addIncoming(IncomingComponents[C], PN.getIncomingBlock(I));
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

void FunctionWidener::widenStageOp(CallInst &CI, feme::StageOpKind Kind,
                                   IRBuilder<> &Builder) {
  assert(Kind != feme::StageOpKind::Discard &&
         Kind != feme::StageOpKind::Demote &&
         Kind != feme::StageOpKind::OutputStore &&
         Kind != feme::StageOpKind::NumStageOpKinds &&
         "unexpected stage op for widenStageOp");

  Module *M = NewF->getParent();
  SmallVector<Value *, 8> WideArgs;
  SmallVector<Type *, 8> WideArgTys;
  bool FirstOperandIsElementID =
      Kind == feme::StageOpKind::InputLoad ||
      Kind == feme::StageOpKind::InterpolateAtCentroid ||
      Kind == feme::StageOpKind::InterpolateAtSample ||
      Kind == feme::StageOpKind::InterpolateAtOffset;
  // `SubpassLoad`'s `attachment_index`/`component` operands (0 and 1) are
  // always compile-time constants (baked from the shader's own
  // `InputAttachmentIndex` decoration and the read's component selector),
  // exactly like `InputLoad`'s element ID above -- never a genuinely
  // divergent per-lane value -- so both stay scalar rather than being
  // widened into a vector `lowerFragmentSubpassLoad` (FragmentWrapper.cpp)
  // would then have to re-collapse. Its third operand, `sample` (roadmap
  // F8c), is an ordinary value-like operand -- `SubpassLoadPattern`
  // (SPIRVToLLVMPatterns.cpp) synthesizes a constant `0` for the common
  // implicit-sample case, but a real `OpImageRead` `Sample` image operand
  // can be a genuinely divergent per-lane value -- so it is widened like
  // any other operand rather than forced scalar.
  bool FirstTwoOperandsAreConstantIDs =
      Kind == feme::StageOpKind::SubpassLoad;
  for (unsigned I = 0, E = CI.arg_size(); I != E; ++I) {
    bool KeepScalar = (I == 0 && FirstOperandIsElementID) ||
                      (I <= 1 && FirstTwoOperandsAreConstantIDs);
    Value *Arg =
        KeepScalar ? CI.getArgOperand(I) : getWidened(CI.getArgOperand(I), Builder);
    WideArgs.push_back(Arg);
    WideArgTys.push_back(Arg->getType());
  }
  Type *WideTy = FixedVectorType::get(CI.getType(), WaveSize);
  FunctionCallee Callee = getOrInsertStageOp(*M, Kind, WideTy, WideArgTys);
  CallInst *WideCall = Builder.CreateCall(Callee, WideArgs, CI.getName());
  Widened[&CI] = WideCall;
  ToErase.push_back(&CI);
}

void FunctionWidener::widenMaskedOutputStore(CallInst &CI,
                                             IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *Element = CI.getArgOperand(0);
  Value *Row = getWidened(CI.getArgOperand(1), Builder);
  Value *Component = getWidened(CI.getArgOperand(2), Builder);
  Value *ValueArg = getWidened(CI.getArgOperand(3), Builder);
  Value *Vertex = getWidened(CI.getArgOperand(4), Builder);
  Value *Mask = Builder.CreateAnd(Env.SideEffectMask,
                                  getWidened(CI.getArgOperand(5), Builder),
                                  "stage.output.mask");
  FunctionCallee Callee = getOrInsertMaskedOutputStore(
      *M, ValueArg->getType(), Row->getType(), Component->getType(),
      Vertex->getType(), Mask->getType());
  Builder.CreateCall(Callee, {Element, Row, Component, ValueArg, Vertex, Mask});
  ToErase.push_back(&CI);
}

void FunctionWidener::widenMaskedStreamEmit(CallInst &CI,
                                            IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *Stream = getWidened(CI.getArgOperand(0), Builder);
  Value *Mask = Builder.CreateAnd(Env.SideEffectMask,
                                  getWidened(CI.getArgOperand(1), Builder),
                                  "stage.stream.emit.mask");
  FunctionCallee Callee =
      getOrInsertMaskedStreamEmit(*M, Stream->getType(), Mask->getType());
  Builder.CreateCall(Callee, {Stream, Mask});
  ToErase.push_back(&CI);
}

void FunctionWidener::widenMaskedStreamCut(CallInst &CI, IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *Stream = getWidened(CI.getArgOperand(0), Builder);
  Value *Mask = Builder.CreateAnd(Env.SideEffectMask,
                                  getWidened(CI.getArgOperand(1), Builder),
                                  "stage.stream.cut.mask");
  FunctionCallee Callee =
      getOrInsertMaskedStreamCut(*M, Stream->getType(), Mask->getType());
  Builder.CreateCall(Callee, {Stream, Mask});
  ToErase.push_back(&CI);
}

// (Roadmap H6c-a-b) `Offset` (operand 0) stays scalar -- it is the same
// compile-time constant byte offset for every lane of this call, unlike
// `widenMaskedOutputStore`'s per-lane `Row`/`Component`/`Vertex` -- only
// `Value` (operand 1) is widened, mirroring `widenMaskedOutputStore`'s own
// treatment of its `Element` operand.
void FunctionWidener::widenMaskedTaskPayloadStore(CallInst &CI,
                                                  IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *Offset = CI.getArgOperand(0);
  Value *ValueArg = getWidened(CI.getArgOperand(1), Builder);
  Value *Mask = Builder.CreateAnd(Env.SideEffectMask,
                                  getWidened(CI.getArgOperand(2), Builder),
                                  "task.payload.store.mask");
  FunctionCallee Callee =
      getOrInsertMaskedTaskPayloadStore(*M, ValueArg->getType(),
                                        Mask->getType());
  Builder.CreateCall(Callee, {Offset, ValueArg, Mask});
  ToErase.push_back(&CI);
}

void FunctionWidener::widenReturnMasks(CallInst &CI, IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *Live =
      Builder.CreateAnd(Env.EntryMask, getWidened(CI.getArgOperand(0), Builder),
                        "stage.return.live");
  Value *SideEffect = Builder.CreateAnd(
      Env.SideEffectMask, getWidened(CI.getArgOperand(1), Builder),
      "stage.return.sideeffect");
  FunctionCallee Callee =
      getOrInsertReturnMasks(*M, Live->getType(), SideEffect->getType());
  Builder.CreateCall(Callee, {Live, SideEffect});
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

  Value *BaseMask = Matched.StoredValue ? Env.SideEffectMask : Env.EntryMask;
  Value *LaneMaskBase = BaseMask;
  if (!isa<Constant>(Matched.Mask)) {
    Value *WideCallMask = getWidened(Matched.Mask, Builder);
    LaneMaskBase = Builder.CreateAnd(BaseMask, WideCallMask, "resource.mask");
  }

  Value *Result = nullptr;
  SmallVector<Value *, 4> LoadComponents;
  bool ResultIsVector = !Matched.StoredValue && CI.getType()->isVectorTy();
  if (!Matched.StoredValue) {
    if (ResultIsVector) {
      // "Vectors become components, not nested vectors": a vector-typed
      // load (e.g. a typed-buffer element) is decomposed into one `<W x
      // elemT>` per component as it is scalarized below, rather than one
      // illegal `<W x <N x elemT>>` (see `checkVectorDecompositionSupported`'s
      // file comment and `widenExtractElement`, which reads these back).
      auto *VecTy = cast<FixedVectorType>(CI.getType());
      LoadComponents.assign(VecTy->getNumElements(),
                            PoisonValue::get(FixedVectorType::get(
                                VecTy->getElementType(), WaveSize)));
    } else {
      Result = PoisonValue::get(FixedVectorType::get(CI.getType(), WaveSize));
    }
  }

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
    if (ResultIsVector) {
      for (unsigned Component = 0, NumComponents = LoadComponents.size();
           Component != NumComponents; ++Component) {
        Value *LaneScalar = Builder.CreateExtractElement(
            LaneResult, Builder.getInt32(Component), "lane.result.elt");
        LoadComponents[Component] = Builder.CreateInsertElement(
            LoadComponents[Component], LaneScalar, Builder.getInt32(Lane));
      }
    } else if (Result) {
      Result = Builder.CreateInsertElement(Result, LaneResult,
                                           Builder.getInt32(Lane));
    }
  }

  if (ResultIsVector)
    WidenedVectorComponents[&CI] = std::move(LoadComponents);
  else if (Result)
    Widened[&CI] = Result;
  ToErase.push_back(&CI);
}

void FunctionWidener::widenImageCall(CallInst &CI,
                                     const MatchedImageCall &Matched,
                                     IRBuilder<> &Builder) {
  // Roadmap R30's remaining SIMD gap. A `feme.cpu.image.*` call does not
  // fit `widenResourceCall`'s fixed (heap, index, offset, [value], mask)
  // shape -- it carries two heaps, two descriptor indices and several
  // coordinate operands (see ImageCalls.h's file comment) -- but its
  // scalarization is the same shape: call the scalar helper once per lane
  // with that lane's operands, then reassemble the result.
  //
  // Every operand except the trailing mask is widened generically, so a
  // divergent coordinate, LOD, comparison reference, or descriptor index
  // is handled without this function knowing which kind of call it is; the
  // leading heap pointer/count operands are entry-point parameters and
  // therefore never divergent.
  unsigned MaskIdx = CI.arg_size() - 1;
  bool AnyDivergent = Widened.count(Matched.Mask) != 0;
  for (unsigned I = 0; I != MaskIdx; ++I)
    AnyDivergent |= Widened.count(CI.getArgOperand(I)) != 0;
  if (!AnyDivergent)
    return; // A uniform sample: leave the scalar call as-is.

  SmallVector<Value *, 12> WideArgs(MaskIdx, nullptr);
  for (unsigned I = 0; I != MaskIdx; ++I)
    if (Widened.count(CI.getArgOperand(I)))
      WideArgs[I] = getWidened(CI.getArgOperand(I), Builder);

  // Every image operation is a read, so the wave's entry mask (not its
  // side-effect mask) is what decides which lanes may run the helper.
  Value *LaneMaskBase = Env.EntryMask;
  if (!isa<Constant>(Matched.Mask))
    LaneMaskBase = Builder.CreateAnd(
        LaneMaskBase, getWidened(Matched.Mask, Builder), "image.mask");

  Function *Callee = CI.getCalledFunction();
  Value *Result = nullptr;
  SmallVector<Value *, 4> LoadComponents;
  bool ResultIsVector = CI.getType()->isVectorTy();
  if (ResultIsVector) {
    auto *VecTy = cast<FixedVectorType>(CI.getType());
    LoadComponents.assign(VecTy->getNumElements(),
                          PoisonValue::get(FixedVectorType::get(
                              VecTy->getElementType(), WaveSize)));
  } else {
    Result = PoisonValue::get(FixedVectorType::get(CI.getType(), WaveSize));
  }

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    SmallVector<Value *, 12> CallArgs;
    for (unsigned I = 0; I != MaskIdx; ++I)
      CallArgs.push_back(WideArgs[I] ? Builder.CreateExtractElement(
                                           WideArgs[I], Builder.getInt32(Lane),
                                           "lane.image.arg")
                                     : CI.getArgOperand(I));
    CallArgs.push_back(Builder.CreateExtractElement(
        LaneMaskBase, Builder.getInt32(Lane), "lane.mask"));

    Value *LaneResult = Builder.CreateCall(Callee, CallArgs);
    if (!ResultIsVector) {
      Result = Builder.CreateInsertElement(Result, LaneResult,
                                           Builder.getInt32(Lane));
      continue;
    }
    for (unsigned Component = 0, NumComponents = LoadComponents.size();
         Component != NumComponents; ++Component) {
      Value *LaneScalar = Builder.CreateExtractElement(
          LaneResult, Builder.getInt32(Component), "lane.result.elt");
      LoadComponents[Component] = Builder.CreateInsertElement(
          LoadComponents[Component], LaneScalar, Builder.getInt32(Lane));
    }
  }

  if (ResultIsVector)
    WidenedVectorComponents[&CI] = std::move(LoadComponents);
  else
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
      Builder.CreateAnd(Env.SideEffectMask, WideMask, "masked.mask");
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
    // A void-typed `I` (e.g. a masked output store with no widened handler
    // of its own) clones to a void `Clone`: naming it would assert (`Value::
    // setNameImpl`'s "Cannot assign a name to void values!"), so only a
    // `HasResult` clone gets the ".lane" name.
    Builder.Insert(Clone, HasResult ? I.getName() + ".lane" : Twine());
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
      Builder.CreateAnd(Env.SideEffectMask, WideMask, "atomicrmw.mask");

  // A uniform groupshared address (the common case: an array element at a
  // compile-time-constant index) must reuse `Matched.Ptr` directly instead
  // of `getWidened`'s usual broadcast: unlike a direct, unindexed global
  // reference (a `Constant`, which `ConstantFolder` broadcasts-then-folds
  // right back to itself), a `getelementptr` off one is an `Instruction`,
  // so the broadcast survives as a real `insertelement`/`shufflevector`
  // `feme::cpu::rewriteGroupSharedGlobals` cannot see through when
  // canonicalizing the address space away afterwards -- the "access
  // through a getelementptr" shape roadmap milestone 9 narrowed
  // (feme/docs/Roadmap.md's §1.6, closed by roadmap step R23). A
  // genuinely divergent groupshared index still needs one real address
  // extracted per lane, from the real vector `getelementptr`
  // `widenGroupSharedGEP` builds for it.
  bool PtrUniform = !isa<Instruction>(Matched.Ptr) ||
                    !UI.isDivergentAtDef(cast<Instruction>(Matched.Ptr));
  bool ReuseScalarPtr =
      PtrUniform && isGroupSharedPointerType(Matched.Ptr->getType());
  Value *WidePtr = ReuseScalarPtr ? nullptr : getWidened(Matched.Ptr, Builder);
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
    Value *LanePtr = ReuseScalarPtr
                         ? Matched.Ptr
                         : Builder.CreateExtractElement(
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

void FunctionWidener::widenGroupSharedGEP(GetElementPtrInst &GEP,
                                          IRBuilder<> &Builder) {
  // A genuinely divergent groupshared index -- the common
  // `groupshared[threadIdInGroup]` pattern -- widens into a real
  // vector-of-pointers `getelementptr` instead of
  // `widenScalarizedFallback`'s per-lane clone-and-reassemble: LLVM allows
  // a scalar base with one or more vector index operands (implicitly
  // broadcasting the base to match), so every index that is itself
  // divergent is widened, and every uniform one (most commonly a leading
  // constant `0`) is left scalar. This gives
  // `feme::cpu::rewriteGroupSharedGlobals` one real divergent access to
  // retarget later, rather than `W` separate uniform accesses hidden
  // behind an `insertelement` chain it cannot see through -- the
  // "divergent index" shape roadmap milestone 9 narrowed
  // (feme/docs/Roadmap.md's §1.6, closed by roadmap step R23).
  SmallVector<Value *, 4> Indices;
  for (Value *Idx : GEP.indices())
    Indices.push_back(Widened.count(Idx) ? Widened[Idx] : Idx);

  Value *NewGEP =
      Builder.CreateGEP(GEP.getSourceElementType(), GEP.getPointerOperand(),
                        Indices, GEP.getName() + ".wide", GEP.isInBounds());
  Widened[&GEP] = NewGEP;
  ToErase.push_back(&GEP);
}

void FunctionWidener::widenGroupSharedLoad(LoadInst &LI, IRBuilder<> &Builder) {
  // A raw `load` from a divergent groupshared address -- one
  // `feme::cpu::LinearizePass` never masked into a `feme.cpu.masked.load`
  // call because it is not conditionally executed, only lane-varying in
  // its address -- still needs a real gather, exactly like an already-
  // masked one does (see `widenMaskedLoad` above); the only difference is
  // there is no extra governing mask to fold in besides the wave's own
  // entry mask. `LI`'s pointer operand is always already in `Widened`: a
  // `load`'s divergence tracks its pointer operand's exactly, and that
  // operand, being divergent, was necessarily widened earlier in reverse
  // post-order by `widenGroupSharedGEP` above.
  Value *WidePtr = Widened.lookup(LI.getPointerOperand());
  Value *Passthru =
      Constant::getNullValue(FixedVectorType::get(LI.getType(), WaveSize));

  Value *Result = Builder.CreateMaskedGather(
      FixedVectorType::get(LI.getType(), WaveSize), WidePtr, LI.getAlign(),
      Env.EntryMask, Passthru, LI.getName());
  Widened[&LI] = Result;
  ToErase.push_back(&LI);
}

void FunctionWidener::widenGroupSharedStore(StoreInst &SI,
                                            IRBuilder<> &Builder) {
  // See `widenGroupSharedLoad` above: a real scatter is correct for a raw,
  // divergent-address groupshared `store` for the same reason a real
  // gather is for a `load`.
  Value *WidePtr = Widened.lookup(SI.getPointerOperand());
  Value *WideVal = getWidened(SI.getValueOperand(), Builder);

  Builder.CreateMaskedScatter(WideVal, WidePtr, SI.getAlign(),
                              Env.SideEffectMask);
  ToErase.push_back(&SI);
}

void FunctionWidener::widenGroupSharedAtomicRMW(AtomicRMWInst &RMW,
                                                IRBuilder<> &Builder) {
  // An `atomicrmw` always executes once per lane regardless of its own
  // operands' uniformity (see the "always scalarize an atomicrmw" comment
  // in `widenInstruction` below) -- but cloning it through the generic
  // `widenScalarizedFallback` reaches its pointer operand through
  // `getWidened`'s usual broadcast-then-extract, which (unlike a direct,
  // unindexed global reference, a `Constant` `ConstantFolder` broadcasts
  // and folds straight back to itself) survives as a real
  // `insertelement`/`shufflevector` when the pointer is a `getelementptr`
  // instruction, even a uniform one -- exactly the "access through a
  // getelementptr" shape roadmap milestone 9 narrowed
  // (feme/docs/Roadmap.md's §1.6, closed by roadmap step R23; see
  // `feme/test/Transforms/CPU/simdize-groupshared-atomic-scalar.ll`'s
  // comment for the narrower, direct-global-only case this generalizes).
  // Reusing the pointer operand directly, once per lane, when it is
  // uniform sidesteps that broadcast entirely: every lane's clone then
  // shares the identical, untouched `getelementptr`/global operand, the
  // same way multiple ordinary `load`/`store` users of one already can. A
  // genuinely divergent index (widened into a real vector `getelementptr`
  // by `widenGroupSharedGEP` above) still needs one real address
  // extracted per lane.
  Value *Ptr = RMW.getPointerOperand();
  bool PtrDivergent = Widened.count(Ptr) != 0;
  Value *WidePtr = PtrDivergent ? Widened[Ptr] : nullptr;
  Value *WideVal = getWidened(RMW.getValOperand(), Builder);

  Value *Result =
      PoisonValue::get(FixedVectorType::get(RMW.getType(), WaveSize));
  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Value *LanePtr = PtrDivergent
                         ? Builder.CreateExtractElement(
                               WidePtr, Builder.getInt32(Lane), "lane.ptr")
                         : Ptr;
    Value *LaneVal = Builder.CreateExtractElement(
        WideVal, Builder.getInt32(Lane), "lane.val");
    Instruction *Clone = RMW.clone();
    Clone->setOperand(0, LanePtr);
    Clone->setOperand(1, LaneVal);
    Builder.Insert(Clone, RMW.getName() + ".lane");
    Result = Builder.CreateInsertElement(Result, Clone, Builder.getInt32(Lane));
  }

  Widened[&RMW] = Result;
  ToErase.push_back(&RMW);
}

void FunctionWidener::widenInsertElement(InsertElementInst &IE,
                                         IRBuilder<> &Builder) {
  // Decompose a divergent `insertelement` into its widened per-component
  // form (see `checkVectorDecompositionSupported`'s file comment): start
  // from the base's own components (`getVectorComponents` handles both a
  // decomposed divergent base and a uniform one, including `poison`/
  // `undef`), fill in the inserted element's widened value at its constant
  // index, and record the result for the next link (or a select/shuffle/
  // resource-store/`extractelement` consumer) -- this instruction itself
  // never gets a single widened `<W x T>` replacement.
  SmallVector<Value *, 4> Components =
      getVectorComponents(IE.getOperand(0), Builder);

  uint64_t Index = cast<ConstantInt>(IE.getOperand(2))->getZExtValue();
  Components[Index] = getWidened(IE.getOperand(1), Builder);

  WidenedVectorComponents[&IE] = std::move(Components);
  ToErase.push_back(&IE);
}

void FunctionWidener::widenExtractElement(ExtractElementInst &EE,
                                          IRBuilder<> &Builder) {
  // The dual of `widenInsertElement`: reads one already-decomposed `<W x
  // elemT>` component straight out of `getVectorComponents` rather than
  // extracting a per-lane scalar out of a single widened vector (there is
  // none -- see `checkVectorDecompositionSupported`'s file comment for why
  // a divergent vector is never given one).
  SmallVector<Value *, 4> Components =
      getVectorComponents(EE.getVectorOperand(), Builder);

  if (auto *ConstIdx = dyn_cast<ConstantInt>(EE.getIndexOperand())) {
    Widened[&EE] = Components[ConstIdx->getZExtValue()];
    ToErase.push_back(&EE);
    return;
  }

  // A non-constant index ("a shuffle or a dynamic index becomes selects
  // across the components", "Vectors become components, not nested
  // vectors" in "Phase 4: Widening"): there is no single `<W x elemT>`
  // vector a real per-lane-varying `extractelement` could read a component
  // out of, so chain a `select` per component instead, comparing the
  // widened index against that component's compile-time position.
  Value *WideIndex = getWidened(EE.getIndexOperand(), Builder);
  Value *Result = PoisonValue::get(Components[0]->getType());
  for (unsigned I = 0, E = Components.size(); I != E; ++I) {
    Value *Splat = ConstantVector::getSplat(
        ElementCount::getFixed(WaveSize),
        ConstantInt::get(EE.getIndexOperand()->getType(), I));
    Value *Match = Builder.CreateICmpEQ(WideIndex, Splat);
    Result = Builder.CreateSelect(Match, Components[I], Result,
                                  EE.getName() + ".wide");
  }
  Widened[&EE] = Result;
  ToErase.push_back(&EE);
}

void FunctionWidener::widenShuffleVector(ShuffleVectorInst &SV,
                                         IRBuilder<> &Builder) {
  // "A shuffle ... becomes selects across the components" ("Vectors become
  // components, not nested vectors"): a `shufflevector`'s mask is always a
  // compile-time constant in LLVM IR, so each output component is simply
  // one of the two operands' already-widened components, chosen at compile
  // time -- no runtime select needed, unlike a dynamic-index
  // `extractelement` (`widenExtractElement`).
  SmallVector<Value *, 4> LHS = getVectorComponents(SV.getOperand(0), Builder);
  SmallVector<Value *, 4> RHS = getVectorComponents(SV.getOperand(1), Builder);
  unsigned NumSrcElts =
      cast<FixedVectorType>(SV.getOperand(0)->getType())->getNumElements();
  Type *WideElemTy = FixedVectorType::get(
      cast<FixedVectorType>(SV.getType())->getElementType(), WaveSize);

  SmallVector<Value *, 4> Components;
  for (int Idx : SV.getShuffleMask()) {
    if (Idx < 0) {
      Components.push_back(PoisonValue::get(WideElemTy));
      continue;
    }
    Components.push_back(static_cast<unsigned>(Idx) < NumSrcElts
                              ? LHS[Idx]
                              : RHS[Idx - NumSrcElts]);
  }

  WidenedVectorComponents[&SV] = std::move(Components);
  ToErase.push_back(&SV);
}

void FunctionWidener::widenVectorSelect(SelectInst &SI, IRBuilder<> &Builder) {
  // A vector-typed `select` with a scalar `i1` condition (the shape
  // `checkVectorDecompositionSupported` accepts) decomposes into one
  // `select` per component, all sharing that single widened condition --
  // "Vectors become components, not nested vectors" applies to a `select`
  // exactly like a `phi`/`shufflevector`/`insertelement` chain.
  Value *WideCond = getWidened(SI.getCondition(), Builder);
  SmallVector<Value *, 4> TrueComponents =
      getVectorComponents(SI.getTrueValue(), Builder);
  SmallVector<Value *, 4> FalseComponents =
      getVectorComponents(SI.getFalseValue(), Builder);

  SmallVector<Value *, 4> Components;
  for (unsigned I = 0, E = TrueComponents.size(); I != E; ++I)
    Components.push_back(Builder.CreateSelect(
        WideCond, TrueComponents[I], FalseComponents[I],
        SI.getName() + ".wide" + Twine(I)));

  WidenedVectorComponents[&SI] = std::move(Components);
  ToErase.push_back(&SI);
}

void FunctionWidener::widenVectorElementwise(Instruction &I,
                                             IRBuilder<> &Builder) {
  // The vector analogue of `widenElementwise`'s generic `BinaryOperator`/
  // `UnaryOperator`/`CastInst` rule: apply the same scalar-element op once
  // per decomposed component instead of building a single, illegal
  // `<W x <N x T>>` result -- "Vectors become components, not nested
  // vectors" covers ordinary elementwise arithmetic on a vector exactly
  // like a `phi`/`select`/`shufflevector`. Every vector-typed operand of
  // one of these instructions has the same element count as the result
  // (an LLVM IR requirement), so all of a multi-operand op's operand
  // component lists line up component-for-component.
  SmallVector<SmallVector<Value *, 4>, 2> OperandComponents;
  for (Value *Op : I.operands())
    OperandComponents.push_back(
        Op->getType()->isVectorTy() ? getVectorComponents(Op, Builder)
                                     : SmallVector<Value *, 4>());

  Type *WideElemTy = FixedVectorType::get(
      cast<FixedVectorType>(I.getType())->getElementType(), WaveSize);
  unsigned NumComponents =
      cast<FixedVectorType>(I.getType())->getNumElements();

  SmallVector<Value *, 4> Components;
  for (unsigned C = 0; C != NumComponents; ++C) {
    auto ComponentOperand = [&](unsigned OpIdx) -> Value * {
      return OperandComponents[OpIdx].empty()
                 ? getWidened(I.getOperand(OpIdx), Builder)
                 : OperandComponents[OpIdx][C];
    };
    Value *NewV = nullptr;
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      NewV = Builder.CreateBinOp(BO->getOpcode(), ComponentOperand(0),
                                 ComponentOperand(1),
                                 I.getName() + ".wide" + Twine(C));
    } else if (auto *Cast = dyn_cast<CastInst>(&I)) {
      NewV = Builder.CreateCast(Cast->getOpcode(), ComponentOperand(0),
                                WideElemTy, I.getName() + ".wide" + Twine(C));
    } else {
      auto *UO = cast<UnaryOperator>(&I);
      NewV = Builder.CreateUnOp(UO->getOpcode(), ComponentOperand(0),
                               I.getName() + ".wide" + Twine(C));
    }
    Components.push_back(NewV);
  }

  WidenedVectorComponents[&I] = std::move(Components);
  ToErase.push_back(&I);
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
    if (std::optional<MatchedImageCall> Matched = matchImageCall(*CI)) {
      widenImageCall(*CI, *Matched, Builder);
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
    if (isMaskedOutputStoreCall(*CI)) {
      widenMaskedOutputStore(*CI, Builder);
      return true;
    }
    if (isMaskedStreamEmitCall(*CI)) {
      widenMaskedStreamEmit(*CI, Builder);
      return true;
    }
    if (isMaskedStreamCutCall(*CI)) {
      widenMaskedStreamCut(*CI, Builder);
      return true;
    }
    if (isMaskedTaskPayloadStoreCall(*CI)) {
      widenMaskedTaskPayloadStore(*CI, Builder);
      return true;
    }
    if (isReturnMasksCall(*CI)) {
      widenReturnMasks(*CI, Builder);
      return true;
    }
    feme::StageOpKind StageKind;
    if (isStageOpCall(*CI, &StageKind)) {
      switch (StageKind) {
      case feme::StageOpKind::InputLoad:
      case feme::StageOpKind::IsHelper:
      case feme::StageOpKind::DerivativeXFine:
      case feme::StageOpKind::DerivativeYFine:
      case feme::StageOpKind::DerivativeXCoarse:
      case feme::StageOpKind::DerivativeYCoarse:
      case feme::StageOpKind::QuadRead:
      case feme::StageOpKind::InterpolateAtCentroid:
      case feme::StageOpKind::InterpolateAtSample:
      case feme::StageOpKind::InterpolateAtOffset:
      case feme::StageOpKind::SubpassLoad:
        widenStageOp(*CI, StageKind, Builder);
        return true;
      case feme::StageOpKind::OutputStore:
      case feme::StageOpKind::Discard:
      case feme::StageOpKind::Demote:
      case feme::StageOpKind::StreamEmit:
      case feme::StageOpKind::StreamCut:
      case feme::StageOpKind::TaskPayloadStore:
      case feme::StageOpKind::NumStageOpKinds:
        break;
      }
    }
  }

  // An `atomicrmw` always needs scalarization, even when its own operands
  // classify as uniform: unlike a pure computation or an idempotent
  // uniform `store` (every lane writing the identical value to the
  // identical address, so one execution and `W` give the same final
  // memory content), an atomic read-modify-write's effect accumulates --
  // running it once instead of once per active lane silently undercounts
  // (see the P0 "masked" fix in `widenMaskedAtomicRMW`/
  // `getAtomicRMWIdentity` above, and `feme/test/Tools/feme-run/HLSL/
  // histogram.hlsl`, the roadmap step R2 regression test this fixes: a
  // groupshared counter every lane increments unconditionally is uniform
  // by every operand's own value, but must still execute once per lane).
  // A groupshared address gets its own scalarization
  // (`widenGroupSharedAtomicRMW`), which reuses a uniform address directly
  // per lane instead of `widenElementwise`'s generic broadcast-then-
  // extract (roadmap step R23; see that function's comment).
  // `AtomicCmpXchgInst` is not included here: its `{T, i1}` aggregate
  // result already has no widening support regardless of uniformity (see
  // `checkVectorDecompositionSupported`), so forcing it through the
  // generic vector-result fallback below would fail differently instead.
  if (auto *RMW = dyn_cast<AtomicRMWInst>(&I)) {
    if (isGroupSharedPointerType(RMW->getPointerOperand()->getType()))
      widenGroupSharedAtomicRMW(*RMW, Builder);
    else
      widenElementwise(I, Builder);
    return true;
  }

  if (!UI.isDivergentAtDef(&I))
    return true; // Uniform: leave it exactly as it is.

  if (isa<CondBrInst>(I) || isa<UncondBrInst>(I) || isa<ReturnInst>(I))
    return true; // Handled/verified by checkSupportedControlFlow already.

  // A divergent groupshared `getelementptr`/`load`/`store` gets its own
  // widening rules (`widenGroupSharedGEP`/`Load`/`Store`) rather than the
  // generic elementwise/scalarization ones below, so
  // `feme::cpu::rewriteGroupSharedGlobals` sees a real vector access (or a
  // real gather/scatter) to retarget afterwards instead of a broadcast it
  // cannot see through (roadmap step R23).
  if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
    if (isGroupSharedPointerType(GEP->getPointerOperandType())) {
      widenGroupSharedGEP(*GEP, Builder);
      return true;
    }
  }

  if (auto *LI = dyn_cast<LoadInst>(&I)) {
    if (LI->isSimple() &&
        isGroupSharedPointerType(LI->getPointerOperandType())) {
      widenGroupSharedLoad(*LI, Builder);
      return true;
    }
  }

  if (auto *SI = dyn_cast<StoreInst>(&I)) {
    if (SI->isSimple() &&
        isGroupSharedPointerType(SI->getPointerOperandType())) {
      widenGroupSharedStore(*SI, Builder);
      return true;
    }
  }

  if (auto *IE = dyn_cast<InsertElementInst>(&I)) {
    widenInsertElement(*IE, Builder);
    return true;
  }

  if (auto *EE = dyn_cast<ExtractElementInst>(&I)) {
    widenExtractElement(*EE, Builder);
    return true;
  }

  if (auto *SV = dyn_cast<ShuffleVectorInst>(&I)) {
    widenShuffleVector(*SV, Builder);
    return true;
  }

  if (auto *VSel = dyn_cast<SelectInst>(&I); VSel && I.getType()->isVectorTy()) {
    widenVectorSelect(*VSel, Builder);
    return true;
  }

  if (I.getType()->isVectorTy() &&
      (isa<BinaryOperator>(&I) || isa<UnaryOperator>(&I) ||
       isa<CastInst>(&I))) {
    widenVectorElementwise(I, Builder);
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
  for (PHINode *PN : DivergentPHIs) {
    if (PN->getType()->isVectorTy())
      createWidenedVectorPHIStub(*PN);
    else
      createWidenedPHIStub(*PN);
  }

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
  for (PHINode *PN : DivergentPHIs) {
    if (PN->getType()->isVectorTy())
      fillWidenedVectorPHIIncoming(*PN);
    else
      fillWidenedPHIIncoming(*PN, *cast<PHINode>(Widened[PN]));
  }

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
    if (!F.isDeclaration() && feme::isShaderEntryPoint(F))
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
