//===- ResourceLowering.cpp - CPU target resource canonicalization -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceLowering.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Transforms/CPU/ImageCalls.h"
#include "feme/Transforms/CPU/ResourceCalls.h"
#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/DXILResource.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The two resource kinds this pass canonicalizes, distinguished by the
/// `target("dx.")` handle type's name (see the header comment for the
/// kinds this milestone doesn't yet cover).
enum class Family { Typed, Raw };

/// One `llvm.dx.resource.handlefromheap` call this pass will rewrite,
/// together with everything needed to rewrite its accesses.
struct HandleInfo {
  CallInst *Handle;
  Family Kind;
  /// The stride (in bytes) of a `Raw`-family structured buffer's element, or
  /// 0 for an unstructured `ByteAddressBuffer` (see "Descriptor heaps" in
  /// feme/docs/FeMeCPUDesign.md for the distinction). Unused for `Typed`.
  uint64_t Stride = 0;
};

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// Checks that every use of \p Handle is a typed- or raw-buffer access this
/// pass knows how to rewrite (see `hasOnlySupportedUses` in
/// feme::amdgpu::ResourceLoweringPass for the analogous check on the
/// register-bound side): a load's result must only be consumed by
/// `extractvalue`, since the raised intrinsic returns a `{value, checkbit}`
/// pair with no canonical-call counterpart of its own (the canonical load
/// always succeeds, reporting an out-of-bounds access as a zero result
/// instead -- see "Bounds checking"); a store's resource operand must be
/// \p Handle itself, not its stored value.

/// Returns whether \p RMW is a shape `lowerAtomicAccess` below knows how to
/// rewrite into a canonical `feme.cpu.resource.atomic.*` call: a scalar
/// `i32` value directly against \p Ptr (never a further-indirected
/// pointer), using one of the opcodes every `createAtomicXxxRaw`/
/// `createAtomicXxxTyped` builder pair covers -- mirrors
/// `hasOnlySupportedPointerUses`'s identical `AtomicRMWInst` check in
/// `SPIRVResourceLowering.cpp` (this file's SPIR-V-origin counterpart,
/// whose already-proven atomic support this reuses almost verbatim; see
/// that file's own doc for why no ordering/syncscope check is needed --
/// the CPU runtime's own locking discipline does not distinguish them).
bool isSupportedAtomicRMW(const AtomicRMWInst &RMW, const Value &Ptr) {
  if (RMW.getPointerOperand() != &Ptr ||
      !RMW.getValOperand()->getType()->isIntegerTy(32))
    return false;
  switch (RMW.getOperation()) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin:
  case AtomicRMWInst::Xchg:
    return true;
  default:
    return false;
  }
}

/// Returns whether \p CmpXchg is a shape `lowerAtomicAccess` below knows
/// how to rewrite into a canonical `feme.cpu.resource.atomic.
/// compare_exchange.*` call: a scalar `i32` comparator directly against
/// \p Ptr, whose result is read only through a single `extractvalue ...,
/// 0` picking out the original value -- `raiseAtomicCompareExchange`
/// (OpRaising.cpp) already synthesizes exactly that one extract for every
/// raised `dx.op.atomicCompareExchange` call, so this never needs to
/// reject any other shape in practice, but the check stays defensive
/// (mirroring `hasOnlySupportedPointerUses`'s identical `AtomicCmpXchgInst`
/// check in `SPIRVResourceLowering.cpp`) in case a future IR producer
/// constructs this same `getpointer`+`cmpxchg` shape some other way.
bool isSupportedAtomicCmpXchg(const AtomicCmpXchgInst &CmpXchg,
                              const Value &Ptr) {
  if (CmpXchg.getPointerOperand() != &Ptr ||
      !CmpXchg.getCompareOperand()->getType()->isIntegerTy(32))
    return false;
  for (const User *U : CmpXchg.users()) {
    const auto *EV = dyn_cast<ExtractValueInst>(U);
    if (!EV || EV->getIndices() != ArrayRef<unsigned>{0})
      return false;
  }
  return true;
}

/// Returns whether every use of a raised `llvm.dx.resource.getpointer` call
/// \p GetPtr (an atomic access's own address, see `raiseAtomicBinOp` in
/// OpRaising.cpp) is a shape `lowerAtomicAccess` below can rewrite: either
/// a supported `atomicrmw`/`cmpxchg` directly on \p GetPtr's result (the
/// common, scalar-element-type case -- a `RWStructuredBuffer<uint>`/
/// `RWByteAddressBuffer`'s whole-element `InterlockedAdd`/
/// `InterlockedCompareExchange`), or a single byte-level `getelementptr`
/// off it (a genuine `StructuredBuffer`'s own intra-element byte offset,
/// see `raiseAtomicBinOp`'s own doc) whose sole user is, in turn, a
/// supported `atomicrmw`/`cmpxchg`.
bool hasOnlySupportedAtomicUses(const CallInst &GetPtr) {
  auto IsSupportedAtomic = [](const User &U, const Value &Ptr) {
    if (const auto *RMW = dyn_cast<AtomicRMWInst>(&U))
      return isSupportedAtomicRMW(*RMW, Ptr);
    if (const auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&U))
      return isSupportedAtomicCmpXchg(*CmpXchg, Ptr);
    return false;
  };

  for (const User *U : GetPtr.users()) {
    if (isa<AtomicRMWInst>(U) || isa<AtomicCmpXchgInst>(U)) {
      if (!IsSupportedAtomic(*U, GetPtr))
        return false;
      continue;
    }
    const auto *GEP = dyn_cast<GetElementPtrInst>(U);
    if (!GEP || GEP->getPointerOperand() != &GetPtr ||
        !GEP->getSourceElementType()->isIntegerTy(8) ||
        GEP->getNumIndices() != 1 || !GEP->hasOneUser())
      return false;
    const User *GEPUser = *GEP->user_begin();
    if (!isa<AtomicRMWInst>(GEPUser) && !isa<AtomicCmpXchgInst>(GEPUser))
      return false;
    if (!IsSupportedAtomic(*GEPUser, *GEP))
      return false;
  }
  return true;
}

bool hasOnlySupportedUses(const CallInst &Handle, Family Kind) {
  Intrinsic::ID LoadID = Kind == Family::Typed
                             ? Intrinsic::dx_resource_load_typedbuffer
                             : Intrinsic::dx_resource_load_rawbuffer;
  Intrinsic::ID StoreID = Kind == Family::Typed
                              ? Intrinsic::dx_resource_store_typedbuffer
                              : Intrinsic::dx_resource_store_rawbuffer;

  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;
    Intrinsic::ID ID = getIntrinsicID(CI);
    if (ID == StoreID) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      continue;
    }
    if (ID == Intrinsic::dx_resource_getpointer) {
      if (CI->getArgOperand(0) != &Handle || !hasOnlySupportedAtomicUses(*CI))
        return false;
      continue;
    }
    if (ID != LoadID)
      return false;
    for (const User *LoadUser : CI->users())
      if (!isa<ExtractValueInst>(LoadUser))
        return false;
  }
  return true;
}

/// Classifies \p Handle's resource kind from its `target("dx.")` handle
/// type, returning `std::nullopt` for a kind this pass doesn't canonicalize
/// (see the header comment): constant buffers, and anything
/// `feme::dxil::OpRaisingPass` doesn't reconstruct a `handlefromheap` for in
/// the first place (textures, samplers).
std::optional<HandleInfo> classifyHandle(CallInst &Handle,
                                         const DataLayout &DL) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy)
    return std::nullopt;

  if (HandleTy->getName() == "dx.TypedBuffer") {
    if (!hasOnlySupportedUses(Handle, Family::Typed))
      return std::nullopt;
    return HandleInfo{&Handle, Family::Typed, /*Stride=*/0};
  }

  if (HandleTy->getName() == "dx.RawBuffer") {
    if (!hasOnlySupportedUses(Handle, Family::Raw))
      return std::nullopt;
    // An unstructured `ByteAddressBuffer` always carries a literal `i8`
    // element type parameter (see `raiseResourceHandleFromHeap` in
    // OpRaising.cpp); a `StructuredBuffer`'s is instead an opaque
    // size/alignment placeholder whose store size is never 1 byte as a
    // scalar `i8` (it is at minimum a single-element `[1 x i8]` array, a
    // distinct type) -- so the two are unambiguous to tell apart.
    Type *ElemTy = HandleTy->getTypeParameter(0);
    uint64_t Stride = ElemTy->isIntegerTy(8) ? 0 : DL.getTypeStoreSize(ElemTy);
    return HandleInfo{&Handle, Family::Raw, Stride};
  }

  return std::nullopt; // Constant buffer, texture, sampler: not yet covered.
}

/// Returns whether \p HandleTy is one of the texture/sampler handle kinds
/// `feme::dxil::OpRaisingPass` reconstructs (see Design.md's "Decision:
/// texture and sampler handle kinds") but this file's buffer-oriented
/// `classifyHandle` does not classify: these are not "unsupported" in the
/// sense that bails a function's buffer lowering entirely, since
/// `lowerImageAccesses` (roadmap R30) handles their accesses in a separate
/// pass over the same function.
bool isImageOrSamplerHandleType(const TargetExtType &HandleTy) {
  StringRef Name = HandleTy.getName();
  return Name == "dx.Texture" || Name == "dx.MSTexture" ||
         Name == "dx.FeedbackTexture" || Name == "dx.Sampler";
}

/// Collects every `handlefromheap` call \p F contains that this pass can
/// rewrite, or `std::nullopt` if any of them uses a resource kind or access
/// pattern it cannot model -- in which case \p F is left entirely
/// unmodified rather than partially rewritten (see the header comment).
/// Texture/sampler handles are skipped here (neither collected nor cause a
/// bail): they are `lowerImageAccesses`'s concern, not this buffer-oriented
/// collection's.
std::optional<SmallVector<HandleInfo, 4>> collectHandles(Function &F) {
  const DataLayout &DL = F.getDataLayout();
  SmallVector<HandleInfo, 4> Handles;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefromheap)
      continue;
    if (auto *HandleTy = dyn_cast<TargetExtType>(CI->getType()))
      if (isImageOrSamplerHandleType(*HandleTy))
        continue;
    std::optional<HandleInfo> Info = classifyHandle(*CI, DL);
    if (!Info)
      return std::nullopt;
    Handles.push_back(*Info);
  }
  return Handles;
}

/// Builds \p F's replacement: the same function with the eight trailing
/// resource/root-constant/image ABI parameters "Lowering" describes
/// appended (roadmap R30 added the trailing `image_heap`/
/// `image_heap_count` pair to the original six), in that order. The body
/// is moved across (not cloned), so every instruction -- including the
/// handles `collectHandles` already found -- stays valid and belongs to
/// the new function afterwards, exactly as
/// feme::amdgpu::ResourceLoweringPass's `addBindingArguments` does for its
/// own (differently-shaped) parameter list.
Function *addResourceEnvParams(Function &F, ResourceCallEnv &Env) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, I32Ty, PtrTy, I32Ty, PtrTy, I32Ty, PtrTy, I32Ty});

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  // GlobalObject::copyAttributesFrom() does not copy function-attached
  // metadata (e.g. !feme.signature attached by CanonicalizeStagePass); do
  // that explicitly so stage reflection metadata survives resource-env
  // parameter injection. See the identical fix in SPIRVResourceLowering.cpp
  // (roadmap H3a).
  NewF->copyMetadata(&F, /*Offset=*/0);
  NewF->setComdat(F.getComdat());
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : llvm::zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = NewF->arg_begin() + F.arg_size();
  Env.ResourceHeap = &*ArgIt++;
  Env.ResourceHeap->setName("resource_heap");
  Env.ResourceHeapCount = &*ArgIt++;
  Env.ResourceHeapCount->setName("resource_heap_count");
  Env.SamplerHeap = &*ArgIt++;
  Env.SamplerHeap->setName("sampler_heap");
  Env.SamplerHeapCount = &*ArgIt++;
  Env.SamplerHeapCount->setName("sampler_heap_count");
  Env.RootConstants = &*ArgIt++;
  Env.RootConstants->setName("root_constants");
  Env.RootConstantSize = &*ArgIt++;
  Env.RootConstantSize->setName("root_constant_size");
  Env.ImageHeap = &*ArgIt++;
  Env.ImageHeap->setName("image_heap");
  Env.ImageHeapCount = &*ArgIt++;
  Env.ImageHeapCount->setName("image_heap_count");

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

/// Replaces every `extractvalue` reading the raised load's `{value,
/// checkbit}` result with \p Loaded (field 0) or `true` (field 1): the
/// canonical call has no checkbit of its own because it never fails to
/// produce a result -- an out-of-bounds access reads as zero instead (see
/// "Bounds checking" in feme/docs/FeMeCPUDesign.md).
void replaceLoadResultUses(CallInst &Access, Value *Loaded) {
  for (User *U : llvm::make_early_inc_range(Access.users())) {
    auto *EV = cast<ExtractValueInst>(U);
    Value *Replacement =
        EV->getIndices()[0] == 0
            ? Loaded
            : cast<Value>(ConstantInt::getTrue(EV->getContext()));
    EV->replaceAllUsesWith(Replacement);
    EV->eraseFromParent();
  }
}

/// Builds the canonical `feme.cpu.resource.atomic.*` call matching \p Op,
/// dispatching to the `Raw` or `Typed` builder pair (roadmap H8x/H8w
/// respectively) based on \p Kind -- every `createAtomicXxxRaw`/
/// `createAtomicXxxTyped` builder shares the same
/// `(Builder, Env, DescriptorIndex, Offset, Val, Mask, Name)` signature, so
/// only the callee differs between the two families. Mirrors
/// `lowerRawPointerUses`'s identical `AtomicRMWInst` switch in
/// `SPIRVResourceLowering.cpp`.
CallInst *createAtomicCall(IRBuilderBase &Builder, const ResourceCallEnv &Env,
                           Value *DescriptorIndex, Value *Offset, Value *Val,
                           Value *Mask, AtomicRMWInst::BinOp Op, Family Kind,
                           const Twine &Name) {
  bool IsTyped = Kind == Family::Typed;
  switch (Op) {
  case AtomicRMWInst::Add:
    return IsTyped ? createAtomicAddTyped(Builder, Env, DescriptorIndex,
                                         Offset, Val, Mask, Name)
                  : createAtomicAddRaw(Builder, Env, DescriptorIndex, Offset,
                                       Val, Mask, Name);
  case AtomicRMWInst::Sub:
    return IsTyped ? createAtomicSubTyped(Builder, Env, DescriptorIndex,
                                         Offset, Val, Mask, Name)
                  : createAtomicSubRaw(Builder, Env, DescriptorIndex, Offset,
                                       Val, Mask, Name);
  case AtomicRMWInst::And:
    return IsTyped ? createAtomicAndTyped(Builder, Env, DescriptorIndex,
                                         Offset, Val, Mask, Name)
                  : createAtomicAndRaw(Builder, Env, DescriptorIndex, Offset,
                                       Val, Mask, Name);
  case AtomicRMWInst::Or:
    return IsTyped ? createAtomicOrTyped(Builder, Env, DescriptorIndex,
                                        Offset, Val, Mask, Name)
                  : createAtomicOrRaw(Builder, Env, DescriptorIndex, Offset,
                                      Val, Mask, Name);
  case AtomicRMWInst::Xor:
    return IsTyped ? createAtomicXorTyped(Builder, Env, DescriptorIndex,
                                         Offset, Val, Mask, Name)
                  : createAtomicXorRaw(Builder, Env, DescriptorIndex, Offset,
                                       Val, Mask, Name);
  case AtomicRMWInst::Max:
    return IsTyped ? createAtomicSMaxTyped(Builder, Env, DescriptorIndex,
                                          Offset, Val, Mask, Name)
                  : createAtomicSMaxRaw(Builder, Env, DescriptorIndex, Offset,
                                        Val, Mask, Name);
  case AtomicRMWInst::Min:
    return IsTyped ? createAtomicSMinTyped(Builder, Env, DescriptorIndex,
                                          Offset, Val, Mask, Name)
                  : createAtomicSMinRaw(Builder, Env, DescriptorIndex, Offset,
                                        Val, Mask, Name);
  case AtomicRMWInst::UMax:
    return IsTyped ? createAtomicUMaxTyped(Builder, Env, DescriptorIndex,
                                          Offset, Val, Mask, Name)
                  : createAtomicUMaxRaw(Builder, Env, DescriptorIndex, Offset,
                                        Val, Mask, Name);
  case AtomicRMWInst::UMin:
    return IsTyped ? createAtomicUMinTyped(Builder, Env, DescriptorIndex,
                                          Offset, Val, Mask, Name)
                  : createAtomicUMinRaw(Builder, Env, DescriptorIndex, Offset,
                                        Val, Mask, Name);
  case AtomicRMWInst::Xchg:
    return IsTyped ? createAtomicExchangeTyped(Builder, Env, DescriptorIndex,
                                              Offset, Val, Mask, Name)
                  : createAtomicExchangeRaw(Builder, Env, DescriptorIndex,
                                            Offset, Val, Mask, Name);
  default:
    llvm_unreachable(
        "hasOnlySupportedAtomicUses only accepts the RMW kinds handled "
        "above");
  }
}

/// Builds the canonical `feme.cpu.resource.atomic.compare_exchange.*` call,
/// dispatching to the `Raw` or `Typed` builder based on \p Kind -- the
/// `cmpxchg` counterpart to `createAtomicCall` above.
CallInst *createAtomicCompareExchangeCall(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *Offset, Value *Comparator,
                                          Value *Val, Value *Mask,
                                          Family Kind, const Twine &Name) {
  return Kind == Family::Typed
             ? createAtomicCompareExchangeTyped(Builder, Env, DescriptorIndex,
                                               Offset, Comparator, Val, Mask,
                                               Name)
             : createAtomicCompareExchangeRaw(Builder, Env, DescriptorIndex,
                                             Offset, Comparator, Val, Mask,
                                             Name);
}

/// Rewrites every `atomicrmw`/`cmpxchg` reachable from a raised
/// `llvm.dx.resource.getpointer` call \p GetPtr (an atomic access's own
/// address, see `raiseAtomicBinOp`/`raiseAtomicCompareExchange` in
/// OpRaising.cpp) into the corresponding canonical `feme.cpu.resource.
/// atomic.*` call, erasing \p GetPtr (and any intervening byte-offset
/// `getelementptr`, see`hasOnlySupportedAtomicUses`'s own doc) once every
/// use is rewritten. \p ElementIndex is \p GetPtr's own index argument,
/// already converted to \p Info.Kind's own addressing convention (a
/// `Typed` element index, or a `Raw` byte offset -- see `lowerAccesses`'s
/// own `Offset` computation, whose result this reuses unchanged for the
/// non-struct common case).
void lowerAtomicAccess(CallInst &GetPtr, const HandleInfo &Info,
                      const ResourceCallEnv &Env, Value *DescriptorIndex,
                      Value *ElementIndex) {
  LLVMContext &Ctx = GetPtr.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *Mask = ConstantInt::getTrue(Ctx);

  auto RewriteRMW = [&](AtomicRMWInst &RMW, Value *Offset) {
    IRBuilder<> Builder(&RMW);
    CallInst *Old =
        createAtomicCall(Builder, Env, DescriptorIndex, Offset,
                         RMW.getValOperand(), Mask, RMW.getOperation(),
                         Info.Kind, RMW.getName());
    RMW.replaceAllUsesWith(Old);
    RMW.eraseFromParent();
  };

  // `isSupportedAtomicCmpXchg` already guaranteed every user of `CmpXchg`
  // is an `extractvalue ..., 0` picking out the original value -- replace
  // each with the new call directly, since the call's own result *is*
  // that original value (unlike `llvm.cmpxchg`, no `{i32, i1}` struct to
  // extract from). Mirrors `lowerRawPointerUses`'s identical
  // `AtomicCmpXchgInst` handling in `SPIRVResourceLowering.cpp`.
  auto RewriteCmpXchg = [&](AtomicCmpXchgInst &CmpXchg, Value *Offset) {
    IRBuilder<> Builder(&CmpXchg);
    CallInst *Old = createAtomicCompareExchangeCall(
        Builder, Env, DescriptorIndex, Offset, CmpXchg.getCompareOperand(),
        CmpXchg.getNewValOperand(), Mask, Info.Kind, CmpXchg.getName());
    for (User *EU : llvm::make_early_inc_range(CmpXchg.users())) {
      auto *EV = cast<ExtractValueInst>(EU);
      EV->replaceAllUsesWith(Old);
      EV->eraseFromParent();
    }
    CmpXchg.eraseFromParent();
  };

  // Computes the byte-offset-adjusted `Offset` for a `getpointer` user
  // reached through an intervening byte-offset `getelementptr` \p GEP (a
  // genuine `StructuredBuffer`'s own intra-element offset), building the
  // arithmetic right before \p AtomicUser so the inserted instructions
  // dominate it regardless of which lambda below consumes the result.
  auto OffsetForGEP = [&](GetElementPtrInst &GEP, Instruction &AtomicUser) {
    IRBuilder<> OffsetBuilder(&AtomicUser);
    Value *ByteOffset = OffsetBuilder.CreateZExt(GEP.getOperand(1), I64Ty);
    return OffsetBuilder.CreateAdd(ElementIndex, ByteOffset);
  };

  for (User *U : llvm::make_early_inc_range(GetPtr.users())) {
    if (auto *RMW = dyn_cast<AtomicRMWInst>(U)) {
      RewriteRMW(*RMW, ElementIndex);
      continue;
    }
    if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(U)) {
      RewriteCmpXchg(*CmpXchg, ElementIndex);
      continue;
    }
    auto *GEP = cast<GetElementPtrInst>(U);
    User *GEPUser = *GEP->user_begin();
    if (auto *RMW = dyn_cast<AtomicRMWInst>(GEPUser)) {
      RewriteRMW(*RMW, OffsetForGEP(*GEP, *RMW));
    } else {
      auto *CmpXchg = cast<AtomicCmpXchgInst>(GEPUser);
      RewriteCmpXchg(*CmpXchg, OffsetForGEP(*GEP, *CmpXchg));
    }
    GEP->eraseFromParent();
  }
  GetPtr.eraseFromParent();
}

/// Rewrites every access through \p Info.Handle into the corresponding
/// canonical `feme.cpu.resource.*` call, using \p Env and \p DescriptorIndex
/// (the heap index the handle was created from).
void lowerAccesses(const HandleInfo &Info, const ResourceCallEnv &Env,
                   Value *DescriptorIndex) {
  LLVMContext &Ctx = Info.Handle->getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *Mask = ConstantInt::getTrue(Ctx);

  for (User *U : llvm::make_early_inc_range(Info.Handle->users())) {
    auto *Access = cast<CallInst>(U);
    IRBuilder<> Builder(Access);
    Intrinsic::ID ID = getIntrinsicID(Access);
    bool IsStore = ID == Intrinsic::dx_resource_store_typedbuffer ||
                   ID == Intrinsic::dx_resource_store_rawbuffer;

    if (ID == Intrinsic::dx_resource_getpointer) {
      // `getpointer` carries a single index argument (no separate
      // sub-element byte-offset operand the way `load`/`store`'s raw-
      // buffer calls do -- see `raiseAtomicBinOp`'s own doc for why that
      // offset, if any, instead arrives as a `getelementptr`
      // `lowerAtomicAccess` itself resolves): a `Typed` buffer's is
      // already an element index, and a `Raw` buffer's is already an
      // element index needing the same `Stride` multiplication an
      // ordinary access's `ElemIdx` does below (or already a byte address
      // for an unstructured `ByteAddressBuffer`, `Info.Stride == 0`).
      Value *ElemIdx = Builder.CreateZExt(Access->getArgOperand(1), I64Ty);
      Value *ElementIndex =
          Info.Kind == Family::Typed || Info.Stride == 0
              ? ElemIdx
              : Builder.CreateMul(ElemIdx, ConstantInt::get(I64Ty, Info.Stride));
      lowerAtomicAccess(*Access, Info, Env, DescriptorIndex, ElementIndex);
      continue;
    }

    Value *Offset;
    if (Info.Kind == Family::Typed) {
      // The element index is the canonical typed call's own operand; no
      // byte-offset arithmetic is needed (see the literal example in
      // "Lowering").
      Offset = Builder.CreateZExt(Access->getArgOperand(1), I64Ty);
    } else if (Info.Stride == 0) {
      // An unstructured `ByteAddressBuffer`'s index operand is already a
      // byte address.
      Offset = Builder.CreateZExt(Access->getArgOperand(1), I64Ty);
    } else {
      // A `StructuredBuffer` access's two index operands are an element
      // index and a byte offset within that element (see
      // `lowerRawBufferLoad` in DXILOpLowering.cpp, which reads the same
      // pair the other way for codegen).
      Value *ElemIdx = Builder.CreateZExt(Access->getArgOperand(1), I64Ty);
      Value *SubOffset = Builder.CreateZExt(Access->getArgOperand(2), I64Ty);
      Value *StrideConst = ConstantInt::get(I64Ty, Info.Stride);
      Offset =
          Builder.CreateAdd(Builder.CreateMul(ElemIdx, StrideConst), SubOffset);
    }

    if (IsStore) {
      Value *StoredValue = Access->getArgOperand(Access->arg_size() - 1);
      if (Info.Kind == Family::Typed)
        createTypedStore(Builder, Env, DescriptorIndex, Offset, StoredValue,
                         Mask);
      else
        createRawStore(Builder, Env, DescriptorIndex, Offset, StoredValue,
                       Mask);
      Access->eraseFromParent();
      continue;
    }

    Type *ElemTy = cast<StructType>(Access->getType())->getElementType(0);
    CallInst *Loaded =
        Info.Kind == Family::Typed
            ? createTypedLoad(Builder, Env, DescriptorIndex, Offset, Mask,
                              ElemTy, Access->getName())
            : createRawLoad(Builder, Env, DescriptorIndex, Offset, Mask, ElemTy,
                            Access->getName());
    replaceLoadResultUses(*Access, Loaded);
    Access->eraseFromParent();
  }
  Info.Handle->eraseFromParent();
}


/// Which of the four sampled-image shapes `classifyImageHandle` recognizes
/// (roadmap H7b-a widened it beyond plain, non-arrayed `Texture2D` --
/// mirrors `feme::spirv`'s own `ImageShape` on the SPIR-V side of this same
/// milestone). A cube(array) is purely a view-level addressing convention
/// over an ordinary 2D-array-shaped image, never a distinct physical layout
/// of its own (see FeMeVulkanDesign.md's H7b update).
enum class ImageShape {
  /// A plain, non-arrayed `Texture2D`: `(U, V)` sample/load coordinates.
  Plain2D,
  /// A `Texture2DArray`: `(U, V, ArrayLayer)` sample, `(X, Y, Layer)` load.
  Array2D,
  /// A `TextureCube`: a 3-component direction-vector sample coordinate; no
  /// `Load` (`TextureCube`/`TextureCubeArray` have no `Load` method in
  /// HLSL/DXIL, mirroring SPIR-V's identical `OpImageFetch` restriction).
  Cube,
  /// A `TextureCubeArray`: a 3-component direction vector plus a float
  /// array-layer sample coordinate; no `Load`, for the same reason as Cube.
  CubeArray,
};

/// A `dx.Texture` handle originating from a `handlefromheap` call, for one
/// of the four dimensions `runtime/CPU` implements sampling/loading for
/// (see the file header comment's scope note).
struct ImageHandle {
  CallInst *HandleFromHeap;
  ImageShape Shape;
};

/// Recognizes \p V as a `Texture2D`/`Texture2DArray`/`TextureCube`/
/// `TextureCubeArray` `dx.Texture` handle from the descriptor heap, or
/// returns `std::nullopt` for any other shape (a different dimension, a
/// register-bound handle, or not a handle at all) -- left unraised rather
/// than erroring, exactly like every other not-yet-covered shape in this
/// file.
std::optional<ImageHandle> classifyImageHandle(Value *V) {
  auto *CI = dyn_cast<CallInst>(V);
  if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefromheap)
    return std::nullopt;
  auto *Ty = dyn_cast<dxil::TextureExtType>(CI->getType());
  if (!Ty)
    return std::nullopt;
  switch (Ty->getDimension()) {
  case dxil::ResourceKind::Texture2D:
    return ImageHandle{CI, ImageShape::Plain2D};
  case dxil::ResourceKind::Texture2DArray:
    return ImageHandle{CI, ImageShape::Array2D};
  case dxil::ResourceKind::TextureCube:
    return ImageHandle{CI, ImageShape::Cube};
  case dxil::ResourceKind::TextureCubeArray:
    return ImageHandle{CI, ImageShape::CubeArray};
  default:
    return std::nullopt;
  }
}

/// Recognizes \p V as a `dx.Sampler` handle from the descriptor heap.
std::optional<CallInst *> classifySamplerHandle(Value *V) {
  auto *CI = dyn_cast<CallInst>(V);
  if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefromheap)
    return std::nullopt;
  if (!isa<dxil::SamplerExtType>(CI->getType()))
    return std::nullopt;
  return CI;
}

/// Tests the zero-offset gate for the remaining ordinary 2D/cube sampling
/// path. Unsupported offsets are left untouched rather than dropped.
bool isZeroOffset(const Value *Offset) {
  const auto *C = dyn_cast<Constant>(Offset);
  return C && C->isNullValue();
}

unsigned getTexelWidth(Type *Ty, Type *ScalarTy) {
  if (Ty == ScalarTy)
    return 1;
  auto *VecTy = dyn_cast<FixedVectorType>(Ty);
  return VecTy && VecTy->getElementType() == ScalarTy &&
                 VecTy->getNumElements() <= 4
             ? VecTy->getNumElements()
             : 0;
}

Value *narrowImageResult(IRBuilder<> &Builder, Value *Loaded, Type *ResultTy) {
  if (Loaded->getType() == ResultTy)
    return Loaded;
  if (auto *VecTy = dyn_cast<FixedVectorType>(ResultTy)) {
    SmallVector<int, 4> Components;
    for (unsigned I = 0; I < VecTy->getNumElements(); ++I)
      Components.push_back(I);
    return Builder.CreateShuffleVector(Loaded, Components);
  }
  return Builder.CreateExtractElement(Loaded, uint64_t{0});
}

/// Lowers supported `llvm.dx.resource.sample`/`samplelevel`/
/// `samplebias[.clamp]`/`samplegrad[.clamp]`/`samplecmp[.clamp]`/
/// `samplecmplevelzero`/`load.level`
/// calls in
/// \p F whose handle operand(s) `classifyImageHandle`/`classifySamplerHandle`
/// recognize into the corresponding canonical `feme.cpu.image.*` call (see
/// ImageCalls.h), using \p Env's image/sampler heap operands. An access this
/// function cannot model (an unsupported dimension/offset or an unrecognized
/// handle) is left unraised, independent
/// of every other access in \p F -- unlike the buffer path, this is safe on
/// a per-access basis because an unlowered `llvm.dx.resource.*` call remains
/// valid IR on its own (it is simply not retargetable to the CPU target
/// until a later increment covers it), rather than half of a handle's
/// worth of accesses being rewritten out from under the other half.
bool lowerImageAccesses(Function &F, const ImageCallEnv &Env,
                        bool &UsesSamplerHeap) {
  bool Changed = false;
  SmallPtrSet<CallInst *, 4> SampleHandles;
  for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    Intrinsic::ID ID = getIntrinsicID(CI);
    if (isSupportedDXILLoad(*CI)) {
      IRBuilder<> Builder(CI);
      auto *Image = cast<CallInst>(CI->getArgOperand(0));
      Value *Coord = CI->getArgOperand(1);
      Value *Offset = CI->getArgOperand(3);
      auto Kind = cast<dxil::TextureExtType>(Image->getType())->getDimension();
      bool IsInteger = CI->getType()->getScalarType()->isIntegerTy(32);
      Value *Loaded;
      if (Kind == dxil::ResourceKind::Texture3D) {
        Value *XYZ[3];
        for (unsigned Axis = 0; Axis < 3; ++Axis)
          XYZ[Axis] = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{Axis}),
              cast<Constant>(Offset)->getAggregateElement(Axis));
        Loaded =
            IsInteger
                ? createLoad3DI32(Builder, Env, Image->getArgOperand(0), XYZ[0],
                                  XYZ[1], XYZ[2], CI->getArgOperand(2),
                                  Builder.getTrue(), CI->getName())
                : createLoad3D(Builder, Env, Image->getArgOperand(0), XYZ[0],
                               XYZ[1], XYZ[2], CI->getArgOperand(2),
                               Builder.getInt32(0), Builder.getTrue(),
                               CI->getName());
      } else if (Kind == dxil::ResourceKind::Texture2D ||
                 Kind == dxil::ResourceKind::Texture2DArray) {
        Value *X =
            Builder.CreateAdd(Builder.CreateExtractElement(Coord, uint64_t{0}),
                              cast<Constant>(Offset)->getAggregateElement(0u));
        Value *Y =
            Builder.CreateAdd(Builder.CreateExtractElement(Coord, uint64_t{1}),
                              cast<Constant>(Offset)->getAggregateElement(1u));
        if (Kind == dxil::ResourceKind::Texture2DArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{2});
          Loaded = createLoad2DArray(Builder, Env, Image->getArgOperand(0), X,
                                     Y, Layer, CI->getArgOperand(2),
                                     Builder.getInt32(0), Builder.getTrue(),
                                     CI->getName());
        } else {
          Loaded = createLoad2D(Builder, Env, Image->getArgOperand(0), X, Y,
                                CI->getArgOperand(2), Builder.getInt32(0),
                                Builder.getTrue(), CI->getName());
        }
      } else {
        Value *X = Coord->getType()->isIntegerTy(32)
                       ? Coord
                       : Builder.CreateExtractElement(Coord, uint64_t{0});
        X = Builder.CreateAdd(X, Offset);
        if (Kind == dxil::ResourceKind::Texture1DArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{1});
          Loaded =
              IsInteger
                  ? createLoad1DArrayI32(Builder, Env, Image->getArgOperand(0),
                                         X, Layer, CI->getArgOperand(2),
                                         Builder.getTrue(), CI->getName())
                  : createLoad1DArray(Builder, Env, Image->getArgOperand(0), X,
                                      Layer, CI->getArgOperand(2),
                                      Builder.getInt32(0), Builder.getTrue(),
                                      CI->getName());
        } else {
          Loaded = IsInteger
                       ? createLoad1DI32(Builder, Env, Image->getArgOperand(0),
                                         X, CI->getArgOperand(2),
                                         Builder.getTrue(), CI->getName())
                       : createLoad1D(Builder, Env, Image->getArgOperand(0), X,
                                      CI->getArgOperand(2), Builder.getInt32(0),
                                      Builder.getTrue(), CI->getName());
        }
      }
      CI->replaceAllUsesWith(narrowImageResult(Builder, Loaded, CI->getType()));
      CI->eraseFromParent();
      SampleHandles.insert(Image);
      Changed = true;
      continue;
    }
    bool IsGrad = ID == Intrinsic::dx_resource_samplegrad ||
                  ID == Intrinsic::dx_resource_samplegrad_clamp;
    bool IsLevel = isSupportedDXILSampleLevel(*CI);
    bool IsImplicit = isSupportedDXILSample(*CI);
    if (ID == Intrinsic::dx_resource_samplebias ||
        ID == Intrinsic::dx_resource_samplebias_clamp || IsGrad || IsLevel ||
        IsImplicit) {
      if (!IsLevel && !IsImplicit &&
          !(IsGrad ? isSupportedDXILSampleGrad(*CI)
                   : isSupportedDXILSampleBias(*CI)))
        continue;
      IRBuilder<> Builder(CI);
      auto *Image = cast<CallInst>(CI->getArgOperand(0));
      auto *Sampler = cast<CallInst>(CI->getArgOperand(1));
      Value *Coord = CI->getArgOperand(2);
      Value *U = Coord->getType()->isFloatTy()
                     ? Coord
                     : Builder.CreateExtractElement(Coord, uint64_t{0});
      unsigned OffsetIdx = IsImplicit ? 3 : IsGrad ? 5 : 4;
      Value *Offset = CI->getArgOperand(OffsetIdx);
      bool HasClamp = ID == Intrinsic::dx_resource_samplebias_clamp ||
                      ID == Intrinsic::dx_resource_samplegrad_clamp;
      Value *MinLodClamp = HasClamp
                               ? CI->getArgOperand(OffsetIdx + 1)
                               : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                         /*Negative=*/true);
      Value *Zero = ConstantFP::get(Builder.getFloatTy(), 0.0);
      Value *Bias =
          IsGrad || IsLevel || IsImplicit ? Zero : CI->getArgOperand(3);
      Value *Lod = IsLevel ? CI->getArgOperand(3) : Zero;
      auto Kind = cast<dxil::TextureExtType>(Image->getType())->getDimension();
      Value *Loaded;
      if (Kind == dxil::ResourceKind::Texture1D ||
          Kind == dxil::ResourceKind::Texture1DArray) {
        SampleDerivatives1D D =
            IsGrad    ? SampleDerivatives1D{CI->getArgOperand(3),
                                            CI->getArgOperand(4)}
            : IsLevel ? SampleDerivatives1D{Zero, Zero}
                      : getOrSynthesizeSample1DDerivatives(Builder, F, U);
        if (Kind == dxil::ResourceKind::Texture1DArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{1});
          Loaded = createSample1DArray(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, Layer, D.DUdX, D.DUdY, Lod, Builder.getInt1(IsLevel), Bias,
              Offset, MinLodClamp, Builder.getTrue(), CI->getName());
        } else {
          Loaded = createSample1D(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, D.DUdX, D.DUdY, Lod, Builder.getInt1(IsLevel), Bias, Offset,
              MinLodClamp, Builder.getTrue(), CI->getName());
        }
      } else if (Kind == dxil::ResourceKind::TextureCube ||
                 Kind == dxil::ResourceKind::TextureCubeArray) {
        Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
        Value *Z = Builder.CreateExtractElement(Coord, uint64_t{2});
        CubeDirectionDerivatives D;
        if (IsGrad) {
          Value *DX = CI->getArgOperand(3);
          Value *DY = CI->getArgOperand(4);
          D = {Builder.CreateExtractElement(DX, uint64_t{0}),
               Builder.CreateExtractElement(DY, uint64_t{0}),
               Builder.CreateExtractElement(DX, uint64_t{1}),
               Builder.CreateExtractElement(DY, uint64_t{1}),
               Builder.CreateExtractElement(DX, uint64_t{2}),
               Builder.CreateExtractElement(DY, uint64_t{2})};
        } else {
          D = getOrSynthesizeSampleCubeDerivatives(Builder, F, U, V, Z);
        }
        if (Kind == dxil::ResourceKind::TextureCubeArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{3});
          Loaded = createSampleCubeArray(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, V, Z, D.DDirXdX, D.DDirXdY, D.DDirYdX, D.DDirYdY, D.DDirZdX,
              D.DDirZdY, Layer, Zero, Builder.getFalse(), Bias, MinLodClamp,
              Builder.getTrue(), CI->getName());
        } else {
          Loaded = createSampleCube(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, V, Z, D.DDirXdX, D.DDirXdY, D.DDirYdX, D.DDirYdY, D.DDirZdX,
              D.DDirZdY, Zero, Builder.getFalse(), Bias, MinLodClamp,
              Builder.getTrue(), CI->getName());
        }
      } else {
        Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
        SampleDerivatives D;
        if (IsGrad) {
          Value *DX = CI->getArgOperand(3);
          Value *DY = CI->getArgOperand(4);
          D = {Builder.CreateExtractElement(DX, uint64_t{0}),
               Builder.CreateExtractElement(DY, uint64_t{0}),
               Builder.CreateExtractElement(DX, uint64_t{1}),
               Builder.CreateExtractElement(DY, uint64_t{1})};
        } else if (IsLevel) {
          D = {Zero, Zero, Zero, Zero};
        } else {
          D = getOrSynthesizeSample2DDerivatives(Builder, F, U, V);
        }
        Value *OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
        Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
        if (Kind == dxil::ResourceKind::Texture3D) {
          Value *W = Builder.CreateExtractElement(Coord, uint64_t{2});
          SampleDerivatives1D WD;
          if (IsGrad) {
            WD = {
                Builder.CreateExtractElement(CI->getArgOperand(3), uint64_t{2}),
                Builder.CreateExtractElement(CI->getArgOperand(4),
                                             uint64_t{2})};
          } else if (IsLevel) {
            WD = {Zero, Zero};
          } else {
            WD = getOrSynthesizeSample1DDerivatives(Builder, F, W);
          }
          Value *OffsetZ = Builder.CreateExtractElement(Offset, uint64_t{2});
          Loaded = createSample3D(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, V, W, D.DUdX, D.DUdY, D.DVdX, D.DVdY, WD.DUdX, WD.DUdY, Lod,
              Builder.getInt1(IsLevel), Bias, OffsetX, OffsetY, OffsetZ,
              MinLodClamp, Builder.getTrue(), CI->getName());
        } else if (Kind == dxil::ResourceKind::Texture2DArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{2});
          Loaded = createSample2DArray(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, V, Layer, D.DUdX, D.DUdY, D.DVdX, D.DVdY, Zero,
              Builder.getFalse(), Bias, OffsetX, OffsetY, MinLodClamp,
              Builder.getTrue(), CI->getName());
        } else {
          Loaded = createSample2D(
              Builder, Env, Image->getArgOperand(0), Sampler->getArgOperand(0),
              U, V, D.DUdX, D.DUdY, D.DVdX, D.DVdY, Zero, Builder.getFalse(),
              Bias, OffsetX, OffsetY, MinLodClamp, Builder.getTrue(),
              CI->getName());
        }
      }
      CI->replaceAllUsesWith(narrowImageResult(Builder, Loaded, CI->getType()));
      CI->eraseFromParent();
      SampleHandles.insert(Image);
      SampleHandles.insert(Sampler);
      UsesSamplerHeap = true;
      Changed = true;
      continue;
    }
    bool IsComparisonImplicit = ID == Intrinsic::dx_resource_samplecmp ||
                                ID == Intrinsic::dx_resource_samplecmp_clamp;
    if (ID == Intrinsic::dx_resource_samplecmplevelzero ||
        IsComparisonImplicit) {
      if (!(IsComparisonImplicit ? isSupportedDXILSampleCmp(*CI)
                                 : isSupportedDXILSampleCmpLevelZero(*CI)))
        continue;
      IRBuilder<> Builder(CI);
      auto *Image = cast<CallInst>(CI->getArgOperand(0));
      auto Kind = cast<dxil::TextureExtType>(Image->getType())->getDimension();
      auto Sampler = classifySamplerHandle(CI->getArgOperand(1));
      Value *Coord = CI->getArgOperand(2);
      Value *Offset = CI->getArgOperand(4);
      Value *Zero = ConstantFP::get(Builder.getFloatTy(), 0.0);
      Value *ImageIndex = Image->getArgOperand(0);
      Value *SamplerIndex = (*Sampler)->getArgOperand(0);
      Value *U = Coord->getType()->isFloatTy()
                     ? Coord
                     : Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *OffsetX = Offset->getType()->isIntegerTy(32)
                           ? Offset
                           : Builder.CreateExtractElement(Offset, uint64_t{0});
      Value *NoClamp =
          ConstantFP::getInfinity(Builder.getFloatTy(), /*Negative=*/true);
      Value *MinLodClamp = ID == Intrinsic::dx_resource_samplecmp_clamp
                               ? CI->getArgOperand(5)
                               : NoClamp;
      CallInst *NewCall;
      if (Kind == dxil::ResourceKind::TextureCube ||
          Kind == dxil::ResourceKind::TextureCubeArray) {
        Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
        Value *Z = Builder.CreateExtractElement(Coord, uint64_t{2});
        CubeDirectionDerivatives D =
            IsComparisonImplicit
                ? getOrSynthesizeSampleCubeDerivatives(Builder, F, U, V, Z)
                : CubeDirectionDerivatives{Zero, Zero, Zero, Zero, Zero, Zero};
        if (Kind == dxil::ResourceKind::TextureCubeArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{3});
          NewCall = createSampleCmpCubeArray(
              Builder, Env, ImageIndex, SamplerIndex, U, V, Z, D.DDirXdX,
              D.DDirXdY, D.DDirYdX, D.DDirYdY, D.DDirZdX, D.DDirZdY, Layer,
              Zero, Builder.getInt1(!IsComparisonImplicit),
              CI->getArgOperand(3), Zero, MinLodClamp, Builder.getTrue(),
              CI->getName());
        } else {
          NewCall = createSampleCmpCube(
              Builder, Env, ImageIndex, SamplerIndex, U, V, Z, D.DDirXdX,
              D.DDirXdY, D.DDirYdX, D.DDirYdY, D.DDirZdX, D.DDirZdY, Zero,
              Builder.getInt1(!IsComparisonImplicit), CI->getArgOperand(3),
              Zero, MinLodClamp, Builder.getTrue(), CI->getName());
        }
      } else if (Kind == dxil::ResourceKind::Texture1D ||
                 Kind == dxil::ResourceKind::Texture1DArray) {
        SampleDerivatives1D D =
            IsComparisonImplicit
                ? getOrSynthesizeSample1DDerivatives(Builder, F, U)
                : SampleDerivatives1D{Zero, Zero};
        if (Kind == dxil::ResourceKind::Texture1DArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{1});
          NewCall = createSampleCmpArray1D(
              Builder, Env, ImageIndex, SamplerIndex, U, Layer, D.DUdX, D.DUdY,
              Zero, Builder.getInt1(!IsComparisonImplicit),
              CI->getArgOperand(3), Zero, OffsetX, MinLodClamp,
              Builder.getTrue(), CI->getName());
        } else {
          NewCall = createSampleCmp1D(
              Builder, Env, ImageIndex, SamplerIndex, U, D.DUdX, D.DUdY, Zero,
              Builder.getInt1(!IsComparisonImplicit), CI->getArgOperand(3),
              Zero, OffsetX, MinLodClamp, Builder.getTrue(), CI->getName());
        }
      } else {
        Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
        Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
        SampleDerivatives D =
            IsComparisonImplicit
                ? getOrSynthesizeSample2DDerivatives(Builder, F, U, V)
                : SampleDerivatives{Zero, Zero, Zero, Zero};
        if (Kind == dxil::ResourceKind::Texture2DArray) {
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{2});
          NewCall = createSampleCmpArray2D(
              Builder, Env, ImageIndex, SamplerIndex, U, V, Layer, D.DUdX,
              D.DUdY, D.DVdX, D.DVdY, Zero,
              Builder.getInt1(!IsComparisonImplicit), CI->getArgOperand(3),
              Zero, OffsetX, OffsetY, MinLodClamp, Builder.getTrue(),
              CI->getName());
        } else {
          NewCall = createSampleCmp2D(
              Builder, Env, ImageIndex, SamplerIndex, U, V, D.DUdX, D.DUdY,
              D.DVdX, D.DVdY, Zero, Builder.getInt1(!IsComparisonImplicit),
              CI->getArgOperand(3), Zero, OffsetX, OffsetY, MinLodClamp,
              Builder.getTrue(), CI->getName());
        }
      }
      CI->replaceAllUsesWith(NewCall);
      CI->eraseFromParent();
      SampleHandles.insert(Image);
      SampleHandles.insert(*Sampler);
      UsesSamplerHeap = true;
      Changed = true;
      continue;
    }
    bool IsSample = ID == Intrinsic::dx_resource_sample;
    bool IsSampleLevel = ID == Intrinsic::dx_resource_samplelevel;
    if (!IsSample && !IsSampleLevel)
      continue;

    IRBuilder<> Builder(CI);
    Value *Mask = Builder.getTrue();

    // Sample/SampleLevel: (handle, sampler, coord, [lod,] offset).
    std::optional<ImageHandle> Img = classifyImageHandle(CI->getArgOperand(0));
    std::optional<CallInst *> Sampler =
        classifySamplerHandle(CI->getArgOperand(1));
    unsigned OffsetIdx = IsSample ? 3 : 4;
    if (!Img || !Sampler || !isZeroOffset(CI->getArgOperand(OffsetIdx)))
      continue;

    Value *Coord = CI->getArgOperand(2);
    Value *Lod = IsSampleLevel ? CI->getArgOperand(3)
                               : ConstantFP::get(Builder.getFloatTy(), 0.0);
    Value *UseExplicitLod = Builder.getInt1(IsSampleLevel);
    Value *ImageIndex = Img->HandleFromHeap->getArgOperand(0);
    Value *SamplerIndex = (*Sampler)->getArgOperand(0);

    CallInst *NewCall;
    switch (Img->Shape) {
    case ImageShape::Plain2D: {
      Value *U = Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
      // Roadmap H7i: an implicit-LOD sample's real mip level (and, when
      // enabled, an anisotropic footprint) needs this sample's own
      // screen-space derivatives of (U, V), synthesized only in the
      // fragment stage -- the only stage HLSL's implicit `Sample()` is
      // ever legal from; an explicit-LOD `SampleLevel` ignores them, so
      // zero constants (no extra IR) are passed instead.
      SampleDerivatives D =
          IsSample ? getOrSynthesizeSample2DDerivatives(
                        Builder, *CI->getFunction(), U, V)
                   : SampleDerivatives{ConstantFP::get(Builder.getFloatTy(),
                                                       0.0),
                                      ConstantFP::get(Builder.getFloatTy(),
                                                       0.0),
                                      ConstantFP::get(Builder.getFloatTy(),
                                                       0.0),
                                      ConstantFP::get(Builder.getFloatTy(),
                                                       0.0)};
      // DXIL's own `Texture2D::Sample` offset/clamp overloads are not
      // threaded through this pass yet (the `isZeroOffset` gate above
      // already requires the offset operand to be compile-time zero
      // before reaching here) -- pass zero offset and a negative-infinity
      // (no-op) `MinLodClamp` constant, matching roadmap L26's SPIR-V-only
      // scope for ordinary sampling. SampleBias uses its dedicated path
      // above; Sample/SampleLevel retain zero bias here.
      Value *ZeroOffset = Builder.getInt32(0);
      Value *NoMinLodClamp = ConstantFP::getInfinity(Builder.getFloatTy(),
                                                     /*Negative=*/true);
      Value *ZeroBias = ConstantFP::get(Builder.getFloatTy(), 0.0);
      NewCall = createSample2D(Builder, Env, ImageIndex, SamplerIndex, U, V,
                               D.DUdX, D.DUdY, D.DVdX, D.DVdY, Lod,
                               UseExplicitLod, ZeroBias, ZeroOffset,
                               ZeroOffset, NoMinLodClamp, Mask,
                               CI->getName());
      break;
    }
    case ImageShape::Array2D: {
      Value *U = Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
      Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{2});
      // Roadmap L60(a): mirror Plain2D's own derivative synthesis above
      // -- Array2D now threads a real screen-space derivative pair
      // through too.
      SampleDerivatives D =
          IsSample ? getOrSynthesizeSample2DDerivatives(
                        Builder, *CI->getFunction(), U, V)
                   : SampleDerivatives{ConstantFP::get(Builder.getFloatTy(),
                                                       0.0),
                                      ConstantFP::get(Builder.getFloatTy(),
                                                       0.0),
                                      ConstantFP::get(Builder.getFloatTy(),
                                                       0.0),
                                      ConstantFP::get(Builder.getFloatTy(),
                                                       0.0)};
      // Ordinary Sample/SampleLevel retain zero bias/offset and no shader
      // clamp here. SampleBias uses its dedicated path above.
      Value *ZeroOffset = Builder.getInt32(0);
      Value *NoMinLodClamp = ConstantFP::getInfinity(Builder.getFloatTy(),
                                                     /*Negative=*/true);
      Value *ZeroBias = ConstantFP::get(Builder.getFloatTy(), 0.0);
      NewCall = createSample2DArray(
          Builder, Env, ImageIndex, SamplerIndex, U, V, Layer, D.DUdX, D.DUdY,
          D.DVdX, D.DVdY, Lod, UseExplicitLod, ZeroBias, ZeroOffset, ZeroOffset,
          NoMinLodClamp, Mask, CI->getName());
      break;
    }
    case ImageShape::Cube: {
      Value *X = Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *Y = Builder.CreateExtractElement(Coord, uint64_t{1});
      Value *Z = Builder.CreateExtractElement(Coord, uint64_t{2});
      // DXIL's own `TextureCube::Sample` clamp overload is not threaded
      // through this pass yet -- pass a negative-infinity (no-op)
      // `MinLodClamp` constant, matching roadmap L26's SPIR-V-only scope
      // so far.
      Value *NoMinLodClamp = ConstantFP::getInfinity(Builder.getFloatTy(),
                                                     /*Negative=*/true);
      // SampleBias is handled separately above; ordinary sampling has no bias.
      Value *ZeroBias = ConstantFP::get(Builder.getFloatTy(), 0.0);
      // Roadmap L56: mirror Plain2D's own implicit-vs-explicit-LOD
      // derivative gating above, but for the raw direction vector (X, Y,
      // Z) rather than an already-face-local 2D coordinate -- see
      // `getOrSynthesizeSampleCubeDerivatives`'s doc.
      CubeDirectionDerivatives CD =
          IsSample ? getOrSynthesizeSampleCubeDerivatives(
                        Builder, *CI->getFunction(), X, Y, Z)
                   : CubeDirectionDerivatives{
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0)};
      NewCall = createSampleCube(Builder, Env, ImageIndex, SamplerIndex, X, Y,
                                 Z, CD.DDirXdX, CD.DDirXdY, CD.DDirYdX,
                                 CD.DDirYdY, CD.DDirZdX, CD.DDirZdY, Lod,
                                 UseExplicitLod, ZeroBias, NoMinLodClamp, Mask,
                                 CI->getName());
      break;
    }
    case ImageShape::CubeArray: {
      Value *X = Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *Y = Builder.CreateExtractElement(Coord, uint64_t{1});
      Value *Z = Builder.CreateExtractElement(Coord, uint64_t{2});
      Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{3});
      // Ordinary Sample/SampleLevel have no bias here; their clamp overload
      // is not yet threaded through. SampleBias is handled separately above.
      Value *NoMinLodClamp = ConstantFP::getInfinity(Builder.getFloatTy(),
                                                     /*Negative=*/true);
      Value *ZeroBias = ConstantFP::get(Builder.getFloatTy(), 0.0);
      // Roadmap L56: same derivative synthesis as Cube above.
      CubeDirectionDerivatives CD =
          IsSample ? getOrSynthesizeSampleCubeDerivatives(
                        Builder, *CI->getFunction(), X, Y, Z)
                   : CubeDirectionDerivatives{
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0),
                        ConstantFP::get(Builder.getFloatTy(), 0.0)};
      NewCall = createSampleCubeArray(Builder, Env, ImageIndex, SamplerIndex,
                                      X, Y, Z, CD.DDirXdX, CD.DDirXdY,
                                      CD.DDirYdX, CD.DDirYdY, CD.DDirZdX,
                                      CD.DDirZdY, Layer, Lod, UseExplicitLod,
                                      ZeroBias, NoMinLodClamp, Mask,
                                      CI->getName());
      break;
    }
    }
    CI->replaceAllUsesWith(NewCall);
    CI->eraseFromParent();
    Changed = true;
  }
  for (CallInst *Handle : SampleHandles)
    if (Handle->use_empty())
      Handle->eraseFromParent();
  return Changed;
}

/// Returns whether \p F contains any texture/sampler access
/// `lowerImageAccesses` might rewrite -- used only to decide whether a
/// function with no buffer handle of its own still needs its signature
/// grown for the image/sampler heap parameters.
bool hasImageAccesses(const Function &F) {
  for (const Instruction &I : instructions(F)) {
    const auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    Intrinsic::ID ID = getIntrinsicID(CI);
    if (isSupportedDXILLoad(*CI) || isSupportedDXILSampleLevel(*CI) ||
        isSupportedDXILSample(*CI))
      return true;
    if (ID == Intrinsic::dx_resource_samplegrad ||
        ID == Intrinsic::dx_resource_samplegrad_clamp) {
      if (isSupportedDXILSampleGrad(*CI))
        return true;
      continue;
    }
    if (ID == Intrinsic::dx_resource_samplebias ||
        ID == Intrinsic::dx_resource_samplebias_clamp) {
      if (isSupportedDXILSampleBias(*CI))
        return true;
      continue;
    }
    if (ID == Intrinsic::dx_resource_samplecmp ||
        ID == Intrinsic::dx_resource_samplecmp_clamp) {
      if (isSupportedDXILSampleCmp(*CI))
        return true;
      continue;
    }
    if (ID == Intrinsic::dx_resource_samplecmplevelzero) {
      if (isSupportedDXILSampleCmpLevelZero(*CI))
        return true;
      continue;
    }
    bool IsSample = ID == Intrinsic::dx_resource_sample;
    bool IsSampleLevel = ID == Intrinsic::dx_resource_samplelevel;
    if (!IsSample && !IsSampleLevel)
      continue;

    // Mirrors `lowerImageAccesses`'s own eligibility check exactly (without
    // mutating anything): a call this pass cannot actually rewrite (an
    // unsupported dimension, a non-constant/nonzero offset, ...) must not
    // count here, or the signature would grow every time this read-only
    // scan runs, including on the *already-grown* replacement function
    // `ResourceLoweringPass::run`'s module-function loop may revisit for a
    // handle it left deliberately unrewritten (see the header comment's
    // "per-access" note).
    unsigned OffsetIdx = IsSample ? 3 : 4;
    if (classifyImageHandle(CI->getArgOperand(0)) &&
        classifySamplerHandle(CI->getArgOperand(1)) &&
        isZeroOffset(CI->getArgOperand(OffsetIdx)))
      return true;
  }
  return false;
}

/// A co-existing root-constant access's metadata contribution (see
/// `lowerFunctionResources`), or all zero if \p F has none.
struct RootConstantMetadata {
  uint32_t Size = 0;
  uint32_t Space = 0;
  uint32_t Register = 0;
};

/// Lowers every canonicalizable resource access \p F performs, returning the
/// rewritten function (a new one, since its signature grows), or nullptr if
/// \p F has no `handlefromheap` calls, no image/sampler access, or uses a
/// buffer resource kind this pass cannot model (see the header comment).
/// Statically-known heap indices found along the way are appended to
/// \p StaticHeapIndices; a co-existing root-constant access (see
/// RootConstantLowering.h), if any, is written to \p RootConstant.
Function *lowerFunctionResources(Function &F,
                                 SmallVectorImpl<uint32_t> &StaticHeapIndices,
                                 RootConstantMetadata &RootConstant,
                                 bool &UsesSamplerHeap) {
  if (F.isDeclaration())
    return nullptr;

  std::optional<SmallVector<HandleInfo, 4>> Handles = collectHandles(F);
  if (!Handles)
    return nullptr;
  bool HasImages = hasImageAccesses(F);
  if (Handles->empty() && !HasImages)
    return nullptr;

  ResourceCallEnv Env;
  Function *NewF = addResourceEnvParams(F, Env);

  for (const HandleInfo &Info : *Handles) {
    Value *DescriptorIndex = Info.Handle->getArgOperand(0);
    if (auto *ConstIdx = dyn_cast<ConstantInt>(DescriptorIndex))
      StaticHeapIndices.push_back(
          static_cast<uint32_t>(ConstIdx->getZExtValue()));
    lowerAccesses(Info, Env, DescriptorIndex);
  }

  if (HasImages) {
    ImageCallEnv ImgEnv;
    ImgEnv.ImageHeap = Env.ImageHeap;
    ImgEnv.ImageHeapCount = Env.ImageHeapCount;
    ImgEnv.SamplerHeap = Env.SamplerHeap;
    ImgEnv.SamplerHeapCount = Env.SamplerHeapCount;
    lowerImageAccesses(*NewF, ImgEnv, UsesSamplerHeap);
  }

  // A shader that also reads a recognized root-constant binding (see
  // "Root constants" in feme/docs/FeMeCPUDesign.md) has that access
  // finished here, reusing the `RootConstants`/`RootConstantSize`
  // parameters `addResourceEnvParams` above just added, rather than by
  // `feme::cpu::RootConstantLoweringPass` adding its own -- see
  // RootConstantLowering.h's file comment for why a function with any
  // bindless resource access of its own is handled this way instead.
  if (std::optional<RootConstantAccess> Access =
          matchRootConstantAccess(*NewF)) {
    RootConstant.Size = lowerRootConstantAccess(*Access, Env.RootConstants,
                                                Env.RootConstantSize);
    RootConstant.Space = Access->Space;
    RootConstant.Register = Access->Register;
  }

  return NewF;
}

/// Attaches the `!feme.cpu.resources` heap-usage metadata node "Heap usage
/// discovery" describes for \p F: its name, \p RootConstant's byte span
/// (0 if \p F has no co-existing root-constant access -- see
/// `lowerFunctionResources`), whether sampling uses the sampler
/// heap, \p
/// RootConstant's binding (space/register, both 0 if \p F has none), a
/// root-constant min offset (always 0: a DXIL root constant's own
/// register-bound view always starts its own span at byte 0, unlike a
/// SPIR-V push-constant block's own reflected struct, which may declare a
/// nonzero leading `layout(offset=N)` -- see
/// `feme::cpu::ResourceInfo::RootConstantMinOffset`'s own comment, roadmap
/// H6u), and the sorted, deduplicated statically-known heap indices the
/// shader reads through a constant descriptor index.
void attachResourceMetadata(Function &F,
                            SmallVectorImpl<uint32_t> &StaticHeapIndices,
                            const RootConstantMetadata &RootConstant,
                            bool UsesSamplerHeap) {
  llvm::sort(StaticHeapIndices);
  StaticHeapIndices.erase(llvm::unique(StaticHeapIndices),
                          StaticHeapIndices.end());

  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Metadata *, 8> Ops;
  Ops.push_back(MDString::get(Ctx, F.getName()));
  Ops.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstant.Size)));
  Ops.push_back(ConstantAsMetadata::get(
      ConstantInt::get(Type::getInt1Ty(Ctx), UsesSamplerHeap)));
  Ops.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstant.Space)));
  Ops.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstant.Register)));
  Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)));
  for (uint32_t Idx : StaticHeapIndices)
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, Idx)));

  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.resources")
      ->addOperand(MDNode::get(Ctx, Ops));
}

} // namespace

static bool isSupportedDXILFloatingSample(const CallInst &CI,
                                          Intrinsic::ID BaseID) {
  Intrinsic::ID ID = getIntrinsicID(&CI);
  bool IsGrad = BaseID == Intrinsic::dx_resource_samplegrad;
  bool IsLevel = BaseID == Intrinsic::dx_resource_samplelevel;
  bool IsImplicit = BaseID == Intrinsic::dx_resource_sample;
  bool HasClamp = !IsLevel && !IsImplicit &&
                  ID == (IsGrad ? Intrinsic::dx_resource_samplegrad_clamp
                                : Intrinsic::dx_resource_samplebias_clamp);
  unsigned OffsetIdx = IsImplicit ? 3 : IsGrad ? 5 : 4;
  if ((ID != BaseID && !HasClamp) ||
      CI.arg_size() != OffsetIdx + (HasClamp ? 2u : 1u) ||
      (HasClamp && !CI.getArgOperand(OffsetIdx + 1)->getType()->isFloatTy()) ||
      (!IsGrad && !IsImplicit &&
       !CI.getArgOperand(3)->getType()->isFloatTy()) ||
      (!IsGrad && !IsLevel &&
       feme::getShaderStage(*CI.getFunction()) != feme::ShaderStage::Fragment))
    return false;
  auto *Image = dyn_cast<CallInst>(CI.getArgOperand(0));
  auto Sampler = classifySamplerHandle(CI.getArgOperand(1));
  auto *TextureTy =
      dyn_cast<dxil::TextureExtType>(CI.getArgOperand(0)->getType());
  if (!Image ||
      getIntrinsicID(Image) != Intrinsic::dx_resource_handlefromheap ||
      !TextureTy || !Sampler)
    return false;
  auto *SamplerTy = cast<dxil::SamplerExtType>(CI.getArgOperand(1)->getType());
  if (TextureTy->getNumTypeParameters() != 1 ||
      TextureTy->getNumIntParameters() != 4 ||
      SamplerTy->getNumIntParameters() != 1 ||
      SamplerTy->getSamplerType() != dxil::SamplerType::Default)
    return false;
  Type *FloatTy = Type::getFloatTy(CI.getContext());
  unsigned TextureWidth =
      getTexelWidth(TextureTy->getTypeParameter(0), FloatTy);
  unsigned ResultWidth = getTexelWidth(CI.getType(), FloatTy);
  auto Kind = TextureTy->getDimension();
  unsigned NumCoords;
  switch (Kind) {
  case dxil::ResourceKind::Texture1D:
    NumCoords = 1;
    break;
  case dxil::ResourceKind::Texture1DArray:
  case dxil::ResourceKind::Texture2D:
    NumCoords = 2;
    break;
  case dxil::ResourceKind::Texture2DArray:
  case dxil::ResourceKind::Texture3D:
  case dxil::ResourceKind::TextureCube:
    NumCoords = 3;
    break;
  case dxil::ResourceKind::TextureCubeArray:
    NumCoords = 4;
    break;
  default:
    return false;
  }
  bool Is1D = Kind == dxil::ResourceKind::Texture1D ||
              Kind == dxil::ResourceKind::Texture1DArray;
  if ((IsImplicit || IsLevel) && !Is1D && Kind != dxil::ResourceKind::Texture3D)
    return false;
  bool IsCube = Kind == dxil::ResourceKind::TextureCube ||
                Kind == dxil::ResourceKind::TextureCubeArray;
  LLVMContext &Ctx = CI.getContext();
  Type *CoordTy = Type::getFloatTy(Ctx);
  if (NumCoords != 1)
    CoordTy = FixedVectorType::get(CoordTy, NumCoords);
  Type *GradTy = Type::getFloatTy(Ctx);
  if (!Is1D)
    GradTy = FixedVectorType::get(
        GradTy, IsCube || Kind == dxil::ResourceKind::Texture3D ? 3 : 2);
  if (IsGrad && (CI.getArgOperand(3)->getType() != GradTy ||
                 CI.getArgOperand(4)->getType() != GradTy))
    return false;
  Type *OffsetTy = Type::getInt32Ty(Ctx);
  unsigned NumOffsets = Kind == dxil::ResourceKind::Texture3D ? 3 : 2;
  if (!IsCube && !Is1D)
    OffsetTy = FixedVectorType::get(Type::getInt32Ty(Ctx), NumOffsets);
  if (!TextureWidth || !ResultWidth || ResultWidth > TextureWidth ||
      CI.getArgOperand(2)->getType() != CoordTy ||
      CI.getArgOperand(OffsetIdx)->getType() != OffsetTy)
    return false;
  auto *Offset = dyn_cast<Constant>(CI.getArgOperand(OffsetIdx));
  if (IsCube)
    return Offset && (isa<UndefValue>(Offset) || Offset->isNullValue());
  if (Is1D)
    return isa_and_nonnull<ConstantInt>(Offset);
  if (!Offset)
    return false;
  for (unsigned I = 0; I < NumOffsets; ++I)
    if (!isa_and_nonnull<ConstantInt>(Offset->getAggregateElement(I)))
      return false;
  return true;
}

bool feme::cpu::isSupportedDXILSampleBias(const CallInst &CI) {
  return isSupportedDXILFloatingSample(CI, Intrinsic::dx_resource_samplebias);
}

bool feme::cpu::isSupportedDXILSampleGrad(const CallInst &CI) {
  return isSupportedDXILFloatingSample(CI, Intrinsic::dx_resource_samplegrad);
}

bool feme::cpu::isSupportedDXILSampleLevel(const CallInst &CI) {
  return isSupportedDXILFloatingSample(CI, Intrinsic::dx_resource_samplelevel);
}

bool feme::cpu::isSupportedDXILSample(const CallInst &CI) {
  return isSupportedDXILFloatingSample(CI, Intrinsic::dx_resource_sample);
}

bool feme::cpu::isSupportedDXILLoad(const CallInst &CI) {
  if (getIntrinsicID(&CI) != Intrinsic::dx_resource_load_level ||
      CI.arg_size() != 4)
    return false;
  auto *Image = dyn_cast<CallInst>(CI.getArgOperand(0));
  auto *Ty = dyn_cast<dxil::TextureExtType>(CI.getArgOperand(0)->getType());
  if (!Image ||
      getIntrinsicID(Image) != Intrinsic::dx_resource_handlefromheap || !Ty ||
      Ty->getNumTypeParameters() != 1 || Ty->getNumIntParameters() != 4)
    return false;
  auto Kind = Ty->getDimension();
  bool Is1D = Kind == dxil::ResourceKind::Texture1D ||
              Kind == dxil::ResourceKind::Texture1DArray;
  bool Is2D = Kind == dxil::ResourceKind::Texture2D ||
              Kind == dxil::ResourceKind::Texture2DArray;
  if (!Is1D && !Is2D && Kind != dxil::ResourceKind::Texture3D)
    return false;
  Type *ScalarTy = Ty->getTypeParameter(0)->getScalarType();
  if (!ScalarTy->isFloatTy() && (Is2D || !ScalarTy->isIntegerTy(32)))
    return false;
  unsigned TextureWidth = getTexelWidth(Ty->getTypeParameter(0), ScalarTy);
  unsigned ResultWidth = getTexelWidth(CI.getType(), ScalarTy);
  Type *IntTy = Type::getInt32Ty(CI.getContext());
  Type *CoordTy =
      Kind == dxil::ResourceKind::Texture1D
          ? IntTy
          : FixedVectorType::get(
                IntTy, Is1D || Kind == dxil::ResourceKind::Texture2D ? 2 : 3);
  Type *OffsetTy = Is1D ? IntTy : FixedVectorType::get(IntTy, Is2D ? 2 : 3);
  if (!TextureWidth || !ResultWidth || ResultWidth > TextureWidth ||
      CI.getArgOperand(1)->getType() != CoordTy ||
      !CI.getArgOperand(2)->getType()->isIntegerTy(32) ||
      CI.getArgOperand(3)->getType() != OffsetTy)
    return false;
  auto *Offset = dyn_cast<Constant>(CI.getArgOperand(3));
  if (Is1D)
    return isa_and_nonnull<ConstantInt>(Offset);
  if (!Offset)
    return false;
  for (unsigned Axis = 0; Axis < (Is2D ? 2u : 3u); ++Axis)
    if (!isa_and_nonnull<ConstantInt>(Offset->getAggregateElement(Axis)))
      return false;
  return true;
}

static bool isSupportedDXILComparisonSample(const CallInst &CI) {
  if (!CI.getType()->isFloatTy() ||
      !CI.getArgOperand(3)->getType()->isFloatTy())
    return false;
  auto *TextureTy =
      dyn_cast<dxil::TextureExtType>(CI.getArgOperand(0)->getType());
  auto *SamplerTy =
      dyn_cast<dxil::SamplerExtType>(CI.getArgOperand(1)->getType());
  if (!TextureTy || TextureTy->getNumTypeParameters() != 1 ||
      TextureTy->getNumIntParameters() != 4 ||
      !TextureTy->getTypeParameter(0)->getScalarType()->isFloatTy() ||
      (TextureTy->getDimension() != dxil::ResourceKind::Texture1D &&
       TextureTy->getDimension() != dxil::ResourceKind::Texture1DArray &&
       TextureTy->getDimension() != dxil::ResourceKind::Texture2D &&
       TextureTy->getDimension() != dxil::ResourceKind::Texture2DArray &&
       TextureTy->getDimension() != dxil::ResourceKind::TextureCube &&
       TextureTy->getDimension() != dxil::ResourceKind::TextureCubeArray) ||
      !SamplerTy || SamplerTy->getNumIntParameters() != 1 ||
      SamplerTy->getSamplerType() != dxil::SamplerType::Comparison ||
      getIntrinsicID(CI.getArgOperand(0)) !=
          Intrinsic::dx_resource_handlefromheap ||
      !classifySamplerHandle(CI.getArgOperand(1)))
    return false;
  LLVMContext &Ctx = CI.getContext();
  bool Is1D = TextureTy->getDimension() == dxil::ResourceKind::Texture1D ||
              TextureTy->getDimension() == dxil::ResourceKind::Texture1DArray;
  bool IsCube =
      TextureTy->getDimension() == dxil::ResourceKind::TextureCube ||
      TextureTy->getDimension() == dxil::ResourceKind::TextureCubeArray;
  bool IsArray =
      TextureTy->getDimension() == dxil::ResourceKind::Texture1DArray ||
      TextureTy->getDimension() == dxil::ResourceKind::Texture2DArray ||
      TextureTy->getDimension() == dxil::ResourceKind::TextureCubeArray;
  unsigned NumCoords = (IsCube ? 3 : Is1D ? 1 : 2) + (IsArray ? 1 : 0);
  Type *CoordTy = NumCoords == 1
                      ? Type::getFloatTy(Ctx)
                      : FixedVectorType::get(Type::getFloatTy(Ctx), NumCoords);
  Type *OffsetTy = Type::getInt32Ty(Ctx);
  if (!Is1D && !IsCube)
    OffsetTy = FixedVectorType::get(Type::getInt32Ty(Ctx), 2);
  if (CI.getArgOperand(2)->getType() != CoordTy ||
      CI.getArgOperand(4)->getType() != OffsetTy)
    return false;
  auto *Offset = dyn_cast<Constant>(CI.getArgOperand(4));
  if (IsCube)
    return Offset && (isa<UndefValue>(Offset) || Offset->isNullValue());
  if (Is1D)
    return isa_and_nonnull<ConstantInt>(Offset);
  return Offset &&
         isa_and_nonnull<ConstantInt>(Offset->getAggregateElement(0u)) &&
         isa_and_nonnull<ConstantInt>(Offset->getAggregateElement(1u));
}

bool feme::cpu::isSupportedDXILSampleCmpLevelZero(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::dx_resource_samplecmplevelzero &&
         CI.arg_size() == 5 && isSupportedDXILComparisonSample(CI);
}

bool feme::cpu::isSupportedDXILSampleCmp(const CallInst &CI) {
  Intrinsic::ID ID = getIntrinsicID(&CI);
  bool HasClamp = ID == Intrinsic::dx_resource_samplecmp_clamp;
  if ((ID != Intrinsic::dx_resource_samplecmp && !HasClamp) ||
      CI.arg_size() != (HasClamp ? 6u : 5u) ||
      (HasClamp && !CI.getArgOperand(5)->getType()->isFloatTy()) ||
      feme::getShaderStage(*CI.getFunction()) != feme::ShaderStage::Fragment)
    return false;
  return isSupportedDXILComparisonSample(CI);
}

/// Replaces every `llvm.dx.resource.nonuniformindex` call with its own index
/// operand. The marker only tells a GPU backend that a descriptor index may
/// diverge across a wave; the CPU target evaluates every lane's index
/// independently regardless, so it carries no information anything below this
/// point needs (see the "Resource Model" section of
/// feme/docs/FeMeCPUDesign.md).
static bool stripNonUniformIndexMarkers(Module &M) {
  Function *F = M.getFunction(
      Intrinsic::getName(Intrinsic::dx_resource_nonuniformindex));
  if (!F)
    return false;

  for (User *U : llvm::make_early_inc_range(F->users())) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI || CI->getCalledFunction() != F)
      continue;
    CI->replaceAllUsesWith(CI->getArgOperand(0));
    CI->eraseFromParent();
  }
  if (F->isDeclaration() && F->use_empty())
    F->eraseFromParent();
  return true;
}

PreservedAnalyses ResourceLoweringPass::run(Module &M,
                                            ModuleAnalysisManager &) {
  bool Changed = stripNonUniformIndexMarkers(M);
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    SmallVector<uint32_t, 4> StaticHeapIndices;
    RootConstantMetadata RootConstant;
    bool UsesSamplerHeap = false;
    Function *NewF = lowerFunctionResources(F, StaticHeapIndices, RootConstant,
                                            UsesSamplerHeap);
    if (!NewF)
      continue;
    Changed = true;
    attachResourceMetadata(*NewF, StaticHeapIndices, RootConstant,
                           UsesSamplerHeap);
  }

  // An unused `handlefromheap`/`handlefrombinding` declaration is left
  // behind once its last caller is rewritten away (the latter only if this
  // function also lowered a co-existing root-constant access, see
  // `lowerFunctionResources`); nothing downstream can select a call to
  // either.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        (F.getIntrinsicID() == Intrinsic::dx_resource_handlefromheap ||
         F.getIntrinsicID() == Intrinsic::dx_resource_handlefrombinding))
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
