//===- SPIRVResourceLowering.cpp - SPIR-V bound resource emulation -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVResourceLowering.h"

#include "feme/Transforms/CPU/ResourceCalls.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include <map>
#include <optional>
#include <tuple>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// A bound `spirv.VulkanBuffer` handle's identity: (descriptor set,
/// binding), playing the same role DXIL's (register space, register) pair
/// does -- see the header comment's "SPIR-V's (descriptor set, binding)
/// pair" note.
struct RangeKey {
  uint32_t Set;
  uint32_t Binding;

  bool operator<(const RangeKey &Other) const {
    return std::tie(Set, Binding) < std::tie(Other.Set, Other.Binding);
  }
};

/// The outcome of collecting one identity's uses: either a single,
/// consistent element stride, or a conflicting re-declaration that leaves
/// every handle at that identity un-normalized.
struct RangeEntry {
  uint64_t Stride = 0;
  bool Conflicting = false;
  /// Assigned once every range has been collected (see `assignHeapBases`).
  uint32_t HeapBase = 0;
};

/// One `handlefrombinding` call this pass will rewrite, plus its identity.
struct BoundHandle {
  CallInst *Handle;
  RangeKey Key;
  uint64_t Stride;
};

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// Returns \p Handle's buffer element stride (in bytes) if its type is a
/// `spirv.VulkanBuffer` handle over a flat (non-aggregate-accessed) element
/// -- see `feme::spirv::convertBufferBlockType` in SPIRVToLLVMPatterns.cpp
/// for the handle type this recognizes: one type parameter (the buffer's
/// `!llvm.array<0 x ElemTy>` runtime array) and two integer parameters
/// (storage class, writability), neither of which is the stride itself --
/// SPIR-V records that implicitly via `ElemTy`'s own store size, mirroring
/// how `feme::cpu::ResourceLoweringPass::classifyHandle` recovers a DXIL
/// `dx.RawBuffer`'s stride from its element type parameter. Returns
/// `std::nullopt` for any other handle kind (an image/sampler resource, not
/// yet covered -- see the header comment).
std::optional<uint64_t> classifyVulkanBufferStride(const CallInst &Handle,
                                                   const DataLayout &DL) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "spirv.VulkanBuffer")
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1)
    return std::nullopt;
  auto *ArrayTy = dyn_cast<ArrayType>(HandleTy->getTypeParameter(0));
  if (!ArrayTy)
    return std::nullopt;
  return DL.getTypeStoreSize(ArrayTy->getElementType());
}

/// Checks that every use of \p Handle is the flat-element access shape this
/// pass models: a `llvm.spv.resource.getpointer` call whose own result is
/// used only by an ordinary `load`, or a `store` it is the pointer operand
/// (not the stored value) of -- see the header comment's "access shape"
/// bullet. Any further `getelementptr` into the element's own fields (a
/// structured-buffer field access) is left unmodeled, matching
/// `feme::cpu::ResourceLoweringPass::hasOnlySupportedUses`'s own narrowing.
bool hasOnlySupportedUses(const CallInst &Handle) {
  for (const User *U : Handle.users()) {
    const auto *GetPtr = dyn_cast<CallInst>(U);
    if (!GetPtr || getIntrinsicID(GetPtr) != Intrinsic::spv_resource_getpointer)
      return false;
    for (const User *PU : GetPtr->users()) {
      if (const auto *SI = dyn_cast<StoreInst>(PU)) {
        if (SI->getPointerOperand() != GetPtr)
          return false;
        continue;
      }
      if (!isa<LoadInst>(PU))
        return false;
    }
  }
  return true;
}

/// Collects every normalizable `handlefrombinding` call in \p F, or
/// `std::nullopt` if any of them uses a resource kind or access shape this
/// pass cannot model -- in which case \p F is left entirely unmodified
/// rather than partially rewritten, matching
/// `feme::cpu::ResourceLoweringPass::collectHandles`'s own contract.
std::optional<SmallVector<BoundHandle, 4>> collectHandles(Function &F) {
  const DataLayout &DL = F.getDataLayout();
  SmallVector<BoundHandle, 4> Handles;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::spv_resource_handlefrombinding)
      continue;

    std::optional<uint64_t> Stride = classifyVulkanBufferStride(*CI, DL);
    if (!Stride)
      return std::nullopt; // Not one of the kinds this pass normalizes.
    if (!hasOnlySupportedUses(*CI))
      return std::nullopt;

    auto *SetC = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    auto *BindingC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    if (!SetC || !BindingC)
      return std::nullopt; // Non-constant binding: not produced today.

    RangeKey Key{static_cast<uint32_t>(SetC->getZExtValue()),
                 static_cast<uint32_t>(BindingC->getZExtValue())};
    Handles.push_back(BoundHandle{CI, Key, *Stride});
  }
  return Handles;
}

/// Assigns each non-conflicting identity a contiguous single-slot base in
/// the reserved heap prefix, sorted by identity for a deterministic layout
/// -- mirroring `feme::cpu::BoundResourceNormalizationPass`'s
/// `assignHeapBases`, with every range implicitly of size 1 (see the header
/// comment). Returns the total reserved prefix size.
uint32_t assignHeapBases(std::map<RangeKey, RangeEntry> &Ranges) {
  uint32_t Base = 0;
  for (auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    Entry.HeapBase = Base++;
  }
  return Base;
}

/// Builds \p F's replacement: the same function with the six trailing
/// resource/root-constant ABI parameters appended, exactly as
/// `feme::cpu::ResourceLoweringPass`'s own (anonymous-namespace, so
/// duplicated here rather than shared -- matching how
/// `feme::amdgpu::ResourceLoweringPass`'s own `addBindingArguments` is
/// likewise a separate copy for its differently-shaped parameter list)
/// `addResourceEnvParams` does.
Function *addResourceEnvParams(Function &F, ResourceCallEnv &Env) {
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  SmallVector<Type *, 6> ParamTypes(F.getFunctionType()->params());
  ParamTypes.append({PtrTy, I32Ty, PtrTy, I32Ty, PtrTy, I32Ty});

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
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

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

/// Rewrites every access through \p BH.Handle -- a `getpointer` call
/// followed by a load or store, see `hasOnlySupportedUses` -- into the
/// corresponding canonical `feme.cpu.resource.load.raw`/`store.raw` call,
/// using \p Env and the constant heap index \p BH.Key was assigned.
void lowerAccesses(const BoundHandle &BH, const ResourceCallEnv &Env,
                   uint32_t HeapBase) {
  LLVMContext &Ctx = BH.Handle->getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *DescriptorIndex = ConstantInt::get(I32Ty, HeapBase);
  Value *Mask = ConstantInt::getTrue(Ctx);

  for (User *U : llvm::make_early_inc_range(BH.Handle->users())) {
    auto *GetPtr = cast<CallInst>(U);
    IRBuilder<> PtrBuilder(GetPtr);
    Value *ElemIdx = PtrBuilder.CreateZExt(GetPtr->getArgOperand(1), I64Ty);
    Value *Offset =
        PtrBuilder.CreateMul(ElemIdx, ConstantInt::get(I64Ty, BH.Stride));

    for (User *PU : llvm::make_early_inc_range(GetPtr->users())) {
      if (auto *LI = dyn_cast<LoadInst>(PU)) {
        IRBuilder<> Builder(LI);
        CallInst *Loaded = createRawLoad(Builder, Env, DescriptorIndex, Offset,
                                         Mask, LI->getType(), LI->getName());
        LI->replaceAllUsesWith(Loaded);
        LI->eraseFromParent();
        continue;
      }
      auto *SI = cast<StoreInst>(PU);
      IRBuilder<> Builder(SI);
      createRawStore(Builder, Env, DescriptorIndex, Offset,
                     SI->getValueOperand(), Mask);
      SI->eraseFromParent();
    }
    GetPtr->eraseFromParent();
  }
  BH.Handle->eraseFromParent();
}

/// Attaches the `!feme.cpu.resources` metadata node
/// `feme::cpu::ResourceInfo::fromModule` reads: name, root constant size
/// (always 0 -- root constants are not yet raised from SPIR-V push
/// constants into this form), whether the sampler heap is used (always
/// false -- no SPIR-V sampler handle is normalized by this pass), and an
/// empty statically-known-heap-index tail (every heap index this pass
/// assigns is already static by construction, but none is a *dynamic* heap
/// access the way `feme::cpu::ResourceLoweringPass`'s own tail records --
/// see "Heap usage discovery" in feme/docs/FeMeCPUDesign.md).
void attachResourceMetadata(Function &F) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Metadata *Ops[] = {MDString::get(Ctx, F.getName()),
                     ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
                     ConstantAsMetadata::get(ConstantInt::getFalse(Ctx))};
  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.resources")
      ->addOperand(MDNode::get(Ctx, Ops));
}

/// Attaches the `!feme.cpu.bound_resources` metadata node
/// `feme::cpu::ResourceInfo::fromModule` reads, in the same shape
/// `feme::cpu::BoundResourceNormalizationPass::attachBoundResourceMetadata`
/// produces: name, the reserved prefix size, then each accepted identity as
/// a (space, register, range-size, heap-base) tuple -- SPIR-V's (set,
/// binding) filling the (space, register) slots per the header comment's
/// correspondence, and range-size always 1.
void attachBoundResourceMetadata(Function &F, uint32_t PrefixSize,
                                 const std::map<RangeKey, RangeEntry> &Ranges) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Metadata *, 8> Ops;
  Ops.push_back(MDString::get(Ctx, F.getName()));
  Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, PrefixSize)));
  for (const auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, Key.Set)));
    Ops.push_back(
        ConstantAsMetadata::get(ConstantInt::get(I32Ty, Key.Binding)));
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, 1)));
    Ops.push_back(
        ConstantAsMetadata::get(ConstantInt::get(I32Ty, Entry.HeapBase)));
  }
  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.bound_resources")
      ->addOperand(MDNode::get(Ctx, Ops));
}

} // namespace

PreservedAnalyses SPIRVResourceLoweringPass::run(Module &M,
                                                 ModuleAnalysisManager &) {
  // Collect every function's normalizable handles first, and every
  // identity's element stride across the whole module, before rewriting
  // anything -- a conflicting re-declaration can only be detected once all
  // of them are known (see `feme::cpu::BoundResourceNormalizationPass`'s
  // own two-phase shape).
  DenseMap<Function *, SmallVector<BoundHandle, 4>> PerFunctionHandles;
  std::map<RangeKey, RangeEntry> Ranges;
  for (Function &F : M) {
    std::optional<SmallVector<BoundHandle, 4>> Handles = collectHandles(F);
    if (!Handles || Handles->empty())
      continue;
    for (const BoundHandle &BH : *Handles) {
      auto It = Ranges.find(BH.Key);
      if (It == Ranges.end())
        Ranges.emplace(BH.Key, RangeEntry{BH.Stride, /*Conflicting=*/false});
      else if (It->second.Stride != BH.Stride)
        It->second.Conflicting = true;
    }
    PerFunctionHandles[&F] = std::move(*Handles);
  }
  if (PerFunctionHandles.empty())
    return PreservedAnalyses::all();

  uint32_t PrefixSize = assignHeapBases(Ranges);

  bool Changed = false;
  for (auto &[F, Handles] : PerFunctionHandles) {
    // Every handle in this function is dropped if its own identity turned
    // out to conflict with another function's use of the same (set,
    // binding); the rest of the function is still rewritten, matching
    // `feme::cpu::BoundResourceNormalizationPass::rewriteBoundHandles`'s own
    // per-handle (not per-function) conflict check.
    bool RewroteAny = false;
    ResourceCallEnv Env;
    Function *NewF = F;
    for (const BoundHandle &BH : Handles) {
      const RangeEntry &Entry = Ranges.at(BH.Key);
      if (Entry.Conflicting)
        continue;
      if (!RewroteAny) {
        NewF = addResourceEnvParams(*F, Env);
        RewroteAny = true;
      }
      lowerAccesses(BH, Env, Entry.HeapBase);
    }
    if (!RewroteAny)
      continue;
    Changed = true;
    attachResourceMetadata(*NewF);
    attachBoundResourceMetadata(*NewF, PrefixSize, Ranges);
  }

  // An unused `handlefrombinding` declaration is left behind once its last
  // accepted caller is rewritten away; a conflicting one may still have
  // users, left for `feme::cpu::checkSupportedRaisedOps` to reject.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        F.getIntrinsicID() == Intrinsic::spv_resource_handlefrombinding)
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
