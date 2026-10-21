//===- SPIRVPushConstantLowering.h - SPIR-V push constant lowering -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVPushConstantLoweringPass, which lowers
// SPIR-V's push-constant block into loads from the CPU ABI's inline
// root-constant block (see "Root constants" in feme/docs/FeMeCPUDesign.md
// and roadmap step V3's "map Vulkan push constants onto [root constants]").
//
// Unlike DXIL's root constant (a register-bound `dx.CBuffer` read through
// `llvm.dx.resource.load.cbufferrow.4.*`, see RootConstantLowering.h), a
// SPIR-V push-constant block survives `feme::SPIRVToLLVMTranslator` as an
// ordinary LLVM global variable in address space 13 (the push-constant
// address space LLVM's own SPIR-V backend uses -- see
// `feme::spirv::PushConstantGlobalVariablePattern`'s header comment in
// SPIRVToLLVMPatterns.cpp), accessed through plain `load`/`getelementptr`
// instructions rather than a resource-handle intrinsic. There is at most one
// such global per module (SPIR-V permits only one push-constant block), and
// it is never legal to write to it.
//
// Scope: an access is recognized if it is a direct load of the global
// itself, or a load through a chain of `getelementptr`s into it whose
// indices are all compile-time constants (the common "read one
// push-constant member" shape a real shader compiles down to), or --
// roadmap L131 -- the same, except for a *single* index anywhere in that
// chain that is a runtime-computed (non-constant) value, e.g. a
// dynamically-uniform expression a shader uses to index a push-constant
// array, vector, or matrix column, such as
// `dEQP-VK.pipeline.monolithic.push_constant.graphics_pipeline.
// dynamic_index_vert`'s `pc.arrType[dynamicIndex]`. A second, independent
// dynamic index anywhere in the same chain (e.g. a doubly-nested dynamic
// array access), a dynamic *struct member* index (GLSL/SPIR-V never
// generates one -- only array/vector/matrix indices are ever
// runtime-computed), or any other use (a store, a GEP result used as
// anything but a load or a further GEP), is left entirely alone -- the
// whole function is skipped, for `feme::cpu::checkSupportedRaisedOps` to
// reject exactly as if this pass did not exist, rather than partially
// rewritten. This mirrors `feme::cpu::SPIRVResourceLoweringPass::
// hasOnlySupportedUses`'s own "leave it alone rather than partially lower
// it" contract.
//
// roadmap L386: one further shape is recognized -- a `phi` merging two or
// more push-constant pointers that each resolve (by the same rules as
// above) to their own `{BaseOffset, DynamicIndex, DynamicStride}`, e.g. a
// branchy multisample-resolve shader that computes one of two different
// compile-time-constant byte offsets along two different control-flow
// paths and only then merges the resulting pointers before a single
// shared load. Support is intentionally narrower here than the plain-GEP
// case above: every incoming value of a recognized `phi` must itself
// resolve to the push-constant global (a `phi` with even one incoming
// edge that does not is left alone entirely -- see
// `SPIRVPushConstantPhiAccess`'s own comment), and the `phi`'s own result
// may only be used by plain loads -- not a further GEP, and not a further
// (nested) `phi` -- again, anything else leaves the whole function
// untouched rather than partially rewritten.
//
// roadmap H9: the same `phi`-of-pointers shape, but folded one step
// further into a `select` instead -- LLVM's own `InstCombinePass` turns a
// simple, side-effect-free two-predecessor `phi` merge (exactly the shape
// above) into a single-block `select` whenever nothing else in either
// predecessor block needs to stay split, which the upstream
// `feme::graphics::UnrollConstantTripCountStageLoopsPass`'s own
// `InstCombinePass` run (see `feme::cpu::Pipeline.cpp`'s own
// `SPIRVUnmergeResourceLoadsPass` header comment for why that pass exists
// at all) is free to do to a push-constant pointer merge exactly as it
// would to anything else. A `select`'s two arms are recognized by the
// same rule as a `phi`'s incoming edges: both must themselves resolve to
// the push-constant global, and the `select`'s own result may only be
// used by plain loads -- see `SPIRVPushConstantSelectAccess`'s own
// comment.
//
// As with DXIL root constants, a function that also performs bound
// (`spirv.VulkanBuffer` handle) resource access is supported, but lowered
// differently: `feme::cpu::SPIRVResourceLoweringPass` itself finishes the
// job, reusing the `root_constants`/`root_constant_size` parameters it
// already unconditionally appends, rather than this pass adding its own
// (which would collide by name) -- see `matchSPIRVPushConstantAccess`/
// `lowerSPIRVPushConstantAccess`, which both that pass and this one call.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVPUSHCONSTANTLOWERING_H
#define FEME_TRANSFORMS_CPU_SPIRVPUSHCONSTANTLOWERING_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"

#include <cstdint>
#include <optional>

namespace llvm {
class Function;
class GlobalVariable;
class LoadInst;
class PHINode;
class SelectInst;
class Value;
} // namespace llvm

namespace feme::cpu {

/// One recognized load through the module's push-constant global: either a
/// compile-time-constant byte offset alone (`DynamicIndex == nullptr`, the
/// common case), or that same constant `BaseOffset` plus one further
/// runtime-computed term -- `DynamicIndex * DynamicStride` bytes -- roadmap
/// L131's dynamically-indexed case (see the file comment's scope note).
struct SPIRVPushConstantLoad {
  llvm::LoadInst *Load;
  uint64_t BaseOffset = 0;
  llvm::Value *DynamicIndex = nullptr;
  uint64_t DynamicStride = 0;
};

/// One incoming edge of a `phi` recognized by `SPIRVPushConstantPhiAccess`
/// -- which of `Phi->getNumIncomingValues()` this edge is, and that edge's
/// own resolved address relative to the push-constant global, exactly as
/// `SPIRVPushConstantLoad`'s own fields describe a plain load's address
/// (see the file comment's "roadmap L386" scope note).
///
/// This records the incoming *index*, not the predecessor `BasicBlock *`
/// itself: `lowerSPIRVPushConstantAccess` rewrites every plain
/// `SPIRVPushConstantLoad` in `Access.Loads` before it ever reaches
/// `Access.PhiAccesses`, and that earlier rewrite's own
/// `SplitBlockAndInsertIfThenElse` call may split a block this edge's
/// predecessor was recorded against right out from under it, if some
/// unrelated plain load this edge's own predecessor block also happens to
/// contain gets rewritten first -- the original `BasicBlock *` captured at
/// match time would then no longer be `Phi`'s real predecessor by the
/// time this edge's own rewrite runs. `Phi->getIncomingBlock(Index)`
/// always remains accurate (LLVM's own `BasicBlock::splitBasicBlock`
/// keeps every PHI node in a split block's successors, `Phi` included,
/// pointed at its real current predecessor -- see that function's own
/// comment), so re-reading it from `Phi` itself, immediately before this
/// edge's own rewrite, is what this index exists to do.
struct SPIRVPushConstantPhiEdge {
  unsigned Index = 0;
  uint64_t BaseOffset = 0;
  llvm::Value *DynamicIndex = nullptr;
  uint64_t DynamicStride = 0;
};

/// One recognized `phi` merge of two or more differently-addressed
/// push-constant pointers, and every plain load reading through it (see
/// the file comment's "roadmap L386" scope note). `Edges` always has
/// exactly one entry per `Phi->getNumIncomingValues()` -- a `phi` with any
/// incoming edge that does not itself resolve to the push-constant global
/// is not recognized at all, and never appears here (nor is the whole
/// function's access, per the file comment's "leave it alone" contract).
struct SPIRVPushConstantPhiAccess {
  llvm::PHINode *Phi;
  llvm::SmallVector<SPIRVPushConstantPhiEdge, 2> Edges;
  llvm::SmallVector<llvm::LoadInst *, 2> Loads;
};

/// One arm of a `select` recognized by `SPIRVPushConstantSelectAccess` --
/// which arm this is (`true`/condition-taken, or `false`/otherwise), and
/// that arm's own resolved address relative to the push-constant global,
/// exactly as `SPIRVPushConstantLoad`'s own fields describe a plain
/// load's address (see `SPIRVPushConstantSelectAccess`'s own comment).
/// Unlike `SPIRVPushConstantPhiEdge`, there is no predecessor block to
/// re-read later -- a `select` is a pure value-level merge with no
/// control-flow edge of its own, so `IsTrueArm` alone, not an index into
/// a mutable block list, is all a later rewrite ever needs.
struct SPIRVPushConstantSelectEdge {
  bool IsTrueArm = false;
  uint64_t BaseOffset = 0;
  llvm::Value *DynamicIndex = nullptr;
  uint64_t DynamicStride = 0;
};

/// One recognized `select` merge of two differently-addressed
/// push-constant pointers, and every plain load reading through it --
/// roadmap H9's extension of the `phi` case above (`SPIRVPushConstantPhiAccess`)
/// to cover the exact same branchy-shader shape after LLVM's own
/// `InstCombinePass` has further folded a simple two-predecessor `phi`
/// merge into a single-block `select` instead (no side effects on either
/// arm, so there is nothing stopping that fold -- see
/// `feme::cpu::SPIRVPushConstantLoweringPass`'s header comment for a real
/// case, `dEQP-VK.pipeline.*.multisample.misc.multi_renderpass`'s sample
/// mask selection). `Edges` always has exactly one true-arm and one
/// false-arm entry -- a `select` with either arm not itself resolving to
/// the push-constant global is not recognized at all, and never appears
/// here (nor is the whole function's access, per the file comment's
/// "leave it alone" contract).
struct SPIRVPushConstantSelectAccess {
  llvm::SelectInst *Select;
  llvm::SmallVector<SPIRVPushConstantSelectEdge, 2> Edges;
  llvm::SmallVector<llvm::LoadInst *, 2> Loads;
};

/// One function's complete, supported push-constant access: the module's
/// push-constant global, every plain load recognized as reading through
/// it, every `phi`-of-pointers merge recognized as reading through it, and
/// every `select`-of-pointers merge recognized as reading through it (see
/// the file comment for exactly what is recognized).
struct SPIRVPushConstantAccess {
  llvm::GlobalVariable *Global;
  llvm::SmallVector<SPIRVPushConstantLoad, 8> Loads;
  llvm::SmallVector<SPIRVPushConstantPhiAccess, 2> PhiAccesses;
  llvm::SmallVector<SPIRVPushConstantSelectAccess, 2> SelectAccesses;
};

/// Returns \p F's push-constant access if the module has a push-constant
/// global \p F references, and every one of its uses within \p F matches
/// the recognized shape (see the file comment), or `std::nullopt`
/// otherwise (no push-constant global, \p F does not reference it, or one
/// of its uses is unsupported). Performs no mutation.
std::optional<SPIRVPushConstantAccess>
matchSPIRVPushConstantAccess(llvm::Function &F);

/// One function's push-constant access span, as `lowerSPIRVPushConstantAccess`
/// computes it: the lowest and highest byte any recognized load actually
/// reads. `MaxOffset` alone (the historical, pre-roadmap-H6u return value)
/// cannot distinguish a shader that accesses `[0, MaxOffset)` from one that
/// accesses only `[MinOffset, MaxOffset)` -- exactly the shape a SPIR-V
/// push-constant block's own nonzero leading `layout(offset=N)` produces
/// (see `feme::cpu::ResourceInfo::RootConstantMinOffset`'s own comment for
/// why a pipeline-layout coverage check needs both ends, not just one).
struct PushConstantAccessSpan {
  uint32_t MinOffset = 0;
  uint32_t MaxOffset = 0;
};

/// Rewrites every load in \p Access into a bounds-checked load from \p
/// RootConstants (zero for any byte range outside \p RootConstantSize's
/// declared span), and erases every instruction \p Access recognized once
/// unused. Returns the byte span [MinOffset, MaxOffset) any recognized load
/// actually reads -- unlike `feme::cpu::lowerRootConstantAccess`'s own DXIL
/// root constant, which must report its full declared binding size (always
/// starting at byte 0) because a dynamic row/array index means there is no
/// fixed set of bytes to inspect statically, every access this pass
/// recognizes has a compile-time-constant *base* byte offset (see the file
/// comment's scope note) -- a dynamically-indexed load
/// (`SPIRVPushConstantLoad::DynamicIndex != nullptr`) still contributes a
/// precise `MinOffset` (its base offset), but, lacking a statically-known
/// index value, the only sound `MaxOffset` contribution it can make is the
/// end of whichever declared push-constant-struct *member* its own base
/// offset falls within (the tightest bound derivable without reasoning
/// about the dynamic index's own runtime range) -- so the tighter "bytes
/// actually touched" span is always safe to report instead -- and, on a CPU
/// target whose data layout does not mark vectors as element-aligned,
/// `MaxOffset` is frequently
/// *narrower* than the push-constant block's own declared-type
/// `DataLayout` store size (e.g. a trailing `int3`/`float3` member's vector
/// alignment rounds its store size up to the next power of two, inflating
/// the whole struct's reported size well past the last byte any such
/// member's own load actually reaches). Reporting the wider, padding-
/// inflated size here would spuriously require a `VkPushConstantRange` to
/// cover bytes no real access ever touches (roadmap L10). Likewise,
/// `MinOffset` is nonzero exactly when the block's own reflected struct
/// declares a nonzero leading `layout(offset=N)` gap (roadmap H6u) --
/// reporting `0` unconditionally would spuriously require a
/// `VkPushConstantRange` to cover bytes *before* the first one any real
/// access ever touches.
PushConstantAccessSpan
lowerSPIRVPushConstantAccess(const SPIRVPushConstantAccess &Access,
                             llvm::Value *RootConstants,
                             llvm::Value *RootConstantSize);

/// Lowers a SPIR-V push-constant block access into loads from the CPU ABI's
/// root-constant block, for a function with no bound (`spirv.VulkanBuffer`)
/// resource access of its own -- see the file comment for the combined
/// case, handled by `feme::cpu::SPIRVResourceLoweringPass` instead.
class SPIRVPushConstantLoweringPass
    : public llvm::OptionalPassInfoMixin<SPIRVPushConstantLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() {
    return "feme-cpu-lower-spirv-push-constants";
  }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVPUSHCONSTANTLOWERING_H
