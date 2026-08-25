//===- SPIRVResourceLowering.cpp - SPIR-V bound resource emulation -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVResourceLowering.h"

#include "feme/Target/CPU/ResourceInfo.h"

#include "feme/Transforms/CPU/ImageCalls.h"
#include "feme/Transforms/CPU/ResourceCalls.h"
#include "feme/Transforms/CPU/SPIRVPushConstantLowering.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
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

/// Which kind of resource a bound handle wraps.
///
/// The first five are buffers -- a storage buffer -- a homogeneous,
/// dynamically-indexed runtime array (`RWStructuredBuffer<T>`/
/// `StructuredBuffer<T>`) -- a uniform buffer -- a fixed set of
/// differently-typed named fields at fixed byte offsets
/// (`cbuffer`/`ConstantBuffer<T>`) -- a uniform buffer array -- a uniform
/// block whose sole field is itself a fixed-size, homogeneous array
/// (roadmap F12a's own `layout(std140) uniform Input { uint data[16]; }`
/// shape) -- or (V4) a texel buffer -- a `Buffer<T>`/`RWBuffer<T>`-shaped,
/// format-converting view over a `Dim::Buffer` SPIR-V image. Each needs its
/// own offset arithmetic (see `lowerAccesses`): a storage buffer access
/// multiplies a (possibly dynamic) array index by a fixed element stride, a
/// uniform buffer access resolves a (always compile-time-constant) field
/// index directly to a fixed struct-layout byte offset, a uniform buffer
/// *array* access multiplies a (possibly dynamic) array index by a fixed
/// stride exactly like a storage buffer's own -- except that stride is
/// carried explicitly on the handle type itself (see
/// `classifyVulkanBufferHandle`'s comment), since a std140 array's stride
/// (always a multiple of 16 bytes) need not equal its element's own natural
/// size the way a std430 storage buffer array's always does -- and a texel
/// buffer access converts through its format at a fixed element index with
/// no byte-offset arithmetic of its own (see
/// `feme::cpu::createTypedLoad`/`createTypedStore` in ResourceCalls.h).
///
/// The last two are the image and sampler halves of a texture sample
/// (roadmap R30's SPIR-V completion): they live in the *image* and
/// *sampler* heaps rather than the buffer-oriented resource heap, and their
/// accesses lower to `feme.cpu.image.*` rather than `feme.cpu.resource.*`
/// (see ImageCalls.h).
enum class HandleKind {
  Storage,
  Uniform,
  UniformArray,
  TexelStorage,
  TexelUniform,
  SampledImage2D,
  Sampler
};

/// Whether \p Kind is one of the two texel-buffer kinds (see `HandleKind`).
bool isTexelHandleKind(HandleKind Kind) {
  return Kind == HandleKind::TexelStorage || Kind == HandleKind::TexelUniform;
}

/// Whether \p Kind's accesses go through `feme.cpu.resource.*` (every
/// buffer kind) rather than `feme.cpu.image.*`.
bool isBufferHandleKind(HandleKind Kind) {
  return Kind != HandleKind::SampledImage2D && Kind != HandleKind::Sampler;
}

/// The heap \p Kind's descriptors are assigned slots in.
BoundResourceClass getResourceClass(HandleKind Kind) {
  switch (Kind) {
  case HandleKind::Storage:
  case HandleKind::Uniform:
  case HandleKind::UniformArray:
  case HandleKind::TexelStorage:
  case HandleKind::TexelUniform:
    return BoundResourceClass::Buffer;
  case HandleKind::SampledImage2D:
    return BoundResourceClass::Image;
  case HandleKind::Sampler:
    return BoundResourceClass::Sampler;
  }
  llvm_unreachable("unhandled HandleKind");
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
  HandleKind Kind = HandleKind::Storage;
  /// The storage-buffer element stride (`Kind == Storage`); unused, always
  /// 0, for a uniform buffer, whose offsets come from `ElementStruct`'s own
  /// layout instead.
  uint64_t Stride = 0;
  /// The uniform-buffer field struct (`Kind == Uniform`); null for a
  /// storage buffer.
  StructType *ElementStruct = nullptr;
  /// The texel-buffer shader-side element type (`isTexelHandleKind(Kind)`);
  /// null otherwise. This is the scalar per-*channel* type
  /// `classifyTexelBufferHandle` reads from the handle (`f32` or, V4,
  /// `i32`), used only to detect a conflicting re-declaration of the same
  /// binding below -- not the `<4 x T>` vector type an actual load/store
  /// uses, which `isSupportedTexelElementType` checks directly against each
  /// access instead (see that function's comment).
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
  HandleKind Kind;
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
/// `HandleKind`).
struct HandleClassification {
  HandleKind Kind;
  uint64_t Stride = 0;
  StructType *ElementStruct = nullptr;
  Type *TexelElementType = nullptr;
};

/// Returns \p Handle's buffer classification if its type is a
/// `spirv.VulkanBuffer` handle over a flat (non-aggregate-accessed) element
/// -- see `feme::spirv::convertBufferBlockType`/`convertUniformBlockType`
/// in SPIRVToLLVMPatterns.cpp for the handle shapes this recognizes: one
/// type parameter (a storage buffer's `!llvm.array<0 x ElemTy>` runtime
/// array, a uniform buffer's own field struct directly, or a uniform
/// buffer array's own `!llvm.array<0 x ElemTy>` marker array -- the same
/// shape a storage buffer's own uses) and either two integer parameters
/// (storage class, writability) or, for a uniform buffer array only, three
/// (storage class, writability, and its own explicit `ArrayStride`).
///
/// A storage buffer's own stride is never carried explicitly: SPIR-V
/// records it implicitly via `ElemTy`'s own store size instead, mirroring
/// how `feme::cpu::ResourceLoweringPass::classifyHandle` recovers a DXIL
/// `dx.RawBuffer`'s stride from its element type parameter -- valid because
/// a std430 storage buffer's own `ArrayStride` always equals its element's
/// natural size. A std140 uniform buffer array's own `ArrayStride` need
/// not (every array widens its element to a 16-byte multiple regardless of
/// the element's own size -- e.g. a scalar `uint`'s 4-byte size against a
/// 16-byte stride, the shape roadmap F12a's own CTS case hits), so its
/// real stride has nowhere else to come from and is carried as that third
/// integer parameter instead (see `feme::spirv::convertUniformBlockType`'s
/// own comment for why the marker array itself cannot carry it). The two
/// shapes are otherwise indistinguishable from `Param`'s own type alone,
/// which is why the parameter count -- not `ElemTy` -- is what
/// distinguishes them here.
///
/// Returns `std::nullopt` for any other handle kind (an image/sampler
/// resource, not yet covered -- see the header comment).
std::optional<HandleClassification>
classifyVulkanBufferHandle(const CallInst &Handle, const DataLayout &DL) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "spirv.VulkanBuffer")
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1)
    return std::nullopt;
  Type *Param = HandleTy->getTypeParameter(0);
  if (auto *ArrayTy = dyn_cast<ArrayType>(Param)) {
    if (HandleTy->getNumIntParameters() > 2)
      return HandleClassification{HandleKind::UniformArray,
                                  HandleTy->getIntParameter(2), nullptr};
    return HandleClassification{HandleKind::Storage,
                                DL.getTypeStoreSize(ArrayTy->getElementType()),
                                nullptr};
  }
  if (auto *StructTy = dyn_cast<StructType>(Param))
    return HandleClassification{HandleKind::Uniform, 0, StructTy};
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
/// Returns whether \p Ty is `<4 x float>` or (V4) `<4 x i32>`, the shader-
/// side element shapes the CPU runtime's typed-load/store helpers
/// implement a format conversion for today (see
/// femeCpuResourceLoadTypedV4F32/StoreTypedV4F32 and
/// femeCpuResourceLoadTypedV4I32/StoreTypedV4I32 in
/// feme/runtime/CPU/FeMeRuntimeCPU.c). A scalar element (a single-channel
/// format's own shader-visible type, e.g. plain `i32`) is not yet modeled:
/// SPIR-V's `OpImageRead`/`OpImageFetch` always return a full four-component
/// vector regardless of the underlying format's real channel count (see
/// classifyTexelBufferHandle's comment), so supporting it needs the
/// physically-narrower-than-`<4 x T>` per-format padding this milestone does
/// not add -- see FeMeVulkanDesign.md's V4 status note.
bool isSupportedTexelElementType(Type *Ty) {
  auto *VecTy = dyn_cast<FixedVectorType>(Ty);
  if (!VecTy || VecTy->getNumElements() != 4)
    return false;
  Type *ElemTy = VecTy->getElementType();
  return ElemTy->isFloatTy() || ElemTy->isIntegerTy(32);
}

/// Whether \p Ty is a shape `feme::cpu::mangleResourceCallName`'s own
/// `appendScalarMangling` (ResourceCalls.cpp) can mangle: a scalar
/// half/float/double/integer, or a fixed vector of one. A storage/uniform
/// buffer load or store of anything else -- most notably a SPIR-V
/// aggregate (array or struct) value, e.g. `OpSelect`'s result when its
/// operands are themselves arrays -- is left unclassified by
/// `hasOnlySupportedUses` below rather than reaching
/// `createRawLoad`/`createRawStore` and hitting `appendScalarMangling`'s own
/// `llvm_unreachable` (`dEQP-VK.spirv_assembly.instruction.spirv1p4.
/// opselect.array_select`'s own crash).
bool isSupportedRawElementType(Type *Ty) {
  Type *ElemTy = Ty;
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty))
    ElemTy = VecTy->getElementType();
  return ElemTy->isHalfTy() || ElemTy->isFloatTy() || ElemTy->isDoubleTy() ||
         ElemTy->isIntegerTy();
}

/// Returns \p Handle's buffer classification if its type is a `Dim::Buffer`
/// `target("spirv.Image", ElemTy, [Dim, Depth, Arrayed, MS, Sampled,
/// Format])` handle. `ElemTy` here is SPIR-V's own per-*channel* sampled
/// type (`OpTypeImage`'s "Sampled Type" operand, e.g. `f32`/`i32` for any
/// floating-point-/integer-format image, never a vector) -- the
/// shader-visible `<4 x T>` texel width this milestone actually requires
/// (see the header comment's texel-buffer scope note) shows up only at each
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
    return HandleClassification{HandleKind::TexelStorage, 0, nullptr,
                                ChannelType};
  if (Sampled == SPIRVSampledWithSampler)
    return HandleClassification{HandleKind::TexelUniform, 0, nullptr,
                                ChannelType};
  return std::nullopt;
}

/// SPIR-V's `Dim` operand value for `OpTypeImage 2D`.
constexpr unsigned SPIRVDim2D = 1;

/// Returns \p Handle's classification if its type is a single-sampled,
/// non-arrayed, floating-point or 32-bit-integer 2D `spirv.Image`/
/// `spirv.SignedImage` handle used *with* a sampler -- the one image shape
/// `runtime/CPU`'s sampling/fetch helpers implement (see ImageCalls.h's own
/// scope note). Every other dimension, an arrayed or multisampled image,
/// and a storage image (`Sampled == 2`, which would need a
/// `feme.cpu.image.store.*` helper that does not exist yet) return
/// `std::nullopt`. An integer-channel handle is classified the same as a
/// float one here -- `hasOnlySupportedImageUses` (roadmap E26) is what
/// narrows its *uses* to fetch only, since SPIR-V never legalizes a
/// filtered sample against an integer-sampled image.
std::optional<HandleClassification>
classifySampledImage2DHandle(const CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || (HandleTy->getName() != "spirv.Image" &&
                    HandleTy->getName() != "spirv.SignedImage"))
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1 ||
      HandleTy->getNumIntParameters() != 6)
    return std::nullopt;
  if (HandleTy->getIntParameter(0) != SPIRVDim2D)
    return std::nullopt;
  // [Dim, Depth, Arrayed, MS, Sampled, Format]: an arrayed or multisampled
  // image needs a layer/sample coordinate the 2D helpers do not take.
  if (HandleTy->getIntParameter(2) != 0 || HandleTy->getIntParameter(3) != 0)
    return std::nullopt;
  if (HandleTy->getIntParameter(4) != SPIRVSampledWithSampler)
    return std::nullopt;

  Type *ChannelType = HandleTy->getTypeParameter(0);
  if (!ChannelType->isFloatTy() && !ChannelType->isIntegerTy(32))
    return std::nullopt; // No other channel shape is decodable today.
  return HandleClassification{HandleKind::SampledImage2D, 0, nullptr,
                              ChannelType};
}

/// Returns \p Handle's classification if its type is a `spirv.Sampler`
/// handle (`feme::spirv::convertSamplerType` in SPIRVToLLVMPatterns.cpp
/// gives it no parameters at all -- a sampler's own state lives entirely in
/// the `FemeSamplerDescriptor` the host binds, never in the shader's type).
std::optional<HandleClassification>
classifySamplerHandle(const CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "spirv.Sampler")
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 0 ||
      HandleTy->getNumIntParameters() != 0)
    return std::nullopt;
  return HandleClassification{HandleKind::Sampler, 0, nullptr, nullptr};
}

/// Whether \p CI is one of the two SPIR-V sample intrinsics this pass
/// lowers, setting \p ExplicitLod for `samplelevel`.
bool isSampleIntrinsic(const CallInst &CI, bool &ExplicitLod) {
  Intrinsic::ID ID = getIntrinsicID(&CI);
  if (ID == Intrinsic::spv_resource_sample) {
    ExplicitLod = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_samplelevel) {
    ExplicitLod = true;
    return true;
  }
  return false;
}

/// Whether \p Ty is `<N x ElemTy>`.
bool isVectorOf(const Type *Ty, unsigned N, bool (Type::*Is)() const) {
  const auto *VecTy = dyn_cast<FixedVectorType>(Ty);
  return VecTy && VecTy->getNumElements() == N &&
         (VecTy->getElementType()->*Is)();
}

/// Whether \p Ty is the `<4 x float>` texel every `feme.cpu.image.*` color
/// operation produces.
bool isV4F32(const Type *Ty) { return isVectorOf(Ty, 4, &Type::isFloatTy); }

/// Whether \p Ty is the `<4 x i32>` texel `feme.cpu.image.load.2d.v4i32`
/// (roadmap E26) produces for an integer-format fetch.
bool isV4I32(const Type *Ty) {
  const auto *VecTy = dyn_cast<FixedVectorType>(Ty);
  return VecTy && VecTy->getNumElements() == 4 &&
         VecTy->getElementType()->isIntegerTy(32);
}

/// Whether \p Coord is a two-component coordinate of the right element type
/// for \p Float (normalized `<2 x float>` for a sample, integer
/// `<2 x i32>` for a fetch).
bool isCoord2D(const Value *Coord, bool Float) {
  const auto *VecTy = dyn_cast<FixedVectorType>(Coord->getType());
  if (!VecTy || VecTy->getNumElements() != 2)
    return false;
  return Float ? VecTy->getElementType()->isFloatTy()
               : VecTy->getElementType()->isIntegerTy(32);
}

/// Whether \p Offset is a compile-time-zero texel offset, the only value
/// `runtime/CPU`'s sampling/loading helpers accept -- matching
/// `feme::cpu::ResourceLoweringPass::isZeroOffset`'s identical narrowing on
/// the DXIL side. A nonzero offset is left unlowered rather than dropped.
bool isZeroOffset(const Value *Offset) {
  const auto *C = dyn_cast<Constant>(Offset);
  return C && C->isNullValue();
}

/// Checks that every use of a 2D sampled-image handle is one this pass can
/// rewrite: the image operand of an `llvm.spv.resource.sample`/
/// `samplelevel` whose coordinate, offset and result shapes the CPU
/// runtime's 2D helpers implement, or an `llvm.spv.resource.getpointer`
/// texel fetch (`OpImageFetch`, see `feme::spirv::ImageLoadPattern`) whose
/// pointer is only loaded from. \p IsInteger (roadmap E26) is
/// `classifySampledImage2DHandle`'s own channel-type test, repeated by the
/// caller rather than re-derived here: an integer-channel handle rejects
/// every sample intrinsic outright (SPIR-V never legalizes a filtered
/// sample against an integer-sampled image, so there is no shape to
/// accept), and expects each fetch's loaded type to be `<4 x i32>` instead
/// of `<4 x float>`.
bool hasOnlySupportedImageUses(const CallInst &Handle, bool IsInteger) {
  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;

    bool ExplicitLod = false;
    if (isSampleIntrinsic(*CI, ExplicitLod)) {
      if (IsInteger)
        return false; // No filtered sample over an integer-channel image.
      if (CI->getArgOperand(0) != &Handle)
        return false;
      unsigned OffsetIdx = ExplicitLod ? 4 : 3;
      if (!isCoord2D(CI->getArgOperand(2), /*Float=*/true) ||
          !isZeroOffset(CI->getArgOperand(OffsetIdx)) ||
          !isV4F32(CI->getType()))
        return false;
      continue;
    }

    if (getIntrinsicID(CI) != Intrinsic::spv_resource_getpointer)
      return false;
    if (!isCoord2D(CI->getArgOperand(1), /*Float=*/false))
      return false;
    for (const User *PU : CI->users()) {
      const auto *LI = dyn_cast<LoadInst>(PU);
      if (!LI || !(IsInteger ? isV4I32(LI->getType()) : isV4F32(LI->getType())))
        return false;
    }
  }
  return true;
}

/// Checks that every use of a sampler handle is the sampler operand of a
/// sample intrinsic. A sampler has no accesses of its own -- it only ever
/// pairs with an image -- so there is nothing else it can legitimately be.
bool hasOnlySupportedSamplerUses(const CallInst &Handle) {
  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    bool ExplicitLod = false;
    if (!CI || !isSampleIntrinsic(*CI, ExplicitLod))
      return false;
    if (CI->getArgOperand(1) != &Handle)
      return false;
  }
  return true;
}

/// Checks that every use of \p Handle is the flat access shape this pass
/// models for \p Kind: a `llvm.spv.resource.getpointer` call whose own
/// result is used only by an ordinary `load` (every kind), or a `store` it
/// is the pointer operand (not the stored value) of (`HandleKind::Storage`/
/// `TexelStorage` only -- a uniform/uniform-array/texel-uniform buffer is
/// always read-only, matching Vulkan's own restriction on
/// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_UNIFORM_TEXEL_BUFFER`) -- see the
/// header comment's "access shape" bullet. For `HandleKind::Uniform`, the
/// `getpointer` index (the field selected within the block's struct) must
/// also be a compile-time constant, unlike a storage buffer's -- or a
/// uniform buffer *array*'s (`HandleKind::UniformArray`) -- possibly
/// dynamic array index: a real cbuffer field access is always statically
/// typed. For a texel-buffer kind, every load's result type (or
/// store's stored-value type) must be one of the shapes
/// `isSupportedTexelElementType` accepts -- see `classifyTexelBufferHandle`'s
/// comment for why that check belongs here rather than on the handle type;
/// for a storage/uniform kind, it must instead be one
/// `isSupportedRawElementType` accepts, since that is what
/// `createRawLoad`/`createRawStore` (and, transitively,
/// `feme::cpu::mangleResourceCallName`) can mangle a runtime call name for.
/// Any further `getelementptr` into the element's own fields (a
/// structured-buffer field access, or a nested uniform-buffer field) is
/// left unmodeled, matching
/// `feme::cpu::ResourceLoweringPass::hasOnlySupportedUses`'s own narrowing.
bool hasOnlySupportedUses(const CallInst &Handle, HandleKind Kind) {
  bool Writable =
      Kind == HandleKind::Storage || Kind == HandleKind::TexelStorage;
  bool IsTexel = isTexelHandleKind(Kind);
  for (const User *U : Handle.users()) {
    const auto *GetPtr = dyn_cast<CallInst>(U);
    if (!GetPtr || getIntrinsicID(GetPtr) != Intrinsic::spv_resource_getpointer)
      return false;
    if (Kind == HandleKind::Uniform &&
        !isa<ConstantInt>(GetPtr->getArgOperand(1)))
      return false;
    for (const User *PU : GetPtr->users()) {
      if (const auto *LI = dyn_cast<LoadInst>(PU)) {
        if (IsTexel ? !isSupportedTexelElementType(LI->getType())
                    : !isSupportedRawElementType(LI->getType()))
          return false;
        continue;
      }
      if (Writable) {
        if (const auto *SI = dyn_cast<StoreInst>(PU)) {
          if (SI->getPointerOperand() != GetPtr)
            return false;
          if (IsTexel
                  ? !isSupportedTexelElementType(SI->getValueOperand()->getType())
                  : !isSupportedRawElementType(SI->getValueOperand()->getType()))
            return false;
          continue;
        }
      }
      return false;
    }
  }
  return true;
}

/// Folds away the `{image, sampler}` struct
/// `feme::spirv::SampledImagePattern` builds for `OpSampledImage`: every
/// `extractvalue` over the pair is replaced with the handle the matching
/// `insertvalue` put there, leaving each sample intrinsic's image/sampler
/// operand as a direct use of its own `handlefrombinding` call. Nothing
/// downstream of this pass understands a combined sampled-image value --
/// the CPU image ABI keeps the two descriptors separate, per
/// FeMeGraphicsDesign.md's "Combined image samplers remain two logical
/// descriptors paired by lowering" -- so folding it here is what lets a
/// single forward walk over a handle's users classify it at all.
void foldSampledImageStructs(Function &F) {
  SmallVector<ExtractValueInst *, 4> Extracts;
  for (Instruction &I : instructions(F))
    if (auto *EV = dyn_cast<ExtractValueInst>(&I))
      if (EV->getNumIndices() == 1 &&
          isa<StructType>(EV->getAggregateOperand()->getType()) &&
          isa<TargetExtType>(EV->getType()))
        Extracts.push_back(EV);

  for (ExtractValueInst *EV : Extracts) {
    Value *Found = FindInsertedValue(EV->getAggregateOperand(),
                                     EV->getIndices(), EV->getIterator());
    if (!Found || Found == EV)
      continue;
    EV->replaceAllUsesWith(Found);
    EV->eraseFromParent();
  }

  // The `insertvalue` chain (and the `poison` seed it started from) is dead
  // once every reader is folded; leaving it would make each handle look
  // like it had an unsupported use. Collected first, then erased in reverse
  // so an earlier link's last user is already gone when it is reached.
  SmallVector<InsertValueInst *, 4> Inserts;
  for (Instruction &I : instructions(F))
    if (auto *IV = dyn_cast<InsertValueInst>(&I))
      Inserts.push_back(IV);
  for (InsertValueInst *IV : llvm::reverse(Inserts))
    if (IV->use_empty())
      IV->eraseFromParent();
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
      Classification = classifySampledImage2DHandle(*CI);
    if (!Classification)
      Classification = classifySamplerHandle(*CI);
    if (!Classification)
      return std::nullopt; // Not one of the kinds this pass normalizes.

    switch (Classification->Kind) {
    case HandleKind::SampledImage2D:
      if (!hasOnlySupportedImageUses(
              *CI, Classification->TexelElementType->isIntegerTy(32)))
        return std::nullopt;
      break;
    case HandleKind::Sampler:
      if (!hasOnlySupportedSamplerUses(*CI))
        return std::nullopt;
      break;
    default:
      if (!hasOnlySupportedUses(*CI, Classification->Kind))
        return std::nullopt;
      break;
    }

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

/// The reserved prefix size each of the three heaps needs (see
/// `assignHeapBases`).
struct HeapPrefixSizes {
  uint32_t Resource = 0;
  uint32_t Image = 0;
  uint32_t Sampler = 0;
};

/// Assigns each non-conflicting identity a contiguous run of
/// `Entry.RangeSize` slots *in the heap its kind belongs to*, sorted by
/// identity for a deterministic layout -- mirroring
/// `feme::cpu::BoundResourceNormalizationPass`'s own `assignHeapBases`
/// (roadmap R26 generalized this pass from an implicit range size of 1, see
/// the header comment). The three heaps are numbered independently, so a
/// buffer and an image binding may each be assigned base 0. Returns each
/// heap's total reserved prefix size.
HeapPrefixSizes assignHeapBases(std::map<RangeKey, RangeEntry> &Ranges) {
  HeapPrefixSizes Sizes;
  for (auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    uint32_t *Base = nullptr;
    switch (getResourceClass(Entry.Kind)) {
    case BoundResourceClass::Buffer:
      Base = &Sizes.Resource;
      break;
    case BoundResourceClass::Image:
      Base = &Sizes.Image;
      break;
    case BoundResourceClass::Sampler:
      Base = &Sizes.Sampler;
      break;
    }
    Entry.HeapBase = *Base;
    *Base += Entry.RangeSize;
  }
  return Sizes;
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

/// Builds \p F's replacement: the same function with the eight trailing
/// resource/root-constant/image ABI parameters appended, in exactly the
/// order and naming `feme::cpu::ResourceLoweringPass`'s own (anonymous-
/// namespace, so duplicated here rather than shared -- matching how
/// `feme::amdgpu::ResourceLoweringPass`'s own `addBindingArguments` is
/// likewise a separate copy for its differently-shaped parameter list)
/// `addResourceEnvParams` does. Sharing the order matters because the
/// stage wrappers (`feme::cpu::EntryWrapperPass` and friends) resolve these
/// by *name*, so a SPIR-V-sourced stage and a DXIL-sourced one present the
/// host with one identical resource-binding ABI.
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
/// The access itself differs by \p BH.Kind: a storage-buffer access, or a
/// uniform-buffer *array*'s (`HandleKind::UniformArray`), multiplies its
/// `getpointer` array index (re-read per call, since -- unlike the
/// descriptor index above -- a distinct array element may be read per
/// access) by \p BH.Stride and goes through
/// `feme::cpu::createRawLoad`/`createRawStore`; a (non-array) uniform-buffer
/// access resolves its `getpointer` field index (a compile-time constant,
/// guaranteed by `hasOnlySupportedUses`) directly to \p BH.ElementStruct's
/// own declared byte offset for that field, also through the raw family --
/// no runtime arithmetic needed at all, since a cbuffer's fields have no
/// dynamic index the way a storage/uniform buffer array's elements do. A
/// texel buffer access (`isTexelHandleKind(BH.Kind)`) needs no byte-offset
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
  bool IsTexel = isTexelHandleKind(BH.Kind);

  for (User *U : llvm::make_early_inc_range(BH.Handle->users())) {
    auto *GetPtr = cast<CallInst>(U);
    Value *ElementIndex = nullptr;
    Value *Offset = nullptr;
    if (IsTexel) {
      IRBuilder<> PtrBuilder(GetPtr);
      ElementIndex = PtrBuilder.CreateZExt(GetPtr->getArgOperand(1), I64Ty);
    } else if (BH.Kind == HandleKind::Storage ||
               BH.Kind == HandleKind::UniformArray) {
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
      // Only reachable for HandleKind::Storage/TexelStorage.
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

/// Rewrites every sample and texel fetch performed through the image and
/// sampler handles in \p HeapIndices -- a map from each accepted
/// `handlefrombinding` call to the range-checked heap index it resolves to
/// -- into the corresponding canonical `feme.cpu.image.*` call (see
/// ImageCalls.h), then erases the handles themselves.
///
/// `hasOnlySupportedImageUses`/`hasOnlySupportedSamplerUses` already
/// guaranteed at collection time that every use is one of these shapes, so
/// there is no partially-rewritten state to worry about: either the whole
/// function was accepted, or none of it was.
void lowerImageAccesses(const MapVector<CallInst *, Value *> &HeapIndices,
                        const ImageCallEnv &Env) {
  LLVMContext &Ctx = Env.ImageHeap->getContext();
  Value *Mask = ConstantInt::getTrue(Ctx);

  for (const auto &[Handle, ImageIndex] : HeapIndices) {
    for (User *U : llvm::make_early_inc_range(Handle->users())) {
      auto *CI = cast<CallInst>(U);
      bool ExplicitLod = false;
      if (isSampleIntrinsic(*CI, ExplicitLod)) {
        // A sample is reached twice -- once from its image handle, once
        // from its sampler handle -- so only rewrite it from the image
        // side, where both descriptor indices are already resolvable.
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(2);
        Value *U0 = Builder.CreateExtractElement(Coord, uint64_t{0});
        Value *V0 = Builder.CreateExtractElement(Coord, uint64_t{1});
        Value *Lod = ExplicitLod ? CI->getArgOperand(3)
                                 : ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1)));
        CallInst *NewCall =
            createSample2D(Builder, Env, ImageIndex, SamplerIndex, U0, V0, Lod,
                           Builder.getInt1(ExplicitLod), Mask, CI->getName());
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // `OpImageFetch`: a `getpointer` whose result is only loaded from.
      IRBuilder<> Builder(CI);
      Value *Coord = CI->getArgOperand(1);
      Value *X = Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *Y = Builder.CreateExtractElement(Coord, uint64_t{1});
      for (User *PU : llvm::make_early_inc_range(CI->users())) {
        auto *LI = cast<LoadInst>(PU);
        IRBuilder<> LoadBuilder(LI);
        // Mip level 0: `feme::spirv::ImageLoadPattern` does not thread
        // `OpImageFetch`'s optional `Lod` image operand through today, so
        // there is no level operand to honor here yet. Sample 0: this is
        // an ordinary (non-subpass) `OpImageFetch`, which `ImageLoadPattern`
        // likewise never threads a `Sample` image operand through for --
        // only `SubpassLoadPattern`'s `Dim::SubpassData` case does (roadmap
        // F8c). The loaded type -- `<4 x i32>` or `<4 x float>`,
        // `hasOnlySupportedImageUses`'s own per-handle check already
        // guaranteed one or the other -- selects the integer (roadmap E26)
        // or float `feme.cpu.image.load.2d.*` entry point.
        bool IsInteger = isV4I32(LI->getType());
        CallInst *Loaded =
            IsInteger
                ? createLoad2DI32(LoadBuilder, Env, ImageIndex, X, Y,
                                  LoadBuilder.getInt32(0), Mask, LI->getName())
                : createLoad2D(LoadBuilder, Env, ImageIndex, X, Y,
                               LoadBuilder.getInt32(0), LoadBuilder.getInt32(0),
                               Mask, LI->getName());
        LI->replaceAllUsesWith(Loaded);
        LI->eraseFromParent();
      }
      CI->eraseFromParent();
    }
  }

  // Erased last: a sampler handle still had the sample calls as users while
  // the image side of the loop above was rewriting them.
  for (const auto &[Handle, ImageIndex] : HeapIndices) {
    (void)ImageIndex;
    Handle->eraseFromParent();
  }
}

/// Attaches the `!feme.cpu.resources` metadata node
/// `feme::cpu::ResourceInfo::fromModule` reads: name, \p RootConstantSize
/// (V3: a SPIR-V push-constant access `lowerFunctionResources` below found
/// and lowered through this same function's already-added
/// `root_constants`/`root_constant_size` parameters, or 0 if it has none --
/// see `feme::cpu::matchSPIRVPushConstantAccess`), \p UsesSamplerHeap
/// (roadmap R30's SPIR-V completion: true once this pass normalizes a bound
/// `spirv.Sampler` handle), a root-constant binding (always
/// `(space0, register0)`: a SPIR-V
/// push-constant block has no register identity of its own, unlike DXIL's
/// register-bound root constant, so there is nothing else to report), and
/// an empty statically-known-heap-index tail. That tail always stays empty
/// here regardless of whether a given access went through a compile-time-
/// constant or (roadmap R26) dynamic array index -- it is
/// `feme::cpu::ResourceLoweringPass`'s own dynamic-heap discovery
/// mechanism, unrelated to the bound-range assignment
/// `attachBoundResourceMetadata` below records unconditionally (see "Heap
/// usage discovery" in feme/docs/FeMeCPUDesign.md).
void attachResourceMetadata(Function &F, uint32_t RootConstantSize,
                            bool UsesSamplerHeap) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Metadata *Ops[] = {
      MDString::get(Ctx, F.getName()),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSize)),
      ConstantAsMetadata::get(ConstantInt::getBool(Ctx, UsesSamplerHeap)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0))};
  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.resources")
      ->addOperand(MDNode::get(Ctx, Ops));
}

/// Attaches the `!feme.cpu.bound_resources` metadata node
/// `feme::cpu::ResourceInfo::fromModule` reads, in the same shape
/// `feme::cpu::BoundResourceNormalizationPass::attachBoundResourceMetadata`
/// produces: name, the reserved resource-, image- and sampler-heap prefix
/// sizes, then each accepted identity as a (space, register, range-size,
/// heap-base, class) tuple -- SPIR-V's (set, binding) filling the (space,
/// register) slots per the header comment's correspondence, and range-size
/// the binding's own declared descriptor array count (roadmap R26
/// generalized this from an implicit 1).
void attachBoundResourceMetadata(Function &F, HeapPrefixSizes PrefixSizes,
                                 const std::map<RangeKey, RangeEntry> &Ranges) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Metadata *, 8> Ops;
  auto PushInt = [&](uint32_t V) {
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, V)));
  };
  Ops.push_back(MDString::get(Ctx, F.getName()));
  PushInt(PrefixSizes.Resource);
  PushInt(PrefixSizes.Image);
  PushInt(PrefixSizes.Sampler);
  for (const auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    PushInt(Key.Set);
    PushInt(Key.Binding);
    PushInt(Entry.RangeSize);
    PushInt(Entry.HeapBase);
    PushInt(static_cast<uint32_t>(getResourceClass(Entry.Kind)));
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
  // A `MapVector`, not a `DenseMap`: the rewrite order below decides both
  // the order rewritten functions end up in and the order their metadata
  // nodes are emitted, and neither may depend on pointer values.
  MapVector<Function *, SmallVector<BoundHandle, 4>> PerFunctionHandles;
  std::map<RangeKey, RangeEntry> Ranges;
  for (Function &F : M) {
    // A combined sampled-image value has to be taken apart before a handle's
    // own users can be classified (see `foldSampledImageStructs`).
    foldSampledImageStructs(F);
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

  HeapPrefixSizes PrefixSizes = assignHeapBases(Ranges);

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
    // Image and sampler handles are rewritten together, after this loop:
    // a sample needs *both* of its descriptor indices, which are computed
    // at two different handles.
    MapVector<CallInst *, Value *> ImageHeapIndices;
    bool UsesSamplerHeap = false;
    for (const BoundHandle &BH : Handles) {
      const RangeEntry &Entry = Ranges.at(BH.Key);
      if (Entry.Conflicting)
        continue;
      if (!RewroteAny) {
        NewF = addResourceEnvParams(*F, Env);
        RewroteAny = true;
      }
      if (isBufferHandleKind(BH.Kind)) {
        lowerAccesses(BH, Env, Entry.HeapBase);
        continue;
      }
      UsesSamplerHeap |= BH.Kind == HandleKind::Sampler;
      IRBuilder<> Builder(BH.Handle);
      ImageHeapIndices[BH.Handle] = computeClampedIndex(
          Builder, BH.Handle->getArgOperand(3), Entry.HeapBase, BH.RangeSize);
    }
    if (!RewroteAny)
      continue;
    Changed = true;

    if (!ImageHeapIndices.empty()) {
      ImageCallEnv ImgEnv;
      ImgEnv.ImageHeap = Env.ImageHeap;
      ImgEnv.ImageHeapCount = Env.ImageHeapCount;
      ImgEnv.SamplerHeap = Env.SamplerHeap;
      ImgEnv.SamplerHeapCount = Env.SamplerHeapCount;
      lowerImageAccesses(ImageHeapIndices, ImgEnv);
    }

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

    attachResourceMetadata(*NewF, RootConstantSize, UsesSamplerHeap);
    attachBoundResourceMetadata(*NewF, PrefixSizes, Ranges);
  }

  // An unused `handlefrombinding` declaration is left behind once its last
  // accepted caller is rewritten away; a conflicting one may still have
  // users, left for `feme::cpu::checkSupportedRaisedOps` to reject.
  // The sample/getpointer declarations `lowerImageAccesses` rewrote away go
  // with them.
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (!F.isDeclaration() || !F.use_empty())
      continue;
    Intrinsic::ID ID = F.getIntrinsicID();
    if (ID == Intrinsic::spv_resource_handlefrombinding ||
        ID == Intrinsic::spv_resource_sample ||
        ID == Intrinsic::spv_resource_samplelevel ||
        ID == Intrinsic::spv_resource_getpointer)
      F.eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
