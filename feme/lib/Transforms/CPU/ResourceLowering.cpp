//===- ResourceLowering.cpp - CPU target resource canonicalization -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceLowering.h"

#include "feme/Transforms/CPU/ImageCalls.h"
#include "feme/Transforms/CPU/ResourceCalls.h"
#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "llvm/ADT/STLExtras.h"
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
  ParamTypes.append(
      {PtrTy, I32Ty, PtrTy, I32Ty, PtrTy, I32Ty, PtrTy, I32Ty});

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

/// A `dx.Texture` handle originating from a `handlefromheap` call, only for
/// the one dimension `runtime/CPU` actually implements sampling/loading for
/// (see the file header comment's scope note).
struct ImageHandle {
  CallInst *HandleFromHeap;
};

/// Recognizes \p V as a 2D `dx.Texture` handle from the descriptor heap, or
/// returns `std::nullopt` for any other shape (a different dimension, a
/// register-bound handle, or not a handle at all) -- left unraised rather
/// than erroring, exactly like every other not-yet-covered shape in this
/// file.
std::optional<ImageHandle> classifyImageHandle(Value *V) {
  auto *CI = dyn_cast<CallInst>(V);
  if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefromheap)
    return std::nullopt;
  auto *Ty = dyn_cast<dxil::TextureExtType>(CI->getType());
  if (!Ty || Ty->getDimension() != dxil::ResourceKind::Texture2D)
    return std::nullopt;
  return ImageHandle{CI};
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

/// Returns whether \p Offset is a compile-time-zero constant: the only
/// texel-offset value `lowerImageAccesses` currently lowers, since
/// `runtime/CPU`'s sampling/loading helpers do not yet accept one (see
/// feme/docs/FeMeGraphicsDesign.md's "Canonical image operations" --
/// offsets are scoped out of roadmap R30's initial implementation). A
/// nonzero or non-constant offset is left unraised rather than silently
/// dropped, since dropping it would be a real (if rare) semantic change.
bool isZeroOffset(const Value *Offset) {
  const auto *C = dyn_cast<Constant>(Offset);
  return C && C->isNullValue();
}

/// Lowers every `llvm.dx.resource.sample`/`samplelevel`/`load.level` call in
/// \p F whose handle operand(s) `classifyImageHandle`/`classifySamplerHandle`
/// recognize into the corresponding canonical `feme.cpu.image.*` call (see
/// ImageCalls.h), using \p Env's image/sampler heap operands. An access this
/// function cannot model (a texture dimension other than 2D, a non-constant
/// or nonzero offset, an unrecognized handle) is left unraised, independent
/// of every other access in \p F -- unlike the buffer path, this is safe on
/// a per-access basis because an unlowered `llvm.dx.resource.*` call remains
/// valid IR on its own (it is simply not retargetable to the CPU target
/// until a later increment covers it), rather than half of a handle's
/// worth of accesses being rewritten out from under the other half.
bool lowerImageAccesses(Function &F, const ImageCallEnv &Env) {
  bool Changed = false;
  for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    Intrinsic::ID ID = getIntrinsicID(CI);
    bool IsSample = ID == Intrinsic::dx_resource_sample;
    bool IsSampleLevel = ID == Intrinsic::dx_resource_samplelevel;
    bool IsLoadLevel = ID == Intrinsic::dx_resource_load_level;
    if (!IsSample && !IsSampleLevel && !IsLoadLevel)
      continue;

    IRBuilder<> Builder(CI);
    Value *Mask = Builder.getTrue();

    if (IsLoadLevel) {
      // (handle, coord, level, offset) -> <4 x float>.
      std::optional<ImageHandle> Img =
          classifyImageHandle(CI->getArgOperand(0));
      if (!Img || !isZeroOffset(CI->getArgOperand(3)))
        continue;
      Value *Coord = CI->getArgOperand(1);
      Value *X = Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *Y = Builder.CreateExtractElement(Coord, uint64_t{1});
      Value *ImageIndex = Img->HandleFromHeap->getArgOperand(0);
      CallInst *NewCall = createLoad2D(Builder, Env, ImageIndex, X, Y,
                                       CI->getArgOperand(2), Mask,
                                       CI->getName());
      CI->replaceAllUsesWith(NewCall);
      CI->eraseFromParent();
      Changed = true;
      continue;
    }

    // Sample/SampleLevel: (handle, sampler, coord, [lod,] offset).
    std::optional<ImageHandle> Img = classifyImageHandle(CI->getArgOperand(0));
    std::optional<CallInst *> Sampler =
        classifySamplerHandle(CI->getArgOperand(1));
    unsigned OffsetIdx = IsSample ? 3 : 4;
    if (!Img || !Sampler || !isZeroOffset(CI->getArgOperand(OffsetIdx)))
      continue;

    Value *Coord = CI->getArgOperand(2);
    Value *U = Builder.CreateExtractElement(Coord, uint64_t{0});
    Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
    Value *Lod = IsSampleLevel ? CI->getArgOperand(3)
                              : ConstantFP::get(Builder.getFloatTy(), 0.0);
    Value *UseExplicitLod = Builder.getInt1(IsSampleLevel);
    Value *ImageIndex = Img->HandleFromHeap->getArgOperand(0);
    Value *SamplerIndex = (*Sampler)->getArgOperand(0);

    CallInst *NewCall =
        createSample2D(Builder, Env, ImageIndex, SamplerIndex, U, V, Lod,
                       UseExplicitLod, Mask, CI->getName());
    CI->replaceAllUsesWith(NewCall);
    CI->eraseFromParent();
    Changed = true;
  }
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
    bool IsSample = ID == Intrinsic::dx_resource_sample;
    bool IsSampleLevel = ID == Intrinsic::dx_resource_samplelevel;
    bool IsLoadLevel = ID == Intrinsic::dx_resource_load_level;
    if (!IsSample && !IsSampleLevel && !IsLoadLevel)
      continue;

    // Mirrors `lowerImageAccesses`'s own eligibility check exactly (without
    // mutating anything): a call this pass cannot actually rewrite (an
    // unsupported dimension, a non-constant/nonzero offset, ...) must not
    // count here, or the signature would grow every time this read-only
    // scan runs, including on the *already-grown* replacement function
    // `ResourceLoweringPass::run`'s module-function loop may revisit for a
    // handle it left deliberately unrewritten (see the header comment's
    // "per-access" note).
    if (IsLoadLevel) {
      if (classifyImageHandle(CI->getArgOperand(0)) &&
          isZeroOffset(CI->getArgOperand(3)))
        return true;
      continue;
    }
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
                                 RootConstantMetadata &RootConstant) {
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
    lowerImageAccesses(*NewF, ImgEnv);
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
/// `lowerFunctionResources`), whether the sampler heap is used (always
/// false -- `feme::dxil::OpRaisingPass` does not yet reconstruct a sampler
/// handle from the heap, see `raiseResourceHandleFromHeap`'s comment), \p
/// RootConstant's binding (space/register, both 0 if \p F has none), and
/// the sorted, deduplicated statically-known heap indices the shader reads
/// through a constant descriptor index.
void attachResourceMetadata(Function &F,
                            SmallVectorImpl<uint32_t> &StaticHeapIndices,
                            const RootConstantMetadata &RootConstant) {
  llvm::sort(StaticHeapIndices);
  StaticHeapIndices.erase(llvm::unique(StaticHeapIndices),
                          StaticHeapIndices.end());

  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Metadata *, 8> Ops;
  Ops.push_back(MDString::get(Ctx, F.getName()));
  Ops.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstant.Size)));
  Ops.push_back(ConstantAsMetadata::get(ConstantInt::getFalse(Ctx)));
  Ops.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstant.Space)));
  Ops.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstant.Register)));
  for (uint32_t Idx : StaticHeapIndices)
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, Idx)));

  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.resources")
      ->addOperand(MDNode::get(Ctx, Ops));
}

} // namespace

PreservedAnalyses ResourceLoweringPass::run(Module &M,
                                            ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    SmallVector<uint32_t, 4> StaticHeapIndices;
    RootConstantMetadata RootConstant;
    Function *NewF = lowerFunctionResources(F, StaticHeapIndices, RootConstant);
    if (!NewF)
      continue;
    Changed = true;
    attachResourceMetadata(*NewF, StaticHeapIndices, RootConstant);
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
