//===- SPIRVResourceLowering.cpp - SPIR-V bound resource emulation -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVResourceLowering.h"

#include "feme/Transforms/CPU/ResourceCalls.h"
#include "feme/Transforms/CPU/SPIRVPushConstantLowering.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include <limits>
#include <map>
#include <optional>
#include <tuple>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Whether a bound handle wraps a storage buffer -- a homogeneous,
/// dynamically-indexed runtime array (`RWStructuredBuffer<T>`/
/// `StructuredBuffer<T>`) -- a uniform buffer -- a fixed set of
/// differently-typed named fields at fixed byte offsets
/// (`cbuffer`/`ConstantBuffer<T>`) -- or (V4) a texel buffer -- a
/// `Buffer<T>`/`RWBuffer<T>`-shaped, format-converting view over a
/// `Dim::Buffer` SPIR-V image. The three need different offset arithmetic
/// (see `lowerAccesses`): a storage buffer access multiplies a (possibly
/// dynamic) array index by a fixed element stride, a uniform buffer access
/// resolves a (always compile-time-constant) field index directly to a
/// fixed struct-layout byte offset, and a texel buffer access converts
/// through its format at a fixed element index with no byte-offset
/// arithmetic of its own (see `feme::cpu::createTypedLoad`/`createTypedStore`
/// in ResourceCalls.h).
enum class BufferKind { Storage, Uniform, TexelStorage, TexelUniform };

/// Whether \p Kind is one of the two texel-buffer kinds (see `BufferKind`).
bool isTexelBufferKind(BufferKind Kind) {
  return Kind == BufferKind::TexelStorage || Kind == BufferKind::TexelUniform;
}

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
/// consistent buffer shape (kind, element stride or struct layout, and
/// array range size), or a conflicting re-declaration that leaves every
/// handle at that identity un-normalized (see the header comment's "Scope"
/// note).
struct RangeEntry {
  BufferKind Kind = BufferKind::Storage;
  /// The storage-buffer element stride (`Kind == Storage`); unused, always
  /// 0, for a uniform buffer, whose offsets come from `ElementStruct`'s own
  /// layout instead.
  uint64_t Stride = 0;
  /// The uniform-buffer field struct (`Kind == Uniform`); null for a
  /// storage buffer.
  StructType *ElementStruct = nullptr;
  /// The texel-buffer shader-side element type (`isTexelBufferKind(Kind)`);
  /// null otherwise. Always `<4 x float>` in this milestone (see the header
  /// comment's texel-buffer scope note).
  Type *TexelElementType = nullptr;
  uint32_t RangeSize = 0;
  bool Conflicting = false;
  /// Assigned once every range has been collected (see `assignHeapBases`).
  uint32_t HeapBase = 0;
};

/// One `handlefrombinding` call this pass will rewrite, plus its identity
/// and declared array range size. The (possibly dynamic) array index is
/// deliberately *not* cached here -- it is re-read from `Handle`'s own
/// operand at lowering time instead (`BH.Handle->getArgOperand(3)` in
/// `lowerAccesses`), because `addResourceEnvParams` below rebuilds the
/// handle's function (moving its body to a new `Function`, RAUWing every
/// argument, then erasing the original), which would otherwise leave a
/// `Value*` captured from a stale `Argument` dangling -- exactly the bug
/// roadmap step R25 fixed in `feme::cpu::lowerFunctionRootConstants` for the
/// same reason (see that pass's own header comment).
struct BoundHandle {
  CallInst *Handle;
  RangeKey Key;
  BufferKind Kind;
  uint64_t Stride;
  StructType *ElementStruct;
  Type *TexelElementType;
  uint32_t RangeSize;
};

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// One handle's classification: which kind of buffer it is, and the
/// element shape needed to compute an access's byte offset later (see
/// `BufferKind`).
struct HandleClassification {
  BufferKind Kind;
  uint64_t Stride = 0;
  StructType *ElementStruct = nullptr;
  Type *TexelElementType = nullptr;
};

/// Returns \p Handle's buffer classification if its type is a
/// `spirv.VulkanBuffer` handle over a flat (non-aggregate-accessed) element
/// -- see `feme::spirv::convertBufferBlockType`/`convertUniformBlockType`
/// in SPIRVToLLVMPatterns.cpp for the two handle shapes this recognizes:
/// one type parameter (either a storage buffer's `!llvm.array<0 x ElemTy>`
/// runtime array, or a uniform buffer's own field struct directly) and two
/// integer parameters (storage class, writability), neither of which is
/// the stride itself -- SPIR-V records that implicitly via `ElemTy`'s own
/// store size, mirroring how `feme::cpu::ResourceLoweringPass::
/// classifyHandle` recovers a DXIL `dx.RawBuffer`'s stride from its element
/// type parameter. Returns `std::nullopt` for any other handle kind (an
/// image/sampler resource, not yet covered -- see the header comment).
std::optional<HandleClassification>
classifyVulkanBufferHandle(const CallInst &Handle, const DataLayout &DL) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "spirv.VulkanBuffer")
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1)
    return std::nullopt;
  Type *Param = HandleTy->getTypeParameter(0);
  if (auto *ArrayTy = dyn_cast<ArrayType>(Param))
    return HandleClassification{BufferKind::Storage,
                                DL.getTypeStoreSize(ArrayTy->getElementType()),
                                nullptr};
  if (auto *StructTy = dyn_cast<StructType>(Param))
    return HandleClassification{BufferKind::Uniform, 0, StructTy};
  return std::nullopt;
}

/// SPIR-V's `Dim` operand value for `OpTypeImage Buffer` (see
/// `feme::spirv::getImageIntParams` in MLIR's SPIRVToLLVM.cpp, whose six
/// integer parameters -- `[Dim, Depth, Arrayed, MS, Sampled, Format]` -- a
/// converted `spirv.Image`/`spirv.SignedImage` handle carries unchanged).
constexpr unsigned SPIRVDimBuffer = 5;
/// The `Sampled` operand's "used without a sampler" value: a storage texel
/// buffer (`RWBuffer<T>` in HLSL), accessed through `OpImageRead`/
/// `OpImageWrite` and writable.
constexpr unsigned SPIRVSampledWithoutSampler = 2;
/// The `Sampled` operand's "used with a sampler" value: a uniform texel
/// buffer (`Buffer<T>` in HLSL), accessed through `OpImageFetch` and
/// read-only.
constexpr unsigned SPIRVSampledWithSampler = 1;

/// Returns \p Handle's buffer classification if its type is a `Dim::Buffer`
/// Returns whether \p Ty is `<4 x float>`, the only shader-side element
/// shape the CPU runtime's typed-load/store helpers implement a format
/// conversion for today (see femeCpuResourceLoadTypedV4F32/StoreTypedV4F32
/// in feme/runtime/CPU/FeMeRuntimeCPU.c).
bool isSupportedTexelElementType(Type *Ty) {
  auto *VecTy = dyn_cast<FixedVectorType>(Ty);
  return VecTy && VecTy->getNumElements() == 4 &&
         VecTy->getElementType()->isFloatTy();
}

/// Returns \p Handle's buffer classification if its type is a `Dim::Buffer`
/// `target("spirv.Image", ElemTy, [Dim, Depth, Arrayed, MS, Sampled,
/// Format])` handle. `ElemTy` here is SPIR-V's own per-*channel* sampled
/// type (`OpTypeImage`'s "Sampled Type" operand, e.g. `f32` for any
/// floating-point-format image, never a vector) -- the shader-visible
/// `<4 x float>` texel width this milestone actually requires (see the
/// header comment's texel-buffer scope note) shows up only at each
/// `OpImageRead`/`OpImageFetch`/`OpImageWrite`'s own load/store type, so
/// `hasOnlySupportedUses` checks that instead of anything recorded here.
/// `Sampled == 0` ("runtime known") is ambiguous and rejected rather than
/// guessed at. Returns `std::nullopt` for any other handle kind.
std::optional<HandleClassification>
classifyTexelBufferHandle(const CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || (HandleTy->getName() != "spirv.Image" &&
                    HandleTy->getName() != "spirv.SignedImage"))
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1 ||
      HandleTy->getNumIntParameters() != 6)
    return std::nullopt;
  if (HandleTy->getIntParameter(0) != SPIRVDimBuffer)
    return std::nullopt;

  Type *ChannelType = HandleTy->getTypeParameter(0);
  unsigned Sampled = HandleTy->getIntParameter(4);
  if (Sampled == SPIRVSampledWithoutSampler)
    return HandleClassification{BufferKind::TexelStorage, 0, nullptr,
                                ChannelType};
  if (Sampled == SPIRVSampledWithSampler)
    return HandleClassification{BufferKind::TexelUniform, 0, nullptr,
                                ChannelType};
  return std::nullopt;
}

/// Checks that every use of \p Handle is the flat access shape this pass
/// models for \p Kind: a `llvm.spv.resource.getpointer` call whose own
/// result is used only by an ordinary `load` (both kinds), or a `store` it
/// is the pointer operand (not the stored value) of (`BufferKind::Storage`/
/// `TexelStorage` only -- a uniform/texel-uniform buffer is always
/// read-only, matching Vulkan's own restriction on
/// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_UNIFORM_TEXEL_BUFFER`) -- see the
/// header comment's "access shape" bullet. For `BufferKind::Uniform`, the
/// `getpointer` index (the field selected within the block's struct) must
/// also be a compile-time constant, unlike a storage buffer's (possibly
/// dynamic) array index -- a real cbuffer field access is always
/// statically typed. For a texel-buffer kind, every load's result type (or
/// store's stored-value type) must be exactly `<4 x float>`
/// (`isSupportedTexelElementType`) -- see `classifyTexelBufferHandle`'s
/// comment for why that check belongs here rather than on the handle type.
/// Any further `getelementptr` into the element's own fields (a
/// structured-buffer field access, or a nested uniform-buffer field) is
/// left unmodeled, matching
/// `feme::cpu::ResourceLoweringPass::hasOnlySupportedUses`'s own narrowing.
bool hasOnlySupportedUses(const CallInst &Handle, BufferKind Kind) {
  bool Writable =
      Kind == BufferKind::Storage || Kind == BufferKind::TexelStorage;
  bool IsTexel = isTexelBufferKind(Kind);
  for (const User *U : Handle.users()) {
    const auto *GetPtr = dyn_cast<CallInst>(U);
    if (!GetPtr || getIntrinsicID(GetPtr) != Intrinsic::spv_resource_getpointer)
      return false;
    if (Kind == BufferKind::Uniform &&
        !isa<ConstantInt>(GetPtr->getArgOperand(1)))
      return false;
    for (const User *PU : GetPtr->users()) {
      if (const auto *LI = dyn_cast<LoadInst>(PU)) {
        if (IsTexel && !isSupportedTexelElementType(LI->getType()))
          return false;
        continue;
      }
      if (Writable) {
        if (const auto *SI = dyn_cast<StoreInst>(PU)) {
          if (SI->getPointerOperand() != GetPtr)
            return false;
          if (IsTexel &&
              !isSupportedTexelElementType(SI->getValueOperand()->getType()))
            return false;
          continue;
        }
      }
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

    std::optional<HandleClassification> Classification =
        classifyVulkanBufferHandle(*CI, DL);
    if (!Classification)
      Classification = classifyTexelBufferHandle(*CI);
    if (!Classification)
      return std::nullopt; // Not one of the kinds this pass normalizes.
    if (!hasOnlySupportedUses(*CI, Classification->Kind))
      return std::nullopt;

    auto *SetC = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    auto *BindingC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    if (!SetC || !BindingC)
      return std::nullopt; // Non-constant binding: not produced today.

    // The array range size, unlike the array index below, must be a
    // compile-time constant: it is part of the (set, binding) identity
    // itself, exactly like DXIL's `handlefrombinding` range-size operand
    // (see `feme::cpu::BoundResourceNormalizationPass::collectBoundHandles`).
    auto *RangeSizeC = dyn_cast<ConstantInt>(CI->getArgOperand(2));
    if (!RangeSizeC)
      return std::nullopt; // Non-constant range size: not produced today.
    uint32_t RangeSize = static_cast<uint32_t>(RangeSizeC->getZExtValue());
    if (RangeSize == 0)
      return std::nullopt; // Unbounded range: see the header comment.

    RangeKey Key{static_cast<uint32_t>(SetC->getZExtValue()),
                 static_cast<uint32_t>(BindingC->getZExtValue())};
    Handles.push_back(BoundHandle{CI, Key, Classification->Kind,
                                  Classification->Stride,
                                  Classification->ElementStruct,
                                  Classification->TexelElementType, RangeSize});
  }
  return Handles;
}

/// Assigns each non-conflicting identity a contiguous run of
/// `Entry.RangeSize` heap slots, sorted by identity for a deterministic
/// layout -- mirroring `feme::cpu::BoundResourceNormalizationPass`'s own
/// `assignHeapBases` (roadmap R26 generalized this pass from an implicit
/// range size of 1, see the header comment). Returns the total reserved
/// prefix size.
uint32_t assignHeapBases(std::map<RangeKey, RangeEntry> &Ranges) {
  uint32_t Base = 0;
  for (auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    Entry.HeapBase = Base;
    Base += Entry.RangeSize;
  }
  return Base;
}

/// Builds `select(Base + Index > UINT32_MAX, UINT32_MAX, Base + Index)`,
/// computed in i64 so the overflow itself can be detected exactly --
/// duplicated from `feme::cpu::BoundResourceNormalizationPass`'s own helper
/// of the same name (matching how `addResourceEnvParams` below is already a
/// separate copy of that pass's `addResourceEnvParams`, per this file's own
/// header comment).
Value *computeOverflowClampedIndex(IRBuilderBase &Builder, Value *Index,
                                   uint32_t Base) {
  LLVMContext &Ctx = Builder.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);

  Value *Sum64 = Builder.CreateAdd(ConstantInt::get(I64Ty, Base),
                                   Builder.CreateZExt(Index, I64Ty));
  Value *Overflow = Builder.CreateICmpUGT(
      Sum64, ConstantInt::get(I64Ty, std::numeric_limits<uint32_t>::max()));
  return Builder.CreateSelect(
      Overflow, ConstantInt::get(I32Ty, std::numeric_limits<uint32_t>::max()),
      Builder.CreateTrunc(Sum64, I32Ty));
}

/// Builds `select(OutOfRange, UINT32_MAX, Base + Index)`, using
/// `computeOverflowClampedIndex` for the addition itself: `Index` is
/// unsigned and compared against \p RangeSize first, so only a range this
/// large ever exercises the overflow path in practice, but the design
/// requires both checks (see "Bound-resource normalization" in
/// feme/docs/FeMeCPUDesign.md).
Value *computeClampedIndex(IRBuilderBase &Builder, Value *Index, uint32_t Base,
                           uint32_t RangeSize) {
  Type *I32Ty = Type::getInt32Ty(Builder.getContext());
  Value *OutOfRange =
      Builder.CreateICmpUGE(Index, ConstantInt::get(I32Ty, RangeSize));
  Value *Clamped = computeOverflowClampedIndex(Builder, Index, Base);
  return Builder.CreateSelect(
      OutOfRange, ConstantInt::get(I32Ty, std::numeric_limits<uint32_t>::max()),
      Clamped);
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
/// corresponding canonical `feme.cpu.resource.*` call, using \p Env and the
/// range-checked heap index `HeapBase + clamp(Index, BH.RangeSize)` (see
/// `computeClampedIndex` and the header comment's roadmap R26 note).
/// `Index` is re-read from \p BH.Handle's own operand here rather than
/// cached in `BoundHandle` -- see that struct's comment for why. Computed
/// once, at \p BH.Handle's own location -- which dominates every use
/// rewritten below -- rather than once per access.
///
/// The access itself differs by \p BH.Kind: a storage-buffer access
/// multiplies its `getpointer` array index (re-read per call, since --
/// unlike the descriptor index above -- a distinct array element may be
/// read per access) by \p BH.Stride and goes through
/// `feme::cpu::createRawLoad`/`createRawStore`; a uniform-buffer access
/// resolves its `getpointer` field index (a compile-time constant,
/// guaranteed by `hasOnlySupportedUses`) directly to \p BH.ElementStruct's
/// own declared byte offset for that field, also through the raw family --
/// no runtime arithmetic needed at all, since a cbuffer's fields have no
/// dynamic index the way a storage buffer's array elements do. A texel
/// buffer access (`isTexelBufferKind(BH.Kind)`) needs no byte-offset
/// arithmetic either: its `getpointer` "index" is already the image
/// coordinate `OpImageRead`/`OpImageFetch`/`OpImageWrite` themselves address
/// by, so it goes through `feme::cpu::createTypedLoad`/`createTypedStore`
/// directly, letting the CPU runtime's format conversion (keyed off the
/// bound `FemeDescriptor::Format`) do the rest.
void lowerAccesses(const BoundHandle &BH, const ResourceCallEnv &Env,
                   uint32_t HeapBase) {
  LLVMContext &Ctx = BH.Handle->getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  IRBuilder<> IndexBuilder(BH.Handle);
  Value *Index = BH.Handle->getArgOperand(3);
  Value *DescriptorIndex =
      computeClampedIndex(IndexBuilder, Index, HeapBase, BH.RangeSize);
  Value *Mask = ConstantInt::getTrue(Ctx);
  const DataLayout &DL = BH.Handle->getModule()->getDataLayout();
  bool IsTexel = isTexelBufferKind(BH.Kind);

  for (User *U : llvm::make_early_inc_range(BH.Handle->users())) {
    auto *GetPtr = cast<CallInst>(U);
    Value *ElementIndex = nullptr;
    Value *Offset = nullptr;
    if (IsTexel) {
      IRBuilder<> PtrBuilder(GetPtr);
      ElementIndex = PtrBuilder.CreateZExt(GetPtr->getArgOperand(1), I64Ty);
    } else if (BH.Kind == BufferKind::Storage) {
      IRBuilder<> PtrBuilder(GetPtr);
      Value *ElemIdx = PtrBuilder.CreateZExt(GetPtr->getArgOperand(1), I64Ty);
      Offset =
          PtrBuilder.CreateMul(ElemIdx, ConstantInt::get(I64Ty, BH.Stride));
    } else {
      auto *FieldIdxC = cast<ConstantInt>(GetPtr->getArgOperand(1));
      const StructLayout *SL = DL.getStructLayout(BH.ElementStruct);
      uint64_t ByteOffset = SL->getElementOffset(FieldIdxC->getZExtValue());
      Offset = ConstantInt::get(I64Ty, ByteOffset);
    }

    for (User *PU : llvm::make_early_inc_range(GetPtr->users())) {
      if (auto *LI = dyn_cast<LoadInst>(PU)) {
        IRBuilder<> Builder(LI);
        CallInst *Loaded =
            IsTexel
                ? createTypedLoad(Builder, Env, DescriptorIndex, ElementIndex,
                                  Mask, LI->getType(), LI->getName())
                : createRawLoad(Builder, Env, DescriptorIndex, Offset, Mask,
                                LI->getType(), LI->getName());
        LI->replaceAllUsesWith(Loaded);
        LI->eraseFromParent();
        continue;
      }
      // Only reachable for BufferKind::Storage/TexelStorage.
      auto *SI = cast<StoreInst>(PU);
      IRBuilder<> Builder(SI);
      if (IsTexel)
        createTypedStore(Builder, Env, DescriptorIndex, ElementIndex,
                         SI->getValueOperand(), Mask);
      else
        createRawStore(Builder, Env, DescriptorIndex, Offset,
                       SI->getValueOperand(), Mask);
      SI->eraseFromParent();
    }
    GetPtr->eraseFromParent();
  }
  BH.Handle->eraseFromParent();
}

/// Attaches the `!feme.cpu.resources` metadata node
/// `feme::cpu::ResourceInfo::fromModule` reads: name, \p RootConstantSize
/// (V3: a SPIR-V push-constant access `lowerFunctionResources` below found
/// and lowered through this same function's already-added
/// `root_constants`/`root_constant_size` parameters, or 0 if it has none --
/// see `feme::cpu::matchSPIRVPushConstantAccess`), whether the sampler heap
/// is used (always false -- no SPIR-V sampler handle is normalized by this
/// pass), a root-constant binding (always `(space0, register0)`: a SPIR-V
/// push-constant block has no register identity of its own, unlike DXIL's
/// register-bound root constant, so there is nothing else to report), and
/// an empty statically-known-heap-index tail. That tail always stays empty
/// here regardless of whether a given access went through a compile-time-
/// constant or (roadmap R26) dynamic array index -- it is
/// `feme::cpu::ResourceLoweringPass`'s own dynamic-heap discovery
/// mechanism, unrelated to the bound-range assignment
/// `attachBoundResourceMetadata` below records unconditionally (see "Heap
/// usage discovery" in feme/docs/FeMeCPUDesign.md).
void attachResourceMetadata(Function &F, uint32_t RootConstantSize) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Metadata *Ops[] = {
      MDString::get(Ctx, F.getName()),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSize)),
      ConstantAsMetadata::get(ConstantInt::getFalse(Ctx)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0))};
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
/// correspondence, and range-size the binding's own declared descriptor
/// array count (roadmap R26 generalized this from an implicit 1).
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
    Ops.push_back(
        ConstantAsMetadata::get(ConstantInt::get(I32Ty, Entry.RangeSize)));
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
  // identity's element stride/range size across the whole module, before
  // rewriting anything -- a conflicting re-declaration can only be detected
  // once all of them are known (see
  // `feme::cpu::BoundResourceNormalizationPass`'s own two-phase shape).
  DenseMap<Function *, SmallVector<BoundHandle, 4>> PerFunctionHandles;
  std::map<RangeKey, RangeEntry> Ranges;
  for (Function &F : M) {
    std::optional<SmallVector<BoundHandle, 4>> Handles = collectHandles(F);
    if (!Handles || Handles->empty())
      continue;
    for (const BoundHandle &BH : *Handles) {
      auto It = Ranges.find(BH.Key);
      if (It == Ranges.end())
        Ranges.emplace(BH.Key, RangeEntry{BH.Kind, BH.Stride, BH.ElementStruct,
                                          BH.TexelElementType, BH.RangeSize,
                                          /*Conflicting=*/false});
      else if (It->second.Kind != BH.Kind || It->second.Stride != BH.Stride ||
               It->second.ElementStruct != BH.ElementStruct ||
               It->second.TexelElementType != BH.TexelElementType ||
               It->second.RangeSize != BH.RangeSize)
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

    // A function with its own bound-resource access already has
    // `root_constants`/`root_constant_size` parameters from
    // `addResourceEnvParams` above -- reuse them for a push-constant access
    // too, rather than leaving `feme::cpu::SPIRVPushConstantLoweringPass`
    // to add a second, colliding pair (see that pass's header comment's
    // "combined case").
    uint32_t RootConstantSize = 0;
    if (std::optional<SPIRVPushConstantAccess> PCAccess =
            matchSPIRVPushConstantAccess(*NewF))
      RootConstantSize = lowerSPIRVPushConstantAccess(
          *PCAccess, Env.RootConstants, Env.RootConstantSize);

    attachResourceMetadata(*NewF, RootConstantSize);
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
