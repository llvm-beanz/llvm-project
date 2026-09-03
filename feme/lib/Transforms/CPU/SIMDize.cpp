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

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
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
#include "llvm/Support/Alignment.h"
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
    else if (Arg.getName() == "wave_group_count_x")
      Env.GroupCountX = &Arg, Found = true;
    else if (Arg.getName() == "wave_group_count_y")
      Env.GroupCountY = &Arg, Found = true;
    else if (Arg.getName() == "wave_group_count_z")
      Env.GroupCountZ = &Arg, Found = true;
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

/// `NumWorkgroups` (`llvm.spv.num.workgroups`, the dispatch's own grid
/// size -- `vkCmdDrawMeshTasksEXT`'s `groupCountX/Y/Z`, or `vkCmdDispatch`'s
/// own dimensions) -- roadmap H6o, found by the same full CTS re-run that
/// found H6n's `SubgroupId`/`NumSubgroups` gap. Uniform for this widened
/// function, exactly like `WorkgroupId`/`isGroupIdCall` above (the *same*
/// value for every group in the dispatch) -- but, unlike `NumSubgroups`
/// (H6n), it is a genuine *runtime* dispatch-time value, not a compile-time
/// constant derivable from `hlsl.numthreads`: a mesh dispatch's own group
/// count is supplied by the caller's `vkCmdDrawMeshTasksEXT` arguments (see
/// the CTS case name this gap was found in, `many_mesh_work_groups_x`,
/// which varies it directly), not fixed by the shader's own execution mode.
/// So, like `GroupIDX/Y/Z`, it must be threaded through the wave-body
/// interface as a genuine parameter (`Env.GroupCountX/Y/Z`, sourced from
/// `FemeDispatchArgs::GroupCount` by `feme::cpu::EntryWrapperPass`) rather
/// than folded away, and is handled inline exactly like `isGroupIdCall`/
/// `replaceGroupIdCall`, not deferred to a `BuiltinCallKind`/`WaveCallKind`
/// for the same reason `SubgroupId` was not.
bool isNumWorkgroupsCall(Intrinsic::ID ID) {
  return ID == Intrinsic::spv_num_workgroups;
}

/// `SubgroupId` (`llvm.spv.subgroup.id`, "which subgroup [wave] this
/// invocation belongs to within its workgroup") -- roadmap H6n. Uniform,
/// exactly like `WorkgroupId`/`isGroupIdCall` above, not per-lane-varying:
/// `feme/docs/FeMeCPUDesign.md`'s "wave loop" (`group = ceil(GroupSize / W)
/// waves`) already models a workgroup as a sequence of `W`-wide waves, one
/// `feme::cpu::SIMDizePass`-widened function call per wave, so "which
/// subgroup" *is* "which wave-loop iteration" -- exactly the `WaveIndex`
/// wave-body parameter every widened function already receives (see
/// `Env.WaveIndex`, threaded for `WorkgroupId`'s own sibling substitution
/// just above). Neither a `BuiltinCallKind` (reserved for a genuinely
/// per-lane-varying builtin `feme::cpu::WaveLoweringPass` still has to
/// decompose out of the group/wave-index parameters, see
/// `feme::cpu::BuiltinCallKind`'s own header comment) nor a `WaveCallKind`
/// (reserved for an actual wave-wide reduction/query over the mask of
/// currently-active lanes) fits this shape: it is a simple, direct,
/// uniform substitution, so it is handled inline exactly like
/// `isGroupIdCall`/`replaceGroupIdCall`, not deferred to either of those.
bool isSubgroupIdCall(Intrinsic::ID ID) {
  return ID == Intrinsic::spv_subgroup_id;
}

/// `NumSubgroups` (`llvm.spv.num.subgroups`, "how many subgroups [waves]
/// this workgroup dispatches as") -- roadmap H6n, the sibling gap
/// `isSubgroupIdCall` closes. Also uniform, but unlike `SubgroupId` it is
/// not merely a wave-body parameter already being threaded through: it is
/// a compile-time constant derivable the same way `feme/docs/
/// FeMeVulkanDesign.md`'s "Builtin and execution-shape mapping" table
/// already documents `WorkgroupSize` as one (`hlsl.numthreads`'s
/// `NumThreadsX/Y/Z`, known statically), matching `feme/docs/
/// FeMeCPUDesign.md`'s own `group = ceil(GroupSize / W) waves` formula --
/// so this call folds directly to a `ConstantInt`, computed once here from
/// this function's own `NumThreads`/`WaveSize` members, rather than
/// threading any new runtime value through the wave-body interface at
/// all.
bool isNumSubgroupsCall(Intrinsic::ID ID) {
  return ID == Intrinsic::spv_num_subgroups;
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

/// Roadmap H6g-b-a-i-a-i-b: whether \p ID is one of the `llvm.vector.reduce.*`
/// intrinsics `FunctionWidener::widenVectorReduce` can widen -- a real,
/// concrete GLSL/SPIR-V shape a component-wise vector comparison feeds,
/// e.g. glslang's `all(lessThanEqual(a, b))` lowering to
/// `llvm.vector.reduce.and.v4i1(fcmp ole <4 x float> %a, %b)`, confirmed by
/// a one-off diagnostic dump of the exact rejected producer/consumer pair
/// against a real failing `dEQP-VK.mesh_shader.ext.in_out.32_bits_only`
/// case. Each of these folds its vector operand's `N` components together
/// two at a time with the same scalar binary op/intrinsic, so a divergent,
/// per-lane-decomposed `<N x T>` operand widens into a single lane-wise
/// `<W x T>` result exactly the way any other divergent scalar-typed value
/// does -- no different from ordinary elementwise arithmetic, just applied
/// across components instead of across two whole operands. The
/// floating-point reductions (`fadd`/`fmul`/`fmax`/`fmin`, the latter two
/// also available as NaN-propagating `fmaximum`/`fminimum`) are excluded:
/// none of `fadd`/`fmul`'s extra scalar `start` operand is exercised by
/// any shape reaching this pass yet, and generalizing to it is left to a
/// future row if a real case needs it.
bool isSupportedVectorReduceIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_smax:
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_umax:
  case Intrinsic::vector_reduce_umin:
    return true;
  default:
    return false;
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

/// Whether every leaf reachable by recursing through \p Ty's own
/// `StructType`/`ArrayType` nesting is a genuine scalar (not itself a
/// vector) -- the shape `FunctionWidener`'s aggregate decomposition
/// (`WidenedAggregateComponents`, "Vectors become components, not nested
/// vectors" in "Phase 4: Widening", extended to aggregates by roadmap
/// milestone L21) supports. A vector-typed leaf (e.g. a struct field that
/// is itself a `<N x T>`, rather than already fully scalar-decomposed the
/// way `feme::mlir::CompositeConstructPattern`'s own struct-reassembly
/// always leaves one by the time it reaches this pass) remains out of
/// scope: no real case has needed it yet (every vector-typed field this
/// pass has actually seen inside a divergent aggregate is decomposed into
/// scalar `extractelement`/`insertvalue` chains before the aggregate is
/// ever built, confirmed by the real `packed.test` IR reduction L21's own
/// roadmap row cites), and mixing a per-lane vector-of-components
/// decomposition inside a per-lane flat-scalar-slot one would need its own
/// separate representation this row does not attempt.
bool isAllScalarAggregateLeaves(Type *Ty) {
  if (auto *StructTy = dyn_cast<StructType>(Ty))
    return llvm::all_of(StructTy->elements(), isAllScalarAggregateLeaves);
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty))
    return isAllScalarAggregateLeaves(ArrayTy->getElementType());
  return !Ty->isVectorTy();
}

/// The total number of scalar leaves \p Ty flattens to -- i.e. the number
/// of `<W x leafT>` slots `WidenedAggregateComponents` stores for a
/// divergent value of this (all-scalar-leaves, per
/// `isAllScalarAggregateLeaves`) type.
unsigned countAggregateLeafScalars(Type *Ty) {
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    unsigned Count = 0;
    for (Type *ElemTy : StructTy->elements())
      Count += countAggregateLeafScalars(ElemTy);
    return Count;
  }
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty))
    return ArrayTy->getNumElements() *
           countAggregateLeafScalars(ArrayTy->getElementType());
  return 1;
}

/// Recursively appends every leaf index path reachable from \p Ty (in
/// flattening order) to \p Paths, extending \p Prefix as it descends --
/// used by `FunctionWidener::getAggregateComponents` to build one real
/// `extractvalue` per leaf for a *uniform* aggregate value that has no
/// `WidenedAggregateComponents` entry of its own yet.
void collectAggregateLeafIndexPaths(
    Type *Ty, SmallVectorImpl<unsigned> &Prefix,
    SmallVectorImpl<SmallVector<unsigned, 4>> &Paths) {
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    for (unsigned I = 0, E = StructTy->getNumElements(); I != E; ++I) {
      Prefix.push_back(I);
      collectAggregateLeafIndexPaths(StructTy->getElementType(I), Prefix,
                                     Paths);
      Prefix.pop_back();
    }
    return;
  }
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty)) {
    for (unsigned I = 0, E = ArrayTy->getNumElements(); I != E; ++I) {
      Prefix.push_back(I);
      collectAggregateLeafIndexPaths(ArrayTy->getElementType(), Prefix, Paths);
      Prefix.pop_back();
    }
    return;
  }
  Paths.emplace_back(Prefix.begin(), Prefix.end());
}

/// Returns the `[Offset, Offset + Count)` flat-slot range (into a
/// `WidenedAggregateComponents` entry for a value of type \p Ty) that an
/// `insertvalue`/`extractvalue` index path \p Indices selects -- mirroring
/// those instructions' own index semantics (each index selects one struct
/// field or array element in turn; an index path never descends into a
/// `FixedVectorType`, since reaching inside one needs `extractelement`/
/// `insertelement` instead). \p Count is `1` when \p Indices names a
/// genuine scalar leaf, and greater than `1` when it names a nested
/// sub-aggregate instead (e.g. one whole array field of a larger struct).
std::pair<unsigned, unsigned> getAggregateLeafRange(Type *Ty,
                                                    ArrayRef<unsigned> Indices) {
  unsigned Offset = 0;
  Type *Cur = Ty;
  for (unsigned Idx : Indices) {
    if (auto *StructTy = dyn_cast<StructType>(Cur)) {
      for (unsigned I = 0; I != Idx; ++I)
        Offset += countAggregateLeafScalars(StructTy->getElementType(I));
      Cur = StructTy->getElementType(Idx);
    } else {
      auto *ArrayTy = cast<ArrayType>(Cur);
      Offset += Idx * countAggregateLeafScalars(ArrayTy->getElementType());
      Cur = ArrayTy->getElementType();
    }
  }
  return {Offset, countAggregateLeafScalars(Cur)};
}

/// Recursively appends every scalar leaf type reachable from \p Ty (in
/// flattening order) to \p Leaves -- used by
/// `FunctionWidener::getAggregateComponents` to build one `<W x leafT>`
/// poison placeholder per leaf for an `undef`/`poison` aggregate value.
void flattenAggregateLeafScalarTypes(Type *Ty,
                                     SmallVectorImpl<Type *> &Leaves) {
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    for (Type *ElemTy : StructTy->elements())
      flattenAggregateLeafScalarTypes(ElemTy, Leaves);
    return;
  }
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty)) {
    for (unsigned I = 0, E = ArrayTy->getNumElements(); I != E; ++I)
      flattenAggregateLeafScalarTypes(ArrayTy->getElementType(), Leaves);
    return;
  }
  Leaves.push_back(Ty);
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
  /// The aggregate analogue of `WidenedVectorComponents` (roadmap milestone
  /// L21): a divergent, all-scalar-leaves (`isAllScalarAggregateLeaves`)
  /// struct-or-array value (in the *old* function) -> its decomposed
  /// widened form, one `<W x leafT>` per *flattened* scalar leaf, in
  /// flattening order (`countAggregateLeafScalars`/`getAggregateLeafRange`)
  /// -- see `widenInsertValue`/`widenExtractValue`.
  DenseMap<Value *, SmallVector<Value *, 8>> WidenedAggregateComponents;

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
  bool checkAggregateValueSupported(Instruction &I);
  Function *buildWidenedFunction();
  Value *getWidened(Value *V, IRBuilderBase &Builder);
  SmallVector<Value *, 4> getVectorComponents(Value *V, IRBuilderBase &Builder);
  SmallVector<Value *, 8> getAggregateComponents(Value *V,
                                                 IRBuilderBase &Builder);
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
  void widenMaskedSetMeshOutputs(CallInst &CI, IRBuilder<> &Builder);
  void widenMaskedEmitMeshTasks(CallInst &CI, IRBuilder<> &Builder);
  void widenReturnMasks(CallInst &CI, IRBuilder<> &Builder);
  void replaceGroupIdCall(CallInst &CI);
  void replaceNumWorkgroupsCall(CallInst &CI);
  void replaceSubgroupIdCall(CallInst &CI);
  void replaceNumSubgroupsCall(CallInst &CI);
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
  void widenInsertValue(InsertValueInst &IV, IRBuilder<> &Builder);
  void widenExtractValue(ExtractValueInst &EV, IRBuilder<> &Builder);
  void widenShuffleVector(ShuffleVectorInst &SV, IRBuilder<> &Builder);
  void widenVectorSelect(SelectInst &SI, IRBuilder<> &Builder);
  void widenVectorElementwise(Instruction &I, IRBuilder<> &Builder);
  void widenVectorReduce(CallInst &CI, IRBuilder<> &Builder);
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
  //  - a `select` of vector type, decomposed into `N` per-component
  //    `select`s (see `widenVectorSelect`) -- a scalar `i1` condition is
  //    shared unchanged by every component, while a per-lane `<N x i1>`
  //    condition (roadmap H6g-b-a-i-a-i-b) is itself decomposed into `N`
  //    widened components first, one per `select`, exactly like any other
  //    divergent vector operand,
  //  - a vector comparison (`fcmp`/`icmp`), producing a `<N x i1>` result
  //    (roadmap H6g-b-a-i-a-i-b), decomposed into `N` per-component
  //    `fcmp`/`icmp`s exactly like ordinary elementwise arithmetic (see
  //    `widenVectorElementwise`) -- the common component-wise
  //    `lessThanEqual`/`greaterThan`-style GLSL comparison feeding a
  //    per-lane `select` (`mix`/`select` intrinsics with a `bvec`
  //    condition), and
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
  // vector-typed `select`'s condition, true, or false operand, a
  // `shufflevector`'s vector operand, a vector-typed `phi`'s incoming
  // value, another elementwise arithmetic/cast operand, an `fcmp`/`icmp`
  // operand (roadmap H6g-b-a-i-a-i-b: its own `<N x i1>` result is, in
  // turn, itself just another divergent vector value, most commonly
  // consumed by a `select`'s now-supported per-lane vector condition, or
  // by a `llvm.vector.reduce.and`/`or` call folding it into a single
  // per-lane boolean -- see below), a `llvm.vector.reduce.*` call's vector
  // operand (roadmap H6g-b-a-i-a-i-b: see
  // `isSupportedVectorReduceIntrinsic`/`widenVectorReduce` -- the shape a
  // component-wise vector comparison feeding glslang's `all`/`any`
  // GLSL builtins actually takes, confirmed by reducing a real failing
  // `dEQP-VK.mesh_shader.ext.in_out.32_bits_only` case down to its exact
  // IR shape), or (roadmap H6g-b-a-i-a-i-a) a matched
  // `feme.cpu.masked.store.*` call's stored-value operand -- the same
  // per-lane reassembly a matched resource-store call's stored-value
  // operand already gets (see `widenMaskedStore`), needed for a write with
  // no canonicalized `feme.stage.*`/`feme.cpu.resource.*` op of its own to
  // become instead, e.g. a mesh entry point's own
  // `gl_PrimitiveTriangleIndicesEXT[...] = uvec3(...)` (see
  //    MeshOutputWrapper.h's file comment), and
  //  - (roadmap H7o) an ordinary, non-groupshared `load` of vector type
  //    through a divergent address -- the common "local constant lookup
  //    table indexed by a per-invocation builtin" shape, e.g.
  //    `positions[gl_VertexIndex]` -- decomposed into `N` widened
  //    components by `widenScalarizedFallback`'s per-lane clone-and-
  //    reassemble, exactly like any divergent-address load whose result is
  //    already scalar, and
  //  - (roadmap L11) a groupshared `load` of vector type through a
  //    divergent address -- e.g. reading a whole `float4` row out of a
  //    `groupshared` matrix at a per-lane row index -- decomposed into `N`
  //    widened components by `widenGroupSharedLoad`'s own per-component
  //    gather (see that function's comment) rather than
  //    `widenScalarizedFallback`'s per-lane clone, since a groupshared
  //    address is already a real vector-of-pointers `getelementptr`
  //    (`widenGroupSharedGEP`) rather than a value needing per-lane
  //    extraction, and
  //  - (roadmap L15) a `feme.cpu.masked.load.*` call producing a
  //    vector-typed result -- `feme::cpu::LinearizePass`'s masked form of
  //    the L11 shape above, produced whenever that same groupshared
  //    vector `load` is itself inside genuinely divergent control flow
  //    (e.g. a real `if (ThreadID.x == 0)` guard, as
  //    `WaveOps/GroupSharedMatrixRowComponentDataRace.test` itself has) --
  //    decomposed into `N` per-component widened gathers exactly like the
  //    unmasked groupshared case, but against this call's own governing
  //    mask and its own (possibly divergent) passthru operand rather than
  //    the wave's bare entry mask and a constant zero (see
  //    `widenMaskedLoad`'s own vector case),
  //
  // A divergent *aggregate* (struct/array) value gets its own analogous
  // decomposition (roadmap milestone L21, extending roadmap step C3's
  // vector-only narrowing): a chain of `insertvalue`s assembling a struct
  // or array from scalar components (or from an already-decomposed
  // sub-aggregate/vector value inserted whole), and an `extractvalue`
  // reading either a genuine scalar leaf or a nested sub-aggregate back out
  // -- the shape `feme::cpu::SPIRVResourceLoweringPass`'s own whole-
  // aggregate resource load/store decomposition (roadmap L20) produces once
  // reassembled through `feme::cpu::LinearizePass` (confirmed by reducing a
  // real `Feature/StructuredBuffer/packed.test` failure, its own `Doggo
  // Fido = Buf[GI]; ...; Buf[GI] = Fido;` whole-struct-copy idiom, down to
  // its exact IR shape) -- see `checkAggregateValueSupported`,
  // `widenInsertValue`/`widenExtractValue`. Every leaf this decomposition
  // reaches must itself be a genuine scalar, not a vector
  // (`isAllScalarAggregateLeaves`'s own comment); an aggregate-typed `phi`
  // remains unsupported (no real case has needed one yet: unlike a vector
  // value reconciled across a uniform control-flow diamond,
  // `feme::cpu::LinearizePass` fully scalarizes every field of a divergent
  // aggregate reassignment, e.g. `packed.test`'s own `TailState` field,
  // into a plain scalar `select` before ever rebuilding the struct itself,
  // so the struct/array value is always freshly built via `insertvalue` in
  // the merge block, never merged via a struct-typed `phi` directly).
  //
  // Verify every divergent vector or aggregate value matches one of these
  // producer shapes, and every use of one matches one of the consumer
  // shapes, up front and bail with a diagnostic, matching every other
  // precondition this pass checks before mutating anything, rather than let
  // a later step build an invalid nested vector type and assert.
  for (Instruction &I : instructions(*OldF)) {
    if (!UI.isDivergentAtDef(&I))
      continue;

    if (I.getType()->isAggregateType()) {
      if (!checkAggregateValueSupported(I))
        return false;
      continue;
    }

    // Both a constant-index and a non-constant-index `extractelement` are
    // supported *consumers* of a decomposed vector (validated from the
    // producer's side below, since every divergent vector-typed value in
    // this function is visited by this same loop); its own result is
    // scalar, so it does not fall through to the vector-producer checks
    // below. Likewise for a scalar-result `extractvalue` (its own
    // aggregate operand's validity is checked when that operand is itself
    // visited by this same loop, in `checkAggregateValueSupported`); an
    // aggregate-*result* `extractvalue` (a nested sub-aggregate extraction)
    // is instead caught by the `isAggregateType()` branch just above.
    if (isa<ExtractElementInst>(&I) || isa<ExtractValueInst>(&I))
      continue;

    if (!I.getType()->isVectorTy())
      continue;

    bool IsSupportedProducer = false;
    if (auto *IE = dyn_cast<InsertElementInst>(&I)) {
      IsSupportedProducer = isa<ConstantInt>(IE->getOperand(2));
    } else if (isa<PHINode>(&I)) {
      IsSupportedProducer = true;
    } else if (isa<SelectInst>(&I)) {
      // A scalar `i1` condition is shared unchanged by every per-component
      // `select`; a per-lane `<N x i1>` condition (roadmap
      // H6g-b-a-i-a-i-b) is itself just another divergent vector operand,
      // decomposed the same way any other one is (see `widenVectorSelect`),
      // so both shapes are supported unconditionally here.
      IsSupportedProducer = true;
    } else if (isa<ShuffleVectorInst>(&I)) {
      IsSupportedProducer = true;
    } else if (isa<CmpInst>(&I)) {
      // A vector `fcmp`/`icmp` (roadmap H6g-b-a-i-a-i-b) decomposes exactly
      // like ordinary elementwise arithmetic: one scalar-element comparison
      // per component, its `<N x i1>` result split into `N` `<W x i1>`
      // components instead of one illegal `<W x <N x i1>>` (see
      // `widenVectorElementwise`). Both of a `CmpInst`'s operands always
      // share the result's element count (an LLVM IR requirement), so
      // components always line up, exactly like a `BinaryOperator`.
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
      if ((Matched && !Matched->StoredValue) || matchImageCall(*CI)) {
        IsSupportedProducer = true;
      } else if (matchMaskedLoad(*CI)) {
        // (Roadmap L15) A `feme.cpu.masked.load.*` call producing a
        // vector-typed result -- `feme::cpu::LinearizePass`'s masked form
        // of the already-supported groupshared vector `load` (roadmap
        // L11), reached once that same access is itself inside genuinely
        // divergent control flow -- decomposes into `N` per-component
        // widened gathers exactly like an unmasked groupshared vector
        // `load` already does (see `widenMaskedLoad`'s own vector case),
        // rather than one illegal `<W x <N x T>>` `llvm.masked.gather`.
        IsSupportedProducer = true;
      } else if (Function *Callee = CI->getCalledFunction()) {
        // Roadmap H6g-b-a-i-a-i-b: a vector-typed, homogeneous "trivially
        // vectorizable" intrinsic call (`llvm.minnum`/`llvm.maxnum`/
        // `llvm.smin`/`llvm.smax`/...) over an already-decomposed
        // divergent vector operand -- the shape a GLSL `min`/`max`/`clamp`
        // builtin over a vec-typed resource-load result takes (confirmed
        // by reducing a real failing `dEQP-VK.mesh_shader.ext.in_out`
        // case down to its exact IR shape once the row's own initial
        // `fcmp`/`icmp` fix let it progress this far) -- decomposes
        // exactly like ordinary elementwise arithmetic: one
        // scalar-element intrinsic call per component (see
        // `widenVectorElementwise`).
        Intrinsic::ID ID = Callee->getIntrinsicID();
        IsSupportedProducer =
            ID != Intrinsic::not_intrinsic &&
            isElementwiseVectorizableIntrinsic(ID) &&
            llvm::all_of(CI->args(), [&](const Value *Arg) {
              return Arg->getType() == I.getType();
            });
      }
    } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
      // An ordinary, non-groupshared divergent-address `load` producing a
      // vector-typed value -- e.g. `positions[gl_VertexIndex]` reading a
      // local constant lookup table indexed by a per-invocation builtin
      // like `gl_VertexIndex` (roadmap H7o, reduced from a real
      // `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_*`
      // vertex shader) -- decomposes into `N` widened per-component values
      // exactly like a vector-typed resource-call load already does (see
      // `widenScalarizedFallback`'s per-component reassembly), rather than
      // one illegal `<W x <N x T>>` result: its address, divergent because
      // the index feeding its `getelementptr` is, is scalarized into `W`
      // real per-lane loads, one through each lane's own extracted
      // pointer. A groupshared address's own vector-typed load (roadmap
      // L11, e.g. reading a whole `float4` row out of a `groupshared`
      // matrix at a divergent row index -- reduced from a real
      // `WaveOps/GroupSharedMatrixRowComponentDataRace.test` failure)
      // decomposes the same way, but via `widenGroupSharedLoad`'s own
      // per-component gather rather than this fallback's per-lane clone
      // (see that function's comment).
      IsSupportedProducer = LI->isSimple();
    }

    if (!IsSupportedProducer) {
      Ctx.emitError(
          "feme-cpu-simdize: function '" + OldF->getName() +
          "' has a divergent value '" + I.getName() +
          "' of vector type; only a constant-index insertelement chain, a "
          "phi, a select, a shufflevector, elementwise arithmetic/cast, a "
          "vector comparison, a homogeneous vectorizable intrinsic call, "
          "or a resource/image/ordinary load is supported (roadmap "
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
        // Roadmap H19a: a `feme.cpu.image.store.2d.*` call's own stored
        // `Texel` operand is decomposed exactly like a matched
        // resource-store call's stored value above -- the shape an
        // `imageStore()` GLSL builtin's own per-lane divergent value
        // takes, confirmed by reducing a real failing
        // `dEQP-VK.image.load_store.with_format.2d.*` case down to its
        // exact IR shape.
        std::optional<MatchedImageCall> ImgMatched = matchImageCall(*UserCI);
        if (ImgMatched && ImgMatched->Texel == &I)
          continue;
        // A `feme.cpu.masked.store.*` call (see MaskIntrinsics.h) is
        // `feme::cpu::LinearizePass`'s masked form of an ordinary `store`
        // under divergent control flow -- the shape a mesh entry point's
        // own `gl_PrimitiveTriangleIndicesEXT[...] = uvec3(...)` write
        // takes, since (unlike an output element write) it has no
        // canonicalized `feme.stage.*` op to become a `feme.cpu.resource.*`/
        // masked-output-store call instead (see MeshOutputWrapper.h's file
        // comment). Its stored value is decomposed exactly like a matched
        // resource-store call's (see `widenMaskedStore`).
        std::optional<MatchedMaskedMemOp> MaskedStore = matchMaskedStore(*UserCI);
        if (MaskedStore && MaskedStore->ValueOperand == &I)
          continue;
        // Roadmap H6g-b-a-i-a-i-b: a `llvm.vector.reduce.*` call folding
        // `I`'s own components together (see `isSupportedVectorReduceIntrinsic`/
        // `widenVectorReduce`) -- the shape glslang's `all`/`any`-style
        // GLSL builtins take over a component-wise vector comparison, e.g.
        // `llvm.vector.reduce.and.v4i1(fcmp ole <4 x float> %a, %b)`.
        Function *Callee = UserCI->getCalledFunction();
        if (Callee &&
            isSupportedVectorReduceIntrinsic(Callee->getIntrinsicID()) &&
            UserCI->getArgOperand(0) == &I)
          continue;
        // Roadmap H6g-b-a-i-a-i-b: an argument of a vector-typed,
        // homogeneous "trivially vectorizable" intrinsic call (see the
        // producer-side check above and `widenVectorElementwise`) -- `I`
        // is itself visited (and validated as a producer) by this same
        // top-level loop when its result is also vector-typed, so accept
        // it here unconditionally rather than re-checking argument
        // positions.
        if (Callee) {
          Intrinsic::ID ID = Callee->getIntrinsicID();
          if (ID != Intrinsic::not_intrinsic &&
              isElementwiseVectorizableIntrinsic(ID) &&
              llvm::all_of(UserCI->args(), [&](const Value *Arg) {
                return Arg->getType() == UserCI->getType();
              }))
            continue;
        }
      }
      if (isa<ExtractElementInst>(U))
        continue;
      // A `select`'s true/false operand is a supported consumer
      // unconditionally (widened by `widenVectorSelect`'s per-component
      // reassembly regardless of the condition's own shape); its
      // condition, when `I` itself, is accepted only when it is a vector
      // (roadmap H6g-b-a-i-a-i-b's own per-lane `<N x i1>` condition,
      // decomposed alongside the true/false operands rather than shared
      // unchanged the way a scalar condition is) -- a scalar `i1`
      // condition never reaches this loop at all, since only a
      // vector-typed divergent value is visited here in the first place.
      if (auto *UserSel = dyn_cast<SelectInst>(U))
        if (UserSel->getTrueValue() == &I || UserSel->getFalseValue() == &I ||
            (UserSel->getCondition() == &I && I.getType()->isVectorTy()))
          continue;
      if (isa<CmpInst>(U))
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
          "extractelement/select/shufflevector/phi/elementwise/comparison/"
          "reduce/vectorizable-intrinsic pattern; component decomposition "
          "is not yet supported for this use (roadmap milestone 7 "
          "deviation)");
      return false;
    }
  }
  return true;
}

/// The aggregate analogue of the vector-specific producer/consumer checks
/// inside `checkVectorDecompositionSupported` above (see that function's
/// own file comment for the full picture, roadmap milestone L21): verifies
/// that the divergent, aggregate-typed \p I is one of the two supported
/// producer shapes (an `insertvalue` or a nested sub-aggregate
/// `extractvalue`, both requiring every leaf `isAllScalarAggregateLeaves`
/// reaches to be a genuine scalar) and that every use of it is one of the
/// three supported consumer shapes (another `insertvalue`'s aggregate-base
/// or inserted-value operand, or an `extractvalue`'s aggregate operand).
bool FunctionWidener::checkAggregateValueSupported(Instruction &I) {
  bool IsSupportedProducer = false;
  if (isa<InsertValueInst>(&I) || isa<ExtractValueInst>(&I))
    IsSupportedProducer = isAllScalarAggregateLeaves(I.getType());

  if (!IsSupportedProducer) {
    Ctx.emitError(
        "feme-cpu-simdize: function '" + OldF->getName() +
        "' has a divergent value '" + I.getName() +
        "' of aggregate type; component decomposition is not yet supported "
        "for this producer (only an insertvalue chain or a nested "
        "sub-aggregate extractvalue, over an all-scalar-leaves struct/"
        "array, is supported) (roadmap milestone 7/L21 deviation)");
    return false;
  }

  for (User *U : I.users()) {
    if (auto *UserIV = dyn_cast<InsertValueInst>(U))
      if (UserIV->getAggregateOperand() == &I ||
          UserIV->getInsertedValueOperand() == &I)
        continue;
    if (isa<ExtractValueInst>(U))
      continue;
    Ctx.emitError(
        "feme-cpu-simdize: function '" + OldF->getName() +
        "' has a divergent aggregate value '" + I.getName() +
        "' used outside a supported insertvalue/extractvalue pattern; "
        "component decomposition is not yet supported for this use "
        "(roadmap milestone 7/L21 deviation)");
    return false;
  }
  return true;
}

Function *FunctionWidener::buildWidenedFunction() {
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *MaskTy = FixedVectorType::get(Type::getInt1Ty(Ctx), WaveSize);

  SmallVector<Type *, 8> ParamTypes(OldF->getFunctionType()->params());
  ParamTypes.append(
      {I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, MaskTy, MaskTy, PtrTy});

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
  Env.GroupCountX = &*ArgIt++;
  Env.GroupCountX->setName("wave_group_count_x");
  Env.GroupCountY = &*ArgIt++;
  Env.GroupCountY->setName("wave_group_count_y");
  Env.GroupCountZ = &*ArgIt++;
  Env.GroupCountZ->setName("wave_group_count_z");
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
  // `getWidened` only ever produces a flat `<W x T>` for a *scalar*-typed
  // `V` (`T` itself must be a valid vector element type). A vector-typed
  // `V` (e.g. a whole `<4 x float>` being stored through a divergent
  // per-lane pointer -- roadmap H6n) has its own decomposed, per-component
  // widened form instead; route it through `getVectorComponents`, never
  // here (a caller reaching this assert has skipped that routing and would
  // otherwise build an illegal `<W x <N x T>>` nested vector).
  assert(!V->getType()->isVectorTy() &&
        "getWidened does not support a vector-typed value; use "
        "getVectorComponents instead");
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

SmallVector<Value *, 8>
FunctionWidener::getAggregateComponents(Value *V, IRBuilderBase &Builder) {
  // The aggregate analogue of `getVectorComponents` (roadmap milestone
  // L21): either read back an already-decomposed divergent aggregate's
  // flat leaf components, or build the widened form of each of a
  // *uniform* aggregate's (constant or not) leaves directly, one
  // `getWidened` broadcast (or `getVectorComponents`/`getAggregateComponents`
  // recursive read, if a leaf is itself a vector or nested aggregate --
  // impossible here, since every leaf is a genuine scalar per
  // `isAllScalarAggregateLeaves`) per flattened leaf.
  if (auto It = WidenedAggregateComponents.find(V);
      It != WidenedAggregateComponents.end())
    return It->second;

  if (isa<UndefValue>(V)) {
    SmallVector<Type *, 8> LeafTypes;
    flattenAggregateLeafScalarTypes(V->getType(), LeafTypes);
    SmallVector<Value *, 8> Components;
    for (Type *LeafTy : LeafTypes)
      Components.push_back(
          PoisonValue::get(FixedVectorType::get(LeafTy, WaveSize)));
    return Components;
  }

  SmallVector<SmallVector<unsigned, 4>, 8> Paths;
  SmallVector<unsigned, 4> Prefix;
  collectAggregateLeafIndexPaths(V->getType(), Prefix, Paths);
  SmallVector<Value *, 8> Components;
  for (ArrayRef<unsigned> Path : Paths)
    Components.push_back(
        getWidened(Builder.CreateExtractValue(V, Path), Builder));
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

// (Roadmap H6c-a-a-i) Unlike `widenMaskedTaskPayloadStore`'s scalar
// `Offset`, both `VertexCount` and `PrimitiveCount` are genuine per-lane
// values here (see `applyStageMasks`'s own comment) and are widened
// identically -- they share `getOrInsertMaskedSetMeshOutputs`'s single
// `CountTy` parameter since `StageOpKind::SetMeshOutputs` is not
// overloaded (both operands are always `i32`, so widening always leaves
// them the same vector type).
void FunctionWidener::widenMaskedSetMeshOutputs(CallInst &CI,
                                                IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *VertexCount = getWidened(CI.getArgOperand(0), Builder);
  Value *PrimitiveCount = getWidened(CI.getArgOperand(1), Builder);
  Value *Mask = Builder.CreateAnd(Env.SideEffectMask,
                                  getWidened(CI.getArgOperand(2), Builder),
                                  "set.mesh.outputs.mask");
  FunctionCallee Callee = getOrInsertMaskedSetMeshOutputs(
      *M, VertexCount->getType(), Mask->getType());
  Builder.CreateCall(Callee, {VertexCount, PrimitiveCount, Mask});
  ToErase.push_back(&CI);
}

// (Roadmap H6s) Mirrors `widenMaskedSetMeshOutputs` immediately above
// exactly, just for three genuine per-lane operands (`group_count_x/y/z`)
// instead of two -- they share `getOrInsertMaskedEmitMeshTasks`'s single
// `CountTy` parameter since `StageOpKind::EmitMeshTasks` is likewise not
// overloaded (all three operands are always `i32`).
void FunctionWidener::widenMaskedEmitMeshTasks(CallInst &CI,
                                               IRBuilder<> &Builder) {
  Module *M = NewF->getParent();
  Value *GroupCountX = getWidened(CI.getArgOperand(0), Builder);
  Value *GroupCountY = getWidened(CI.getArgOperand(1), Builder);
  Value *GroupCountZ = getWidened(CI.getArgOperand(2), Builder);
  Value *Mask = Builder.CreateAnd(Env.SideEffectMask,
                                  getWidened(CI.getArgOperand(3), Builder),
                                  "emit.mesh.tasks.mask");
  FunctionCallee Callee = getOrInsertMaskedEmitMeshTasks(
      *M, GroupCountX->getType(), Mask->getType());
  Builder.CreateCall(Callee, {GroupCountX, GroupCountY, GroupCountZ, Mask});
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

// Roadmap H6o: `NumWorkgroups` is uniform for this widened function, like
// `WorkgroupId`, but a genuine runtime dispatch-time value rather than a
// compile-time constant -- see `isNumWorkgroupsCall`'s comment -- so it
// substitutes directly for `Env.GroupCountX/Y/Z`, threaded through the
// wave-body interface exactly like `Env.GroupIDX/Y/Z` above.
void FunctionWidener::replaceNumWorkgroupsCall(CallInst &CI) {
  unsigned Component = static_cast<unsigned>(
      cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue());
  Value *Replacement = Component == 0   ? Env.GroupCountX
                       : Component == 1 ? Env.GroupCountY
                                        : Env.GroupCountZ;
  CI.replaceAllUsesWith(Replacement);
  ToErase.push_back(&CI);
}

// Roadmap H6n: `SubgroupId` is uniform for this widened, one-wave-loop-
// iteration-per-call function -- see `isSubgroupIdCall`'s comment -- so it
// substitutes directly for `Env.WaveIndex`, exactly like `WorkgroupId`
// substitutes for `Env.GroupIDX/Y/Z` in `replaceGroupIdCall` just above.
void FunctionWidener::replaceSubgroupIdCall(CallInst &CI) {
  CI.replaceAllUsesWith(Env.WaveIndex);
  ToErase.push_back(&CI);
}

// Roadmap H6n: `NumSubgroups` folds to a compile-time constant --
// `ceil(NumThreads.x * NumThreads.y * NumThreads.z / WaveSize)`, mirroring
// `feme/docs/FeMeCPUDesign.md`'s own `group = ceil(GroupSize / W) waves`
// formula for the wave loop's own trip count -- see `isNumSubgroupsCall`'s
// comment.
void FunctionWidener::replaceNumSubgroupsCall(CallInst &CI) {
  uint64_t GroupSize = uint64_t{NumThreads[0]} * NumThreads[1] * NumThreads[2];
  uint64_t WavesPerGroup = (GroupSize + WaveSize - 1) / WaveSize;
  Value *Replacement =
      ConstantInt::get(CI.getType(), WavesPerGroup, /*IsSigned=*/false);
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
  // prevented from touching memory at all. `Matched.Comparator`
  // (`AtomicCompareExchangeTyped` only, roadmap H8w) is an ordinary scalar
  // `i32` operand exactly like `StoredValue`, just never vector-typed
  // (SPIR-V's own comparator is always a scalar), so it needs no
  // `StoredValueIsVector`-style decomposition of its own.
  bool AnyDivergent = Widened.count(Matched.DescriptorIndex) ||
                      Widened.count(Matched.Offset) || StoredValueDivergent ||
                      Widened.count(Matched.Mask) ||
                      (Matched.Comparator && Widened.count(Matched.Comparator));
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
  Value *WideComparator =
      Matched.Comparator ? getWidened(Matched.Comparator, Builder) : nullptr;

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
  // A plain store's call type is void (no result); a load's or, since
  // roadmap H8w, an `Atomic*Typed` call's is not -- the latter carries a
  // `StoredValue` operand too (the RMW/xchg value), so `CI.getType()`
  // itself, not `!Matched.StoredValue`, is what actually distinguishes
  // "produces a result to reassemble" from "a pure side effect" (mirroring
  // `widenImageCall`'s own identical `ResultIsVoid`/`ResultIsVector` split
  // for the same reason, roadmap H8v).
  bool ResultIsVoid = CI.getType()->isVoidTy();
  bool ResultIsVector = !ResultIsVoid && CI.getType()->isVectorTy();
  if (!ResultIsVoid) {
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
    if (Matched.Comparator) {
      // `AtomicCompareExchangeTyped` only (roadmap H8w): the comparator
      // operand comes ahead of the value operand (see
      // `feme::cpu::createAtomicCompareExchangeTyped`'s own argument
      // order).
      CallArgs.push_back(Builder.CreateExtractElement(
          WideComparator, Builder.getInt32(Lane), "lane.comparator"));
    }
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
  // therefore never divergent. Roadmap H19a: the sole exception is a
  // `feme.cpu.image.store.2d.*` call's `Texel` operand, since (unlike every
  // other operand this function widens) it is itself vector-typed and, when
  // divergent, is decomposed into per-component wide vectors
  // (`WidenedVectorComponents`) rather than a single `<W x T>`
  // (`Widened`) -- the same distinction `widenResourceCall` already makes
  // for its own vector-typed `StoredValue`.
  unsigned MaskIdx = CI.arg_size() - 1;
  int TexelArgIdx = -1;
  if (Matched.Texel)
    for (unsigned I = 0; I != MaskIdx; ++I)
      if (CI.getArgOperand(I) == Matched.Texel) {
        TexelArgIdx = static_cast<int>(I);
        break;
      }
  bool TexelDivergent =
      Matched.Texel && WidenedVectorComponents.count(Matched.Texel);

  bool AnyDivergent = Widened.count(Matched.Mask) != 0 || TexelDivergent;
  for (unsigned I = 0; I != MaskIdx; ++I) {
    if (static_cast<int>(I) == TexelArgIdx)
      continue;
    AnyDivergent |= Widened.count(CI.getArgOperand(I)) != 0;
  }
  if (!AnyDivergent)
    return; // A uniform sample/store: leave the scalar call as-is.

  SmallVector<Value *, 12> WideArgs(MaskIdx, nullptr);
  for (unsigned I = 0; I != MaskIdx; ++I) {
    if (static_cast<int>(I) == TexelArgIdx)
      continue;
    if (Widened.count(CI.getArgOperand(I)))
      WideArgs[I] = getWidened(CI.getArgOperand(I), Builder);
  }
  SmallVector<Value *, 4> WideTexelComponents;
  if (TexelDivergent)
    WideTexelComponents = WidenedVectorComponents.lookup(Matched.Texel);

  // A store's governing mask must gate the real memory side effect it
  // causes (matching `widenResourceCall`'s own `Env.SideEffectMask` choice
  // for its `StoredValue` case); a read only needs the wave's entry mask.
  // (Roadmap H8v) An atomic (`AtomicValue` non-null) is likewise a real
  // memory side effect -- an inactive/helper lane must never perform one
  // -- so it needs `SideEffectMask` too, exactly like a store's `Texel`.
  Value *LaneMaskBase = (Matched.Texel || Matched.AtomicValue)
                           ? Env.SideEffectMask
                           : Env.EntryMask;
  if (!isa<Constant>(Matched.Mask))
    LaneMaskBase = Builder.CreateAnd(
        LaneMaskBase, getWidened(Matched.Mask, Builder), "image.mask");

  Function *Callee = CI.getCalledFunction();
  Value *Result = nullptr;
  SmallVector<Value *, 4> LoadComponents;
  bool ResultIsVoid = CI.getType()->isVoidTy();
  bool ResultIsVector = !ResultIsVoid && CI.getType()->isVectorTy();
  if (ResultIsVector) {
    auto *VecTy = cast<FixedVectorType>(CI.getType());
    LoadComponents.assign(VecTy->getNumElements(),
                          PoisonValue::get(FixedVectorType::get(
                              VecTy->getElementType(), WaveSize)));
  } else if (!ResultIsVoid) {
    Result = PoisonValue::get(FixedVectorType::get(CI.getType(), WaveSize));
  }

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    SmallVector<Value *, 12> CallArgs;
    for (unsigned I = 0; I != MaskIdx; ++I) {
      if (static_cast<int>(I) == TexelArgIdx) {
        if (!TexelDivergent) {
          // A uniform stored texel is identical for every lane, so it
          // needs no per-lane extraction.
          CallArgs.push_back(Matched.Texel);
          continue;
        }
        Value *LaneVector = PoisonValue::get(Matched.Texel->getType());
        for (unsigned Component = 0,
                      NumComponents = WideTexelComponents.size();
             Component != NumComponents; ++Component) {
          Value *LaneScalar = Builder.CreateExtractElement(
              WideTexelComponents[Component], Builder.getInt32(Lane),
              "lane.texel.elt");
          LaneVector = Builder.CreateInsertElement(
              LaneVector, LaneScalar, Builder.getInt32(Component));
        }
        CallArgs.push_back(LaneVector);
        continue;
      }
      CallArgs.push_back(WideArgs[I] ? Builder.CreateExtractElement(
                                           WideArgs[I], Builder.getInt32(Lane),
                                           "lane.image.arg")
                                     : CI.getArgOperand(I));
    }
    CallArgs.push_back(Builder.CreateExtractElement(
        LaneMaskBase, Builder.getInt32(Lane), "lane.mask"));

    Value *LaneResult = Builder.CreateCall(Callee, CallArgs);
    if (ResultIsVoid)
      continue;
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
  else if (Result)
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

  // (Roadmap L15) A vector-typed result -- e.g. a masked read of a whole
  // `float4` row out of a `groupshared float4x4` at a per-lane row index,
  // the shape `feme::cpu::LinearizePass`'s `maskMemoryOps` produces once
  // that same L11 access is itself inside genuinely divergent control flow
  // (a real `if (ThreadID.x == 0)` guard, as
  // `WaveOps/GroupSharedMatrixRowComponentDataRace.test` itself has) --
  // cannot gather directly into one illegal `<W x <N x T>>`; it decomposes
  // into `N` separate per-component gathers instead, mirroring
  // `widenGroupSharedLoad`'s own vector case exactly (one `getelementptr`
  // per component off the same `<W x ptr>` row address, LLVM's
  // fixed-vector-source-element-type GEP indexing plus its
  // vector-base-pointer scalar-index broadcast), except that each
  // component's own passthru is this call's own (possibly divergent, so
  // itself already-decomposed) `ValueOperand`, sliced into its own `N`
  // widened components by `getVectorComponents` rather than the constant
  // `zeroinitializer` `widenGroupSharedLoad`'s unmasked raw load always
  // uses, and gathers using this call's own `EffectiveMask` (the entry
  // mask further narrowed by this masked load's own governing mask)
  // instead of the bare `Env.EntryMask` an unmasked raw load gathers with.
  if (auto *VecTy = dyn_cast<FixedVectorType>(CI.getType())) {
    Type *ElemTy = VecTy->getElementType();
    const DataLayout &DL = NewF->getParent()->getDataLayout();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
    SmallVector<Value *, 4> PassthruComponents =
        getVectorComponents(Matched.ValueOperand, Builder);
    SmallVector<Value *, 4> Components;
    for (unsigned Idx = 0, End = VecTy->getNumElements(); Idx != End; ++Idx) {
      Value *ElemPtr = Builder.CreateGEP(
          VecTy, WidePtr, {Builder.getInt32(0), Builder.getInt32(Idx)},
          CI.getName() + ".elt" + Twine(Idx) + ".ptr");
      Align ElemAlign =
          commonAlignment(Align(Matched.Align ? Matched.Align : 1),
                          Idx * ElemSize);
      Components.push_back(Builder.CreateMaskedGather(
          FixedVectorType::get(ElemTy, WaveSize), ElemPtr, ElemAlign,
          EffectiveMask, PassthruComponents[Idx],
          CI.getName() + ".elt" + Twine(Idx)));
    }
    WidenedVectorComponents[&CI] = std::move(Components);
    ToErase.push_back(&CI);
    return;
  }

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

  // "Vectors become components, not nested vectors" (roadmap H6g-b-a-i-a-i-a):
  // a vector-typed stored value -- a mesh entry point's own
  // `gl_PrimitiveTriangleIndicesEXT[...] = uvec3(...)`, the shape a
  // dEQP-VK.mesh_shader.* re-run first surfaced this gap with -- is
  // decomposed into per-component wide values exactly like a matched
  // resource-store call's stored value (`widenResourceCall`), not one
  // illegal `<W x <N x T>>` `getWidened` would otherwise try to broadcast.
  // `llvm.masked.scatter` has no vector-of-vector-element form to lower
  // that to anyway, so each lane's own reassembled vector is written
  // individually instead, guarded by the same load-select-store idiom
  // `MeshOutputWrapper.cpp`'s `lowerMeshOutputStore` already uses for its
  // own per-lane conditional writes.
  if (Matched.ValueOperand->getType()->isVectorTy()) {
    // (Roadmap L15) A groupshared address (e.g. a masked write to a whole
    // `float4` row of a `groupshared float4x4` at a per-lane row index,
    // reduced from a real `WaveOps/GroupSharedMatrixRowComponentDataRace
    // .test` failure) cannot use the generic per-lane load-select-store
    // idiom below: that idiom extracts each lane's own scalar pointer out
    // of `WidePtr` with an `extractelement`, a leaf `feme::cpu::
    // rewriteGroupSharedGlobals` does not (and, since a groupshared
    // address is retargeted to an entirely different, real per-wave
    // buffer rather than an ordinary heap address, safely cannot)
    // recognize as one of its own supported groupshared leaf users. A
    // groupshared address instead decomposes into `N` per-component
    // `llvm.masked.scatter`s, mirroring `widenMaskedLoad`'s own vector
    // case exactly (one `getelementptr` per component off the same
    // `<W x ptr>` row address, each scattering that component's own
    // widened value) -- the second-level-getelementptr-feeding-a-
    // masked-gather-or-scatter shape `rewriteGroupSharedGlobals`'s own
    // validation and retargeting already generically support (see its
    // own comments), so no further change is needed there.
    if (isGroupSharedPointerType(Matched.Ptr->getType())) {
      auto *VecTy = cast<FixedVectorType>(Matched.ValueOperand->getType());
      Type *ElemTy = VecTy->getElementType();
      const DataLayout &DL = NewF->getParent()->getDataLayout();
      uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
      SmallVector<Value *, 4> Components =
          getVectorComponents(Matched.ValueOperand, Builder);
      for (unsigned Idx = 0, End = VecTy->getNumElements(); Idx != End;
           ++Idx) {
        Value *ElemPtr = Builder.CreateGEP(
            VecTy, WidePtr, {Builder.getInt32(0), Builder.getInt32(Idx)},
            CI.getName() + ".elt" + Twine(Idx) + ".ptr");
        Align ElemAlign =
            commonAlignment(Align(Matched.Align ? Matched.Align : 1),
                            Idx * ElemSize);
        Builder.CreateMaskedScatter(Components[Idx], ElemPtr, ElemAlign,
                                    EffectiveMask);
      }
      ToErase.push_back(&CI);
      return;
    }

    SmallVector<Value *, 4> Components =
        getVectorComponents(Matched.ValueOperand, Builder);
    Type *ValueTy = Matched.ValueOperand->getType();
    Align StoreAlign(Matched.Align ? Matched.Align : 1);
    for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
      Value *LaneMask = Builder.CreateExtractElement(
          EffectiveMask, Builder.getInt32(Lane), "lane.mask");
      Value *LanePtr = Builder.CreateExtractElement(
          WidePtr, Builder.getInt32(Lane), "lane.ptr");
      Value *LaneVector = PoisonValue::get(ValueTy);
      for (unsigned Component = 0, NumComponents = Components.size();
           Component != NumComponents; ++Component) {
        Value *LaneScalar = Builder.CreateExtractElement(
            Components[Component], Builder.getInt32(Lane), "lane.value.elt");
        LaneVector = Builder.CreateInsertElement(
            LaneVector, LaneScalar, Builder.getInt32(Component));
      }
      Value *OldVal = Builder.CreateAlignedLoad(ValueTy, LanePtr, StoreAlign);
      Value *NewVal = Builder.CreateSelect(LaneMask, LaneVector, OldVal);
      Builder.CreateAlignedStore(NewVal, LanePtr, StoreAlign);
    }
    ToErase.push_back(&CI);
    return;
  }

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
  // remaining atomic-instruction callers. A vector-typed `I` (roadmap H7o:
  // an ordinary, non-groupshared divergent-address `load` reading a local
  // constant lookup table, e.g. `positions[gl_VertexIndex]`, is this
  // shape's only producer today) decomposes its per-lane clone's own
  // result into `N` widened components -- recorded in
  // `WidenedVectorComponents`, exactly like every other vector-typed
  // producer `checkVectorDecompositionSupported` accepts -- rather than
  // building one illegal `<W x <N x T>>` `Result`.
  //
  // An *operand* can itself be vector-typed too (e.g. a divergently
  // per-lane-indexed `store <4 x float> ...` writing a whole vector, not
  // just a scalar or fixed-vector *result* -- roadmap H6n): `getWidened`
  // alone cannot widen such an operand, since it always broadcasts/gathers
  // into a flat `<W x T>` and a vector-typed `T` is not a valid vector
  // element type (`getWidened(<4 x float> value)` would build an illegal
  // `<W x <4 x float>>`). Route any vector-typed operand through
  // `getVectorComponents` instead -- the same per-component decomposition
  // every other vector-typed consumer already uses -- and reassemble each
  // lane's own real `<4 x float>` value (one `extractelement` +
  // `insertelement` chain per component) right before that lane's clone,
  // rather than ever materializing a nested wide vector.
  SmallVector<Value *, 4> WideOps;
  SmallVector<SmallVector<Value *, 4>, 4> WideVectorOps;
  BitVector IsVectorOp(I.getNumOperands());
  unsigned OpIdx = 0;
  for (Value *Op : I.operands()) {
    if (Op->getType()->isVectorTy()) {
      IsVectorOp.set(OpIdx);
      WideVectorOps.push_back(getVectorComponents(Op, Builder));
      WideOps.push_back(nullptr);
    } else {
      WideVectorOps.push_back({});
      WideOps.push_back(getWidened(Op, Builder));
    }
    ++OpIdx;
  }

  bool HasResult = !I.getType()->isVoidTy();
  auto *ResultVecTy =
      HasResult ? dyn_cast<FixedVectorType>(I.getType()) : nullptr;
  Value *Result = (HasResult && !ResultVecTy)
                      ? PoisonValue::get(FixedVectorType::get(I.getType(), WaveSize))
                      : nullptr;
  SmallVector<Value *, 4> Components;
  if (ResultVecTy)
    Components.assign(ResultVecTy->getNumElements(),
                      PoisonValue::get(FixedVectorType::get(
                          ResultVecTy->getElementType(), WaveSize)));

  for (unsigned Lane = 0; Lane != WaveSize; ++Lane) {
    Instruction *Clone = I.clone();
    for (unsigned OpIdx = 0, E = WideOps.size(); OpIdx != E; ++OpIdx) {
      if (IsVectorOp.test(OpIdx)) {
        // Reassemble this lane's own real vector value, one component at
        // a time, from its decomposed `<W x componentT>` widened form --
        // see the comment above this fallback's operand-widening loop.
        const SmallVector<Value *, 4> &Comps = WideVectorOps[OpIdx];
        auto *VecTy = cast<FixedVectorType>(I.getOperand(OpIdx)->getType());
        Value *LaneVector = PoisonValue::get(VecTy);
        for (unsigned Component = 0, NumComponents = Comps.size();
             Component != NumComponents; ++Component) {
          Value *LaneScalar = Builder.CreateExtractElement(
              Comps[Component], Builder.getInt32(Lane), "lane.op.elt");
          LaneVector = Builder.CreateInsertElement(
              LaneVector, LaneScalar, Builder.getInt32(Component));
        }
        Clone->setOperand(OpIdx, LaneVector);
      } else {
        Clone->setOperand(OpIdx,
                          Builder.CreateExtractElement(
                              WideOps[OpIdx], Builder.getInt32(Lane), "lane.op"));
      }
    }
    // A void-typed `I` (e.g. a masked output store with no widened handler
    // of its own) clones to a void `Clone`: naming it would assert (`Value::
    // setNameImpl`'s "Cannot assign a name to void values!"), so only a
    // `HasResult` clone gets the ".lane" name.
    Builder.Insert(Clone, HasResult ? I.getName() + ".lane" : Twine());
    if (ResultVecTy) {
      for (unsigned Component = 0, NumComponents = ResultVecTy->getNumElements();
           Component != NumComponents; ++Component) {
        Value *LaneScalar = Builder.CreateExtractElement(
            Clone, Builder.getInt32(Component), "lane.op.elt");
        Components[Component] = Builder.CreateInsertElement(
            Components[Component], LaneScalar, Builder.getInt32(Lane));
      }
    } else if (Result) {
      Result =
          Builder.CreateInsertElement(Result, Clone, Builder.getInt32(Lane));
    }
  }

  if (ResultVecTy)
    WidenedVectorComponents[&I] = std::move(Components);
  else if (Result)
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

  // (Roadmap L11) A vector-typed result -- e.g. reading a whole `float4`
  // row out of a `groupshared` matrix at a per-lane row index, reduced
  // from a real `WaveOps/GroupSharedMatrixRowComponentDataRace.test`
  // failure -- cannot gather directly into one illegal `<W x <N x T>>`;
  // it decomposes into `N` separate per-component gathers instead, one
  // per element of `LI`'s own vector type, recorded in
  // `WidenedVectorComponents` exactly like every other vector-typed
  // producer `checkVectorDecompositionSupported` accepts. `WidePtr` is a
  // `<W x ptr>` pointing at the *whole row* in every lane; LLVM's `getelementptr`
  // treats a fixed-vector source element type as an indexable sequential
  // type exactly like an array (a leading `0` index stays at the same
  // row, a second index selects one of its `N` elements), and broadcasts
  // that scalar index across every lane of a vector base pointer
  // automatically, so one such `getelementptr` per component gives each
  // lane's own per-component address without any extra per-lane
  // extraction.
  if (auto *VecTy = dyn_cast<FixedVectorType>(LI.getType())) {
    Type *ElemTy = VecTy->getElementType();
    // `OldF` is already null by Pass 2 (see its declaration comment above);
    // `NewF` is the live function being built and shares the same module
    // (and therefore the same `DataLayout`) as `OldF` did.
    const DataLayout &DL = NewF->getParent()->getDataLayout();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
    Value *ElemPassthru =
        Constant::getNullValue(FixedVectorType::get(ElemTy, WaveSize));
    SmallVector<Value *, 4> Components;
    for (unsigned Idx = 0, End = VecTy->getNumElements(); Idx != End; ++Idx) {
      Value *ElemPtr = Builder.CreateGEP(
          VecTy, WidePtr, {Builder.getInt32(0), Builder.getInt32(Idx)},
          LI.getName() + ".elt" + Twine(Idx) + ".ptr");
      // `LI`'s own alignment only bounds the *row's* address, not every
      // component's: e.g. a 16-byte-aligned `<4 x float>` row's second
      // element sits at a 4-byte-aligned offset, not a 16-byte-aligned
      // one, so using `LI.getAlign()` unmodified for every component
      // would overstate a non-zero-offset component's real alignment.
      Align ElemAlign = commonAlignment(LI.getAlign(), Idx * ElemSize);
      Components.push_back(Builder.CreateMaskedGather(
          FixedVectorType::get(ElemTy, WaveSize), ElemPtr, ElemAlign,
          Env.EntryMask, ElemPassthru, LI.getName() + ".elt" + Twine(Idx)));
    }
    WidenedVectorComponents[&LI] = std::move(Components);
    ToErase.push_back(&LI);
    return;
  }

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

void FunctionWidener::widenInsertValue(InsertValueInst &IV,
                                       IRBuilder<> &Builder) {
  // The aggregate analogue of `widenInsertElement` (roadmap milestone
  // L21): start from the aggregate base's own flat leaf components
  // (`getAggregateComponents` handles both a decomposed divergent base and
  // a uniform one, including `poison`/`undef`), overwrite the leaf range
  // `IV`'s own index path selects (`getAggregateLeafRange`) with the
  // inserted value's own widened form -- a single wide scalar
  // (`getWidened`) when it names a genuine leaf, or another flat
  // component list (`getAggregateComponents`) when it names a whole
  // sub-aggregate inserted at once (e.g. `packed.test`'s own `Legs`
  // sub-array inserted whole into `Doggo`'s field 0) -- and record the
  // result for the next link (or an extractvalue/insertvalue consumer);
  // this instruction itself never gets a single widened `<W x T>`
  // replacement, exactly like `widenInsertElement`.
  SmallVector<Value *, 8> Components =
      getAggregateComponents(IV.getAggregateOperand(), Builder);

  auto [Offset, Count] =
      getAggregateLeafRange(IV.getAggregateOperand()->getType(), IV.getIndices());
  Value *Inserted = IV.getInsertedValueOperand();
  if (Inserted->getType()->isAggregateType()) {
    SmallVector<Value *, 8> InsertedComponents =
        getAggregateComponents(Inserted, Builder);
    for (unsigned I = 0; I != Count; ++I)
      Components[Offset + I] = InsertedComponents[I];
  } else {
    Components[Offset] = getWidened(Inserted, Builder);
  }

  WidenedAggregateComponents[&IV] = std::move(Components);
  ToErase.push_back(&IV);
}

void FunctionWidener::widenExtractValue(ExtractValueInst &EV,
                                        IRBuilder<> &Builder) {
  // The dual of `widenInsertValue`: reads the leaf range `EV`'s own index
  // path selects straight out of `getAggregateComponents` rather than
  // extracting from a single widened aggregate (there is none -- see
  // `checkVectorDecompositionSupported`'s file comment for why a divergent
  // aggregate is never given one). A scalar-result `EV` (a genuine leaf)
  // gets the usual single `Widened` entry; an aggregate-result `EV` (a
  // nested sub-aggregate extraction, e.g. `packed.test`'s own two-level
  // extractvalue chain reading a whole array field before reading its
  // final scalar element) instead gets its own `WidenedAggregateComponents`
  // slice, exactly like any other aggregate-typed producer.
  SmallVector<Value *, 8> Components =
      getAggregateComponents(EV.getAggregateOperand(), Builder);

  auto [Offset, Count] =
      getAggregateLeafRange(EV.getAggregateOperand()->getType(), EV.getIndices());
  if (EV.getType()->isAggregateType()) {
    WidenedAggregateComponents[&EV] = SmallVector<Value *, 8>(
        Components.begin() + Offset, Components.begin() + Offset + Count);
  } else {
    Widened[&EV] = Components[Offset];
  }
  ToErase.push_back(&EV);
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
  // A vector-typed `select`'s condition is either scalar `i1`, shared
  // unchanged by every per-component `select` below (a single broadcast
  // covers every lane and every vector component alike), or -- roadmap
  // H6g-b-a-i-a-i-b -- itself a per-lane `<N x i1>` vector (e.g. the
  // `<N x i1>` result of a component-wise `fcmp`/`icmp`), decomposed into
  // `N` widened `<W x i1>` components exactly like any other divergent
  // vector operand, one used per `select`. Either way, "Vectors become
  // components, not nested vectors" applies to a `select` exactly like a
  // `phi`/`shufflevector`/`insertelement` chain.
  Value *Cond = SI.getCondition();
  Value *WideCond = nullptr;
  SmallVector<Value *, 4> CondComponents;
  if (Cond->getType()->isVectorTy())
    CondComponents = getVectorComponents(Cond, Builder);
  else
    WideCond = getWidened(Cond, Builder);

  SmallVector<Value *, 4> TrueComponents =
      getVectorComponents(SI.getTrueValue(), Builder);
  SmallVector<Value *, 4> FalseComponents =
      getVectorComponents(SI.getFalseValue(), Builder);

  SmallVector<Value *, 4> Components;
  for (unsigned I = 0, E = TrueComponents.size(); I != E; ++I)
    Components.push_back(Builder.CreateSelect(
        CondComponents.empty() ? WideCond : CondComponents[I],
        TrueComponents[I], FalseComponents[I], SI.getName() + ".wide" + Twine(I)));

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
  // component lists line up component-for-component. A `CallInst`'s own
  // "operands" include its callee (the last one), which is never widened,
  // so `I.args()` is used instead for that case (roadmap
  // H6g-b-a-i-a-i-b's homogeneous vectorizable-intrinsic shape).
  auto *ICall = dyn_cast<CallInst>(&I);
  SmallVector<SmallVector<Value *, 4>, 2> OperandComponents;
  for (Value *Op : ICall ? iterator_range(ICall->arg_begin(), ICall->arg_end())
                          : iterator_range(I.op_begin(), I.op_end()))
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
                 ? getWidened((ICall ? ICall->getArgOperand(OpIdx)
                                     : I.getOperand(OpIdx)),
                              Builder)
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
    } else if (auto *Cmp = dyn_cast<CmpInst>(&I)) {
      // A vector `fcmp`/`icmp` (roadmap H6g-b-a-i-a-i-b): its `<N x i1>`
      // result decomposes into `N` per-component `<W x i1>` comparisons
      // exactly like a `BinaryOperator`'s two operands do -- `WideElemTy`
      // above is already `<W x i1>` here, since it is derived from `I`'s
      // own (boolean-vector) result type.
      NewV = Builder.CreateCmp(Cmp->getPredicate(), ComponentOperand(0),
                               ComponentOperand(1),
                               I.getName() + ".wide" + Twine(C));
    } else if (ICall) {
      // Roadmap H6g-b-a-i-a-i-b: a homogeneous "trivially vectorizable"
      // intrinsic call (`llvm.minnum`/`llvm.maxnum`/`llvm.smin`/
      // `llvm.smax`/...) over an already-decomposed divergent vector
      // operand -- the shape a GLSL `min`/`max`/`clamp` builtin over a
      // vec-typed resource-load result takes -- widens to the identical
      // intrinsic's `<W x elemT>` overload, called once per component,
      // mirroring `widenElementwise`'s equivalent uniform-broadcast case.
      Intrinsic::ID ID = ICall->getCalledFunction()->getIntrinsicID();
      Function *WideCallee =
          Intrinsic::getOrInsertDeclaration(NewF->getParent(), ID, {WideElemTy});
      SmallVector<Value *, 4> WideArgs;
      for (unsigned OpIdx = 0, E = ICall->arg_size(); OpIdx != E; ++OpIdx)
        WideArgs.push_back(ComponentOperand(OpIdx));
      NewV = Builder.CreateCall(WideCallee, WideArgs,
                                I.getName() + ".wide" + Twine(C));
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

void FunctionWidener::widenVectorReduce(CallInst &CI, IRBuilder<> &Builder) {
  // Roadmap H6g-b-a-i-a-i-b: fold a (possibly divergent, possibly
  // per-lane-decomposed) vector operand's `N` components together two at a
  // time with the same scalar op `getVectorComponents` retrieves them for
  // -- exactly the shape glslang's `all`/`any`-style GLSL builtins take
  // over a component-wise vector comparison
  // (`llvm.vector.reduce.and.v4i1(fcmp ole <4 x float> %a, %b)`), confirmed
  // against a real failing `dEQP-VK.mesh_shader.ext.in_out.32_bits_only`
  // case. `getVectorComponents` already handles either a genuinely
  // divergent, decomposed operand or a uniform one transparently, so this
  // widens correctly either way. Unlike every other `widen*` helper here,
  // the *result* is not itself vector-typed (an `llvm.vector.reduce.*`
  // call always returns its vector operand's scalar element type) -- one
  // `<W x T>` lane-wise scalar result, recorded in the ordinary `Widened`
  // map exactly like any other divergent scalar-typed value's widened
  // form, not `WidenedVectorComponents`.
  SmallVector<Value *, 4> Components =
      getVectorComponents(CI.getArgOperand(0), Builder);
  Intrinsic::ID ID = CI.getCalledFunction()->getIntrinsicID();
  Value *Acc = Components[0];
  for (Value *Rhs : ArrayRef(Components).drop_front()) {
    switch (ID) {
    case Intrinsic::vector_reduce_and:
      Acc = Builder.CreateAnd(Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_or:
      Acc = Builder.CreateOr(Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_xor:
      Acc = Builder.CreateXor(Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_add:
      Acc = Builder.CreateAdd(Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_mul:
      Acc = Builder.CreateMul(Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_smax:
      Acc = Builder.CreateBinaryIntrinsic(Intrinsic::smax, Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_smin:
      Acc = Builder.CreateBinaryIntrinsic(Intrinsic::smin, Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_umax:
      Acc = Builder.CreateBinaryIntrinsic(Intrinsic::umax, Acc, Rhs);
      break;
    case Intrinsic::vector_reduce_umin:
      Acc = Builder.CreateBinaryIntrinsic(Intrinsic::umin, Acc, Rhs);
      break;
    default:
      llvm_unreachable(
          "isSupportedVectorReduceIntrinsic accepted an unhandled ID");
    }
  }
  Acc->setName(CI.getName() + ".wide");
  Widened[&CI] = Acc;
  ToErase.push_back(&CI);
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

  // Only compute an eager, flat `<W x T>` widening for a *scalar*-typed
  // operand here: the `BinaryOperator`/`CmpInst`/`CastInst`/`SelectInst`/
  // `UnaryOperator` cases just below are the only ones that consume
  // `WideOps` directly, and every one of them keeps a scalar-typed operand
  // scalar-typed except a same-width vector<->scalar `bitcast` (rare, and
  // still handled correctly since only *its* operand would ever be
  // vector-typed while its own result stays scalar here -- see
  // `widenInstruction`'s `isVectorTy()` gate routing every vector-*result*
  // case to `widenVectorElementwise` before this function ever runs).
  // A vector-typed operand (e.g. a whole `<4 x float>` value operand of a
  // divergently-indexed `store` -- roadmap H6n) is left unwidened here:
  // `getWidened` cannot widen it (a vector is not a valid vector element
  // type, so `getWidened(<4 x float> value)` would build an illegal
  // `<W x <4 x float>>`), and no branch below actually needs it --
  // anything reaching the final `widenScalarizedFallback` call widens its
  // own operands afresh, correctly routing a vector-typed one through
  // `getVectorComponents` instead.
  SmallVector<Value *, 4> WideOps;
  for (Value *Op : I.operands()) {
    WideOps.push_back(Op->getType()->isVectorTy() ? nullptr
                                                   : getWidened(Op, Builder));
  }

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
    if (Function *Callee = CI->getCalledFunction();
        Callee && isSupportedVectorReduceIntrinsic(Callee->getIntrinsicID())) {
      widenVectorReduce(*CI, Builder);
      return true;
    }
    if (Function *Callee = CI->getCalledFunction(); Callee &&
        CI->getType()->isVectorTy() &&
        isElementwiseVectorizableIntrinsic(Callee->getIntrinsicID()) &&
        llvm::all_of(CI->args(), [&](const Value *Arg) {
          return Arg->getType() == CI->getType();
        })) {
      // Roadmap H6g-b-a-i-a-i-b: `llvm.minnum`/`llvm.maxnum`/`llvm.smin`/
      // `llvm.smax`/... over an already-decomposed divergent vector
      // operand (see `widenVectorElementwise`). Gated on `isDivergentAtDef`
      // exactly like every other producer/consumer shape below (roadmap
      // H6m): unlike the resource/image/masked-call shapes above, a
      // vector-typed elementwise-vectorizable intrinsic call has an
      // ordinary, un-widened equivalent when uniform (e.g. `{abs(v.xyz),
      // abs(v.w)}`'s `llvm.fabs.v3f32` over a uniform load, confirmed by
      // reducing a real `dEQP`-adjacent HLSL `abs.32.test` failure down to
      // its exact IR shape) -- widening it unconditionally here, ahead of
      // the general uniformity gate below, erased and replaced a value
      // that its own `extractelement` users, correctly gated on
      // uniformity, left unchanged, leaving them referencing a
      // since-erased (RAUW'd-to-poison) operand.
      if (UI.isDivergentAtDef(CI)) {
        widenVectorElementwise(*CI, Builder);
        return true;
      }
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
    if (isNumWorkgroupsCall(ID)) {
      replaceNumWorkgroupsCall(*CI);
      return true;
    }
    if (isSubgroupIdCall(ID)) {
      replaceSubgroupIdCall(*CI);
      return true;
    }
    if (isNumSubgroupsCall(ID)) {
      replaceNumSubgroupsCall(*CI);
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
    if (isMaskedSetMeshOutputsCall(*CI)) {
      widenMaskedSetMeshOutputs(*CI, Builder);
      return true;
    }
    if (isMaskedEmitMeshTasksCall(*CI)) {
      widenMaskedEmitMeshTasks(*CI, Builder);
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
      case feme::StageOpKind::SetMeshOutputs:
      case feme::StageOpKind::EmitMeshTasks:
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

  if (auto *IV = dyn_cast<InsertValueInst>(&I)) {
    widenInsertValue(*IV, Builder);
    return true;
  }

  if (auto *EV = dyn_cast<ExtractValueInst>(&I)) {
    widenExtractValue(*EV, Builder);
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
       isa<CastInst>(&I) || isa<CmpInst>(&I))) {
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
