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
/// The first six are buffers -- a storage buffer -- a homogeneous,
/// dynamically-indexed runtime array (`RWStructuredBuffer<T>`/
/// `StructuredBuffer<T>`) -- a storage buffer block spelled directly as a
/// fixed-layout struct (`glslang`'s own form for a fixed-size-only `buffer`
/// block) -- a uniform buffer -- a fixed set of differently-typed named
/// fields at fixed byte offsets (`cbuffer`/`ConstantBuffer<T>`) -- a
/// uniform buffer array -- a uniform block whose sole field is itself a
/// fixed-size, homogeneous array (roadmap F12a's own
/// `layout(std140) uniform Input { uint data[16]; }` shape) -- or (V4) a
/// texel buffer -- a `Buffer<T>`/`RWBuffer<T>`-shaped, format-converting
/// view over a `Dim::Buffer` SPIR-V image. Each needs its own offset
/// arithmetic (see `lowerAccesses`): a storage buffer access multiplies a
/// (possibly dynamic) array index by a fixed element stride, a direct-field
/// storage or uniform buffer access resolves a (always compile-time-
/// constant) field index directly to a fixed struct-layout byte offset, a
/// uniform buffer *array* access multiplies a (possibly dynamic) array
/// index by a fixed stride exactly like a storage buffer's own -- except
/// that stride is carried explicitly on the handle type itself (see
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
  StorageStruct,
  Uniform,
  UniformArray,
  TexelStorage,
  TexelUniform,
  SampledImage2D,
  Sampler,
  /// A storage image (roadmap H19a/H19b/H19c): `OpImageRead`/
  /// `OpImageWrite` through a `Sampled == 2` handle, lowered to
  /// `feme.cpu.image.{load,store}.{1d,2d(array),3d}.*` rather than the
  /// sampled-image `feme.cpu.image.sample.*` shapes. Covers
  /// `ImageShape::Plain2D` (roadmap H19a), `ImageShape::Array2D` (roadmap
  /// H19b), and `ImageShape::Plain1D`/`Plain3D` (roadmap H19c) only;
  /// `Cube`/`CubeArray` (and an arrayed `Plain1D`) remain unstarted
  /// follow-on work (roadmap H19d/H19e) -- unlike `SampledImage2D`, this
  /// kind's `Shape` is therefore never `Cube`/`CubeArray` in practice.
  StorageImage2D
};

/// Which of the four sampled-image shapes `HandleKind::SampledImage2D`
/// covers (roadmap H7b-a widened it beyond plain, non-arrayed 2D -- the
/// enumerator name is kept for now to minimize the blast radius across this
/// file's many `HandleKind` switches; only `ImageShape` distinguishes the
/// four shapes below it). A cube(array) is purely a view-level addressing
/// convention over an ordinary 2D-array-shaped image, never a distinct
/// physical layout of its own (see FeMeVulkanDesign.md's H7b update), so
/// `Cube`/`CubeArray` reuse the identical `HandleKind::SampledImage2D`
/// classification and only differ in `ImageShape`.
enum class ImageShape {
  /// A plain, non-arrayed `Texture2D`: `(U, V)` sample/fetch coordinates.
  Plain2D,
  /// A `Texture2DArray`: `(U, V, ArrayLayer)` sample, `(X, Y, Layer)` fetch.
  Array2D,
  /// A `TextureCube`: a 3-component direction-vector sample coordinate; no
  /// fetch (`OpImageFetch` is illegal against `Dim::Cube` in SPIR-V).
  Cube,
  /// A `TextureCubeArray`: a 3-component direction vector plus a float
  /// array-layer sample coordinate; no fetch, for the same reason as Cube.
  CubeArray,
  /// A `Texture1D` (roadmap H19c, `HandleKind::StorageImage2D` only -- no
  /// sampled-image counterpart exists yet): a single scalar `X` fetch
  /// coordinate, per SPIR-V's own "Coordinate must be a scalar or vector"
  /// rule (a 1-component coordinate is spelled as a bare scalar, not a
  /// 1-element vector, unlike every other shape here).
  Plain1D,
  /// A `Texture3D` (roadmap H19c, `HandleKind::StorageImage2D` only): a
  /// 3-component `(X, Y, Z)` fetch coordinate. Never arrayed -- SPIR-V
  /// disallows an arrayed `Dim::Dim3D` image entirely.
  Plain3D,
};

/// Whether \p Kind is one of the two texel-buffer kinds (see `HandleKind`).
bool isTexelHandleKind(HandleKind Kind) {
  return Kind == HandleKind::TexelStorage || Kind == HandleKind::TexelUniform;
}

/// Whether \p Kind's accesses go through `feme.cpu.resource.*` (every
/// buffer kind) rather than `feme.cpu.image.*`.
bool isBufferHandleKind(HandleKind Kind) {
  return Kind != HandleKind::SampledImage2D && Kind != HandleKind::Sampler &&
        Kind != HandleKind::StorageImage2D;
}

/// The heap \p Kind's descriptors are assigned slots in.
BoundResourceClass getResourceClass(HandleKind Kind) {
  switch (Kind) {
  case HandleKind::Storage:
  case HandleKind::StorageStruct:
  case HandleKind::Uniform:
  case HandleKind::UniformArray:
  case HandleKind::TexelStorage:
  case HandleKind::TexelUniform:
    return BoundResourceClass::Buffer;
  case HandleKind::SampledImage2D:
  case HandleKind::StorageImage2D:
    return BoundResourceClass::Image;
  case HandleKind::Sampler:
    return BoundResourceClass::Sampler;
  }
  llvm_unreachable("unhandled HandleKind");
}

/// A bound handle's identity: (descriptor set, binding, resource class),
/// playing the same role DXIL's (register space, register) pair does --
/// see the header comment's "SPIR-V's (descriptor set, binding) pair"
/// note. `Class` (roadmap H13d) is part of the identity, not just
/// (Set, Binding): an ordinary Vulkan binding provides exactly one
/// resource class, but `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` is a
/// single binding that legitimately backs *two* -- an `Image`-class and a
/// `Sampler`-class handle -- once `splitCombinedSampledImageHandles`
/// takes its one combined `handlefrombinding` call apart into two,
/// sharing that one binding's (set, binding) exactly as the real
/// descriptor does. Without `Class` in the key, those two would collide
/// at the same `(Set, Binding)` and be flagged an (incorrect) conflicting
/// re-declaration; two independently-declared bindings can still never
/// share the same (set, binding, class) tuple validly, so the conflict
/// check below remains exactly as strict as before for every other case.
struct RangeKey {
  uint32_t Set;
  uint32_t Binding;
  BoundResourceClass Class;

  bool operator<(const RangeKey &Other) const {
    return std::tie(Set, Binding, Class) <
           std::tie(Other.Set, Other.Binding, Other.Class);
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
  /// 0, for a direct-field storage/uniform buffer, whose offsets come from
  /// `ElementStruct`'s own layout instead.
  uint64_t Stride = 0;
  /// The direct-field storage/uniform-buffer field struct
  /// (`Kind == StorageStruct`/`Uniform`); null for a runtime-array storage
  /// or uniform-array buffer.
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
  /// The sampled-image shape (`Kind == SampledImage2D` only, see
  /// `ImageShape`); `Plain2D` (its zero-value) for every other kind. Two
  /// bindings at the same identity with different shapes (e.g. one
  /// function using a `TextureCube`, another a plain `Texture2D`, at the
  /// same descriptor set/binding -- not legal Vulkan usage, but not
  /// diagnosed as an error before this pass either) are a conflict, like
  /// every other classification mismatch this struct already detects.
  ImageShape Shape = ImageShape::Plain2D;
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
  /// Meaningful only for `Kind == SampledImage2D`; `Plain2D` (its
  /// zero-value) for every other kind.
  ImageShape Shape = ImageShape::Plain2D;
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
  /// Meaningful only for `Kind == SampledImage2D`; `Plain2D` (its
  /// zero-value) for every other kind.
  ImageShape Shape = ImageShape::Plain2D;
};

/// SPIR-V's `Uniform` and `StorageBuffer` storage-class numeric values, as
/// forwarded unchanged into `spirv.VulkanBuffer`'s own first integer
/// parameter by `convert{Buffer,Uniform}BlockType`
/// (SPIRVToLLVMPatterns.cpp).
constexpr unsigned SPIRVStorageClassUniform = 2;
constexpr unsigned SPIRVStorageClassStorageBuffer = 12;

/// Returns \p Handle's buffer classification if its type is a
/// `spirv.VulkanBuffer` handle -- see
/// `feme::spirv::convertBufferBlockType`/`convertUniformBlockType` in
/// SPIRVToLLVMPatterns.cpp for the handle shapes this recognizes: one type
/// parameter (a storage buffer's `!llvm.array<0 x ElemTy>` runtime array, a
/// direct-field storage or uniform buffer's own struct, or a uniform
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
/// A struct parameter is normally a direct-field uniform buffer, but a
/// `StorageBuffer`-class handle with one is a glslang-style storage buffer
/// block emitted directly as a fixed-layout struct rather than `dxc`'s
/// one-member runtime-array wrapper. A struct parameter whose storage class
/// is instead `Uniform` is ambiguous by storage class alone: a real,
/// read-only uniform block (`convertUniformBlockType`, always
/// `Writable=0`) and a *pre-1.3* storage buffer block -- spelled as
/// `Uniform` storage class plus a `BufferBlock` decoration rather than the
/// dedicated `StorageBuffer` class (`convertBufferBlockType`'s own
/// `isBufferBlockStorage`; still the form glslang emits by default for a
/// `buffer` block, as seen in this project's own real
/// `dEQP-VK.binding_model.shader_access.*.storage_buffer.compute.*` CTS
/// coverage) -- both carry the identical `Uniform` storage-class int
/// parameter. The second int parameter (`Writable`) is what actually
/// distinguishes them: `convertUniformBlockType` always emits `0`, while
/// `convertBufferBlockType` emits the real, possibly-`NonWritable`-derived
/// bit for every storage buffer block regardless of which storage class
/// spells it. So a writable `Uniform`-class struct is this legacy storage
/// buffer spelling, not an actual uniform block.
///
/// Returns `std::nullopt` for any other handle kind (an image/sampler
/// resource, or a `spirv.VulkanBuffer` shape this pass still does not
/// model -- see the header comment).
std::optional<HandleClassification>
classifyVulkanBufferHandle(const CallInst &Handle, const DataLayout &DL) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "spirv.VulkanBuffer")
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1 ||
      HandleTy->getNumIntParameters() < 2)
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
  if (auto *StructTy = dyn_cast<StructType>(Param)) {
    bool Writable = HandleTy->getIntParameter(1) != 0;
    if (HandleTy->getIntParameter(0) == SPIRVStorageClassStorageBuffer ||
        (HandleTy->getIntParameter(0) == SPIRVStorageClassUniform && Writable))
      return HandleClassification{HandleKind::StorageStruct, 0, StructTy};
    if (HandleTy->getIntParameter(0) == SPIRVStorageClassUniform)
      return HandleClassification{HandleKind::Uniform, 0, StructTy};
  }
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

/// SPIR-V's `Dim` operand value for `OpTypeImage 1D` (roadmap H19c).
constexpr unsigned SPIRVDim1D = 0;
/// SPIR-V's `Dim` operand value for `OpTypeImage 2D`.
constexpr unsigned SPIRVDim2D = 1;
/// SPIR-V's `Dim` operand value for `OpTypeImage 3D` (roadmap H19c).
constexpr unsigned SPIRVDim3D = 2;
/// SPIR-V's `Dim` operand value for `OpTypeImage Cube` (roadmap H7b-a).
constexpr unsigned SPIRVDimCube = 3;

/// Returns \p Handle's classification if its type is a single-sampled,
/// floating-point or 32-bit-integer 2D or Cube `spirv.Image`/
/// `spirv.SignedImage` handle used *with* a sampler -- the shapes
/// `runtime/CPU`'s sampling/fetch helpers implement (see ImageCalls.h's own
/// scope note; roadmap H7b-a widened this beyond plain, non-arrayed 2D to
/// also cover `Texture2DArray`/`TextureCube`/`TextureCubeArray`, recorded in
/// the returned classification's own `Shape`). Every other dimension, a
/// multisampled image, and a storage image (`Sampled == 2`, handled
/// instead by `classifyStorageImage2DHandle` below, roadmap H19a) return
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
  unsigned Dim = HandleTy->getIntParameter(0);
  if (Dim != SPIRVDim2D && Dim != SPIRVDimCube)
    return std::nullopt;
  // [Dim, Depth, Arrayed, MS, Sampled, Format]: a multisampled image needs
  // a per-sample coordinate the runtime's sampling/fetch helpers do not
  // take (no `feme.cpu.image.*` entry point for MSAA sampling exists).
  bool Arrayed = HandleTy->getIntParameter(2) != 0;
  if (HandleTy->getIntParameter(3) != 0)
    return std::nullopt;
  if (HandleTy->getIntParameter(4) != SPIRVSampledWithSampler)
    return std::nullopt;

  Type *ChannelType = HandleTy->getTypeParameter(0);
  if (!ChannelType->isFloatTy() && !ChannelType->isIntegerTy(32))
    return std::nullopt; // No other channel shape is decodable today.
  ImageShape Shape;
  if (Dim == SPIRVDim2D)
    Shape = Arrayed ? ImageShape::Array2D : ImageShape::Plain2D;
  else
    Shape = Arrayed ? ImageShape::CubeArray : ImageShape::Cube;
  return HandleClassification{HandleKind::SampledImage2D, 0, nullptr,
                              ChannelType, Shape};
}

/// Returns \p Handle's classification if its type is a plain, arrayed, 1D,
/// 3D, cube, or cube-array, non-multisampled storage-image handle
/// (`Sampled == 2`, roadmap H19a/H19b/H19c/H19d): the counterpart of
/// `classifySampledImage2DHandle` above for `OpImageRead`/`OpImageWrite`
/// rather than a filtered sample. A multisampled storage image or an
/// arrayed 1D one is left as unstarted follow-on work (see Roadmap.md's
/// H19e/H19g breakdown), so every other shape returns `std::nullopt` here,
/// exactly like `classifySampledImage2DHandle`'s own multisample rejection.
///
/// Unlike the sampled-image classifier above, a storage cube/cube-array
/// handle maps to `ImageShape::Array2D` here, *not* a distinct
/// `Cube`/`CubeArray` shape: a filtered cube *sample* addresses its texel
/// by a 3-component direction vector that a real cube-face-selection
/// algorithm resolves (`createSampleCube`'s own scope), but a storage cube
/// image's `imageLoad`/`imageStore` (GLSL's `imageCube`/`imageCubeArray`)
/// addresses its texel by an ordinary `(x, y, face)` (or, for
/// `imageCubeArray`, an already-flattened `layer * 6 + face`) triple --
/// structurally identical to `Array2D`'s own `(x, y, layer)` triple, and
/// consistent with this project's existing "a cube(array) view is purely a
/// view-level convention over consecutive array layers" treatment
/// (`CommandBuffer.cpp`'s `materializeImageDescriptor`, roadmap H7b).
/// Confirmed via a real CTS shader dump
/// (`dEQP-VK.image.load_store.with_format.cube.r32_uint`): `imageStore(...,
/// pos, imageLoad(u_image0, ivec3(63-pos.x, pos.y, pos.z)))` where `pos.z`
/// is a bare face index, not a direction-vector component. So `Dim`/
/// `Arrayed` map directly to `Plain1D`/`Plain2D`/`Array2D`/`Plain3D`, with
/// `Dim::Cube` folded into the `Array2D`/`Plain2D` branch alongside
/// `Dim::2D` rather than needing its own case; `Arrayed` is only
/// meaningful for `Dim::2D`/`Dim::Cube` here (SPIR-V disallows an arrayed
/// `Dim::3D` image outright, and an arrayed `Dim::1D` one is rejected below
/// as roadmap H19e's own remaining scope).
std::optional<HandleClassification>
classifyStorageImage2DHandle(const CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || (HandleTy->getName() != "spirv.Image" &&
                    HandleTy->getName() != "spirv.SignedImage"))
    return std::nullopt;
  if (HandleTy->getNumTypeParameters() != 1 ||
      HandleTy->getNumIntParameters() != 6)
    return std::nullopt;
  // [Dim, Depth, Arrayed, MS, Sampled, Format].
  unsigned Dim = HandleTy->getIntParameter(0);
  if (Dim != SPIRVDim1D && Dim != SPIRVDim2D && Dim != SPIRVDim3D &&
      Dim != SPIRVDimCube)
    return std::nullopt;
  bool Arrayed = HandleTy->getIntParameter(2) != 0; // Roadmap H19b/H19d.
  if (Dim != SPIRVDim2D && Dim != SPIRVDimCube && Arrayed)
    return std::nullopt; // Arrayed 1D: roadmap H19e. Arrayed 3D: illegal.
  if (HandleTy->getIntParameter(3) != 0) // MS: not yet supported.
    return std::nullopt;
  if (HandleTy->getIntParameter(4) != SPIRVSampledWithoutSampler)
    return std::nullopt;

  Type *ChannelType = HandleTy->getTypeParameter(0);
  if (!ChannelType->isFloatTy() && !ChannelType->isIntegerTy(32))
    return std::nullopt; // No other channel shape is decodable today.
  ImageShape Shape;
  if (Dim == SPIRVDim1D)
    Shape = ImageShape::Plain1D;
  else if (Dim == SPIRVDim3D)
    Shape = ImageShape::Plain3D;
  else if (Dim == SPIRVDimCube)
    // A cube's own face index always occupies the coordinate's third
    // component, even when `Arrayed == 0` (a plain, non-array cube still
    // has 6 faces to select between) -- unlike `Dim::2D`, where `Arrayed`
    // itself is what turns a 2-component coordinate into a 3-component
    // one, `Dim::Cube` always needs the 3-component `Array2D` shape.
    Shape = ImageShape::Array2D;
  else
    Shape = Arrayed ? ImageShape::Array2D : ImageShape::Plain2D;
  return HandleClassification{HandleKind::StorageImage2D, 0, nullptr,
                              ChannelType, Shape};
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

/// Whether \p Coord is an \p N-component coordinate of the right element
/// type for \p Float (normalized `<N x float>` for a sample, integer
/// `<N x i32>` for a fetch) -- `Plain2D`'s 2-component `(u, v)`/`(x, y)`,
/// `Array2D`'s 3-component `(u, v, layer)`/`(x, y, layer)` (roadmap H7b-a),
/// `Cube`/`CubeArray`'s 3-/4-component direction-vector coordinate
/// (roadmap H7b-a), and `Plain3D`'s 3-component `(x, y, z)` fetch
/// coordinate (roadmap H19c). \p N == 1 (`Plain1D`'s single `x` fetch
/// coordinate, roadmap H19c) is the one width with no vector wrapping at
/// all: per the SPIR-V spec, `OpImageFetch`/`OpImageRead`/`OpImageWrite`'s
/// Coordinate operand "must be a scalar or vector" -- a real 1D image's
/// single-component coordinate is emitted as a bare scalar, not a
/// 1-element vector, unlike every other width here -- so this checks the
/// scalar type directly instead of unwrapping a `FixedVectorType`.
bool isCoordN(const Value *Coord, unsigned N, bool Float) {
  if (N == 1) {
    Type *Ty = Coord->getType();
    return Float ? Ty->isFloatTy() : Ty->isIntegerTy(32);
  }
  const auto *VecTy = dyn_cast<FixedVectorType>(Coord->getType());
  if (!VecTy || VecTy->getNumElements() != N)
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

/// Checks that every use of a sampled-image handle is one this pass can
/// rewrite: the image operand of an `llvm.spv.resource.sample`/
/// `samplelevel` whose coordinate, offset and result shapes the CPU
/// runtime's helpers for \p Shape implement, or (`Plain2D`/`Array2D` only
/// -- `OpImageFetch` is illegal against `Dim::Cube` in SPIR-V, so `Cube`/
/// `CubeArray` never reach this branch) an `llvm.spv.resource.getpointer`
/// texel fetch (`OpImageFetch`, see `feme::spirv::ImageLoadPattern`) whose
/// pointer is only loaded from. \p IsInteger (roadmap E26) is
/// `classifySampledImage2DHandle`'s own channel-type test, repeated by the
/// caller rather than re-derived here: an integer-channel handle rejects
/// every sample intrinsic outright (SPIR-V never legalizes a filtered
/// sample against an integer-sampled image, so there is no shape to
/// accept), and expects each fetch's loaded type to be `<4 x i32>` instead
/// of `<4 x float>`.
///
/// Roadmap H7b-a coordinate widths, per SPIR-V's own convention (see
/// `classifySampledImage2DHandle`'s comment): `Plain2D` samples/fetches a
/// 2-component coordinate; `Array2D` samples/fetches a 3-component one
/// (the array layer as its own float/integer third component); `Cube`
/// samples a 3-component direction vector; `CubeArray` samples a
/// 4-component one (direction plus a float array-layer 4th component).
bool hasOnlySupportedImageUses(const CallInst &Handle, bool IsInteger,
                               ImageShape Shape) {
  unsigned SampleCoordWidth =
      Shape == ImageShape::CubeArray ? 4
      : (Shape == ImageShape::Array2D || Shape == ImageShape::Cube) ? 3
                                                                     : 2;
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
      if (!isCoordN(CI->getArgOperand(2), SampleCoordWidth, /*Float=*/true) ||
          !isZeroOffset(CI->getArgOperand(OffsetIdx)) ||
          !isV4F32(CI->getType()))
        return false;
      continue;
    }

    if (Shape == ImageShape::Cube || Shape == ImageShape::CubeArray)
      return false; // No fetch shape exists for Cube/CubeArray.
    if (getIntrinsicID(CI) != Intrinsic::spv_resource_getpointer)
      return false;
    unsigned FetchCoordWidth = Shape == ImageShape::Array2D ? 3 : 2;
    if (!isCoordN(CI->getArgOperand(1), FetchCoordWidth, /*Float=*/false))
      return false;
    for (const User *PU : CI->users()) {
      const auto *LI = dyn_cast<LoadInst>(PU);
      if (!LI || !(IsInteger ? isV4I32(LI->getType()) : isV4F32(LI->getType())))
        return false;
    }
  }
  return true;
}

/// Checks that every use of a `HandleKind::StorageImage2D` handle (roadmap
/// H19a/H19b/H19c) is a plain `getpointer` texel access (`OpImageRead`/
/// `OpImageWrite`, same shape `hasOnlySupportedImageUses` accepts for a
/// sampled image's own fetch), whose own users are `Load`s and/or `Store`s
/// of the matching `<4 x float>`/`<4 x i32>` type -- both may appear on the
/// *same* `getpointer` call, since the CTS's own `load_store` shader
/// pattern reads and writes the same binding (a copy-shader idiom), unlike
/// every other pointer-use check in this file, which does not need to
/// consider that. \p Shape (roadmap H19b/H19c) is `Plain1D`, `Plain2D`,
/// `Array2D`, or `Plain3D` -- `classifyStorageImage2DHandle` never returns
/// `Cube`/`CubeArray` for a storage image today -- and selects the
/// coordinate width exactly like `hasOnlySupportedImageUses`'s own
/// `FetchCoordWidth`.
bool hasOnlySupportedStorageImageUses(const CallInst &Handle, bool IsInteger,
                                      ImageShape Shape) {
  unsigned CoordWidth = Shape == ImageShape::Plain1D ? 1
                       : (Shape == ImageShape::Array2D ||
                          Shape == ImageShape::Plain3D)
                           ? 3
                           : 2;
  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI || getIntrinsicID(CI) != Intrinsic::spv_resource_getpointer)
      return false;
    if (!isCoordN(CI->getArgOperand(1), CoordWidth, /*Float=*/false))
      return false;
    for (const User *PU : CI->users()) {
      if (const auto *LI = dyn_cast<LoadInst>(PU)) {
        if (IsInteger ? !isV4I32(LI->getType()) : !isV4F32(LI->getType()))
          return false;
        continue;
      }
      if (const auto *SI = dyn_cast<StoreInst>(PU)) {
        if (SI->getPointerOperand() != CI)
          return false;
        Type *ValTy = SI->getValueOperand()->getType();
        if (IsInteger ? !isV4I32(ValTy) : !isV4F32(ValTy))
          return false;
        continue;
      }
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

/// Returns whether \p GEP's byte offset relative to its pointer operand can
/// be recovered as a non-negative constant plus zero or more
/// `index * constant` terms, the only form `lowerRawPointerUses` below
/// materializes.
bool hasResolvableGEPByteOffset(const GetElementPtrInst &GEP,
                                const DataLayout &DL) {
  unsigned BitWidth = DL.getIndexSizeInBits(GEP.getPointerAddressSpace());
  SmallMapVector<Value *, APInt, 4> VariableOffsets;
  APInt ConstantOffset(BitWidth, 0);
  if (!GEP.collectOffset(DL, BitWidth, VariableOffsets, ConstantOffset))
    return false;
  if (ConstantOffset.isNegative())
    return false;
  return llvm::all_of(VariableOffsets, [](const auto &Entry) {
    return !Entry.second.isNegative();
  });
}

/// Checks that every use of \p Ptr is one of the load/store or, when
/// \p AllowGEPs, `getelementptr` shapes this pass can lower.
bool hasOnlySupportedPointerUses(const Value &Ptr, bool Writable, bool IsTexel,
                                 bool AllowGEPs, const DataLayout &DL) {
  for (const User *U : Ptr.users()) {
    if (const auto *LI = dyn_cast<LoadInst>(U)) {
      if (IsTexel ? !isSupportedTexelElementType(LI->getType())
                  : !isSupportedRawElementType(LI->getType()))
        return false;
      continue;
    }
    if (Writable)
      if (const auto *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() != &Ptr)
          return false;
        if (IsTexel
                ? !isSupportedTexelElementType(SI->getValueOperand()->getType())
                : !isSupportedRawElementType(SI->getValueOperand()->getType()))
          return false;
        continue;
      }
    if (AllowGEPs)
      if (const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        if (GEP->getPointerOperand() != &Ptr ||
            !hasResolvableGEPByteOffset(*GEP, DL) ||
            !hasOnlySupportedPointerUses(*GEP, Writable, IsTexel, AllowGEPs,
                                         DL))
          return false;
        continue;
      }
    return false;
  }
  return true;
}

/// Checks that every use of \p Handle is the access shape this pass models
/// for \p Kind: a `llvm.spv.resource.getpointer` call whose own result is
/// used by an ordinary `load` (every kind), a `store` it is the pointer
/// operand of (`HandleKind::Storage`/`StorageStruct`/`TexelStorage` only --
/// a uniform/uniform-array/texel-uniform buffer is always read-only,
/// matching Vulkan's own restriction on
/// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_UNIFORM_TEXEL_BUFFER`), or, for a
/// storage-buffer kind only, an ordinary `getelementptr` chain ending in
/// such a load/store. For `HandleKind::Uniform` and `StorageStruct`, the
/// `getpointer` index (the field selected within the block's struct) must
/// also be a compile-time constant: a real cbuffer or direct-field storage
/// block access is statically typed there. For a texel-buffer kind, every
/// load's result type (or store's stored-value type) must be one of the
/// shapes `isSupportedTexelElementType` accepts -- see
/// `classifyTexelBufferHandle`'s comment for why that check belongs here
/// rather than on the handle type; for a storage/uniform kind, it must
/// instead be one `isSupportedRawElementType` accepts, since that is what
/// `createRawLoad`/`createRawStore` (and, transitively,
/// `feme::cpu::mangleResourceCallName`) can mangle a runtime call name for.
/// A nested uniform-buffer field access, or any GEP whose byte offset is not
/// recoverable from its indices, is still left unmodeled.
bool hasOnlySupportedUses(const CallInst &Handle, HandleKind Kind) {
  bool Writable = Kind == HandleKind::Storage ||
                  Kind == HandleKind::StorageStruct ||
                  Kind == HandleKind::TexelStorage;
  bool IsTexel = isTexelHandleKind(Kind);
  bool AllowGEPs =
      Kind == HandleKind::Storage || Kind == HandleKind::StorageStruct;
  const DataLayout &DL = Handle.getModule()->getDataLayout();
  for (const User *U : Handle.users()) {
    const auto *GetPtr = dyn_cast<CallInst>(U);
    if (!GetPtr || getIntrinsicID(GetPtr) != Intrinsic::spv_resource_getpointer)
      return false;
    if ((Kind == HandleKind::Uniform || Kind == HandleKind::StorageStruct) &&
        !isa<ConstantInt>(GetPtr->getArgOperand(1)))
      return false;
    if (!hasOnlySupportedPointerUses(*GetPtr, Writable, IsTexel, AllowGEPs, DL))
      return false;
  }
  return true;
}

/// Whether \p Ty is a two-element `{image, sampler}` struct -- a combined
/// sampled-image handle's result type (see
/// `splitCombinedSampledImageHandles`'s own comment): its first element is
/// a `spirv.Image`/`spirv.SignedImage` handle, its second a `spirv.Sampler`
/// handle, with no other fields -- matched structurally rather than by
/// identity, since `TypeConverter.addConversion(mlir::spirv::SampledImageType)`
/// (SPIRVToLLVMPatterns.cpp) produces an anonymous (literal) struct type,
/// but a `.ll` file's own named type alias (e.g. `%pair = type {...}`,
/// which this file's own unit tests use for readability) is a distinct,
/// identified `StructType` with the identical body -- both must match here.
bool isCombinedSampledImageStructType(Type *Ty) {
  auto *StructTy = dyn_cast<StructType>(Ty);
  if (!StructTy || StructTy->getNumElements() != 2)
    return false;
  auto *ImageTy = dyn_cast<TargetExtType>(StructTy->getElementType(0));
  if (!ImageTy || (ImageTy->getName() != "spirv.Image" &&
                   ImageTy->getName() != "spirv.SignedImage"))
    return false;
  auto *SamplerTy = dyn_cast<TargetExtType>(StructTy->getElementType(1));
  return SamplerTy && SamplerTy->getName() == "spirv.Sampler";
}

/// Splits a `handlefrombinding` call whose own result type is already the
/// combined `{image, sampler}` struct `isCombinedSampledImageStructType`
/// recognizes -- the shape `ResourceAddressOfPattern`
/// (SPIRVToLLVMPatterns.cpp) produces for an ordinary GLSL
/// `uniform sampler2D` declaration (a single `OpTypeSampledImage`
/// `UniformConstant` variable) -- into two synthetic `handlefrombinding`
/// calls, one per element type, sharing the original call's own (set,
/// binding, range size, index, name) operands. This is the *other* real
/// combined-sampled-image shape besides the one `foldSampledImageStructs`
/// already handles: that one is a genuine `OpSampledImage` instruction
/// combining two *separately*-declared handles (an `insertvalue` chain
/// this pass can trace back through with `FindInsertedValue`), while this
/// one is a single call already returning the pair directly, with nothing
/// for `FindInsertedValue` to trace -- it only ever seeds from a
/// `Constant`, `InsertValueInst`, or `ExtractValueInst`, never a `CallInst`
/// (see `llvm::FindInsertedValue`'s own doc comment), so a call's own
/// `extractvalue` users are left untouched otherwise. Redirecting each
/// such user to the matching synthetic call makes the two shapes converge
/// on one downstream representation: an ordinary, separately-declared
/// image handle and sampler handle, exactly as if the module's own source
/// had declared them independently and combined them via `OpSampledImage`
/// to begin with.
///
/// Left entirely alone if any user is not a single-index `extractvalue`
/// selecting element 0 or 1 -- this pass does not need to model what a
/// combined-handle call means used any other way (e.g. passed to another
/// function, or extracted with more than one index), and leaving it
/// unsplit means `collectHandles` correctly declines the whole function
/// rather than silently mis-lowering an unrecognized shape.
void splitCombinedSampledImageHandles(Function &F) {
  SmallVector<CallInst *, 4> Combined;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::spv_resource_handlefrombinding)
      continue;
    if (!isCombinedSampledImageStructType(CI->getType()))
      continue;
    if (llvm::all_of(CI->users(), [](const User *U) {
          const auto *EV = dyn_cast<ExtractValueInst>(U);
          return EV && EV->getNumIndices() == 1 && EV->getIndices()[0] <= 1;
        }))
      Combined.push_back(CI);
  }

  Module *M = F.getParent();
  for (CallInst *CI : Combined) {
    auto *StructTy = cast<StructType>(CI->getType());
    SmallVector<Value *, 5> Args(CI->args());
    IRBuilder<> Builder(CI);
    Function *ImageFn = Intrinsic::getOrInsertDeclaration(
        M, Intrinsic::spv_resource_handlefrombinding,
        {StructTy->getElementType(0)});
    Function *SamplerFn = Intrinsic::getOrInsertDeclaration(
        M, Intrinsic::spv_resource_handlefrombinding,
        {StructTy->getElementType(1)});
    Value *ImageHandle = Builder.CreateCall(ImageFn, Args);
    Value *SamplerHandle = Builder.CreateCall(SamplerFn, Args);

    SmallVector<ExtractValueInst *, 4> Extracts;
    for (User *U : CI->users())
      Extracts.push_back(cast<ExtractValueInst>(U));
    for (ExtractValueInst *EV : Extracts) {
      EV->replaceAllUsesWith(EV->getIndices()[0] == 0 ? ImageHandle
                                                      : SamplerHandle);
      EV->eraseFromParent();
    }
    CI->eraseFromParent();
  }
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
      Classification = classifyStorageImage2DHandle(*CI);
    if (!Classification)
      Classification = classifySamplerHandle(*CI);
    if (!Classification)
      return std::nullopt; // Not one of the kinds this pass normalizes.

    switch (Classification->Kind) {
    case HandleKind::SampledImage2D:
      if (!hasOnlySupportedImageUses(
              *CI, Classification->TexelElementType->isIntegerTy(32),
              Classification->Shape))
        return std::nullopt;
      break;
    case HandleKind::StorageImage2D:
      if (!hasOnlySupportedStorageImageUses(
              *CI, Classification->TexelElementType->isIntegerTy(32),
              Classification->Shape))
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
                 static_cast<uint32_t>(BindingC->getZExtValue()),
                 getResourceClass(Classification->Kind)};
    Handles.push_back(BoundHandle{CI, Key, Classification->Kind,
                                  Classification->Stride,
                                  Classification->ElementStruct,
                                  Classification->TexelElementType, RangeSize,
                                  Classification->Shape});
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
  // `copyAttributesFrom` copies calling convention, function attributes,
  // linkage, etc., but deliberately not function-attached metadata (see
  // `GlobalObject::copyAttributesFrom`): without this, a fragment/vertex
  // entry point that reaches this pass with a `!feme.signature` node
  // already attached (from `feme::graphics::CanonicalizeStagePass`, which
  // always runs first) would silently lose it here, and a later stage
  // wrapper (e.g. `feme::cpu::FragmentWrapperPass`) would then reject the
  // rebuilt function as having no signature at all -- see roadmap H3a.
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

/// Materializes \p GEP's byte offset relative to its pointer operand as an
/// i64 value. `hasOnlySupportedPointerUses` already guaranteed this is
/// representable as a non-negative constant plus zero or more
/// `index * constant` terms.
Value *computePointerOffset(IRBuilderBase &Builder,
                            const GetElementPtrInst &GEP,
                            const DataLayout &DL) {
  unsigned BitWidth = DL.getIndexSizeInBits(GEP.getPointerAddressSpace());
  SmallMapVector<Value *, APInt, 4> VariableOffsets;
  APInt ConstantOffset(BitWidth, 0);
  bool Collected =
      GEP.collectOffset(DL, BitWidth, VariableOffsets, ConstantOffset);
  (void)Collected;
  assert(Collected &&
         "hasOnlySupportedPointerUses only accepts resolvable GEPs");
  assert(!ConstantOffset.isNegative() &&
         "hasOnlySupportedPointerUses only accepts non-negative GEP offsets");

  Value *Offset =
      ConstantInt::get(Builder.getInt64Ty(), ConstantOffset.getZExtValue());
  for (const auto &[Index, Multiplier] : VariableOffsets) {
    assert(!Multiplier.isNegative() &&
           "hasOnlySupportedPointerUses only accepts non-negative GEP offsets");
    Value *Term = Index;
    if (Term->getType() != Builder.getInt64Ty())
      Term = Builder.CreateZExtOrTrunc(Term, Builder.getInt64Ty());
    if (Multiplier != 1)
      Term =
          Builder.CreateMul(Term, ConstantInt::get(Builder.getInt64Ty(),
                                                   Multiplier.getZExtValue()));
    Offset = Builder.CreateAdd(Offset, Term);
  }
  return Offset;
}

/// Rewrites every raw-buffer load/store reachable from \p Ptr (either
/// directly or, for a structured storage block, through a GEP chain) using
/// the already-resolved descriptor index and byte offset.
void lowerRawPointerUses(Value *Ptr, const ResourceCallEnv &Env,
                         Value *DescriptorIndex, Value *Offset,
                         const DataLayout &DL) {
  Value *Mask = ConstantInt::getTrue(Ptr->getContext());
  for (User *U : llvm::make_early_inc_range(Ptr->users())) {
    if (auto *LI = dyn_cast<LoadInst>(U)) {
      IRBuilder<> Builder(LI);
      CallInst *Loaded = createRawLoad(Builder, Env, DescriptorIndex, Offset,
                                       Mask, LI->getType(), LI->getName());
      LI->replaceAllUsesWith(Loaded);
      LI->eraseFromParent();
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(U)) {
      IRBuilder<> Builder(SI);
      createRawStore(Builder, Env, DescriptorIndex, Offset,
                     SI->getValueOperand(), Mask);
      SI->eraseFromParent();
      continue;
    }

    auto *GEP = cast<GetElementPtrInst>(U);
    IRBuilder<> Builder(GEP);
    Value *NestedOffset =
        Builder.CreateAdd(Offset, computePointerOffset(Builder, *GEP, DL));
    lowerRawPointerUses(GEP, Env, DescriptorIndex, NestedOffset, DL);
    GEP->eraseFromParent();
  }
}

/// Rewrites every access through \p BH.Handle -- a `getpointer` call
/// followed by a load or store, or, for a structured storage block, a GEP
/// chain ending in one (see `hasOnlySupportedUses`) -- into the
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
/// `feme::cpu::createRawLoad`/`createRawStore`; a direct-field storage or
/// uniform-buffer access resolves its `getpointer` field index (a compile-
/// time constant, guaranteed by `hasOnlySupportedUses`) directly to
/// \p BH.ElementStruct's own declared byte offset for that field, and any
/// following GEPs contributing further byte offset are folded in by
/// `lowerRawPointerUses` -- no other runtime arithmetic needed at the
/// top-level field selection itself, since such fields have no dynamic index
/// the way a storage/uniform buffer array's elements do. A texel buffer
/// access (`isTexelHandleKind(BH.Kind)`) needs no byte-offset arithmetic
/// either: its `getpointer` "index" is already the image coordinate
/// `OpImageRead`/`OpImageFetch`/`OpImageWrite` themselves address by, so it
/// goes through `feme::cpu::createTypedLoad`/`createTypedStore` directly,
/// letting the CPU runtime's format conversion (keyed off the bound
/// `FemeDescriptor::Format`) do the rest.
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

    if (IsTexel) {
      for (User *PU : llvm::make_early_inc_range(GetPtr->users())) {
        if (auto *LI = dyn_cast<LoadInst>(PU)) {
          IRBuilder<> Builder(LI);
          CallInst *Loaded =
              createTypedLoad(Builder, Env, DescriptorIndex, ElementIndex, Mask,
                              LI->getType(), LI->getName());
          LI->replaceAllUsesWith(Loaded);
          LI->eraseFromParent();
          continue;
        }
        // Only reachable for HandleKind::TexelStorage.
        auto *SI = cast<StoreInst>(PU);
        IRBuilder<> Builder(SI);
        createTypedStore(Builder, Env, DescriptorIndex, ElementIndex,
                         SI->getValueOperand(), Mask);
        SI->eraseFromParent();
      }
    } else {
      lowerRawPointerUses(GetPtr, Env, DescriptorIndex, Offset, DL);
    }
    GetPtr->eraseFromParent();
  }
  BH.Handle->eraseFromParent();
}

/// Rewrites every sample and texel fetch performed through the image and
/// sampler handles in \p HeapIndices -- a map from each accepted
/// One image or sampler handle's already-resolved heap index, plus (for an
/// image handle) its `ImageShape` -- carried through from
/// `classifySampledImage2DHandle` so `lowerImageAccesses` below can dispatch
/// each sample/fetch to the right `feme.cpu.image.*` shape without a second
/// lookup. Meaningless (left `Plain2D`, its zero-value) for a sampler
/// handle's own entry, which is only ever read for its `Index`.
struct ImageHeapEntry {
  Value *Index;
  ImageShape Shape = ImageShape::Plain2D;
};

/// Rewrites every sample and texel fetch performed through the image and
/// sampler handles in \p HeapIndices -- a map from each accepted
/// `handlefrombinding` call to the range-checked heap index (and, for an
/// image handle, `ImageShape`) it resolves to -- into the corresponding
/// canonical `feme.cpu.image.*` call (see ImageCalls.h), then erases the
/// handles themselves.
///
/// `hasOnlySupportedImageUses`/`hasOnlySupportedSamplerUses` already
/// guaranteed at collection time that every use is one of these shapes, so
/// there is no partially-rewritten state to worry about: either the whole
/// function was accepted, or none of it was.
void lowerImageAccesses(const MapVector<CallInst *, ImageHeapEntry> &HeapIndices,
                        const ImageCallEnv &Env) {
  LLVMContext &Ctx = Env.ImageHeap->getContext();
  Value *Mask = ConstantInt::getTrue(Ctx);

  for (const auto &[Handle, Entry] : HeapIndices) {
    Value *ImageIndex = Entry.Index;
    ImageShape Shape = Entry.Shape;
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
        Value *Lod = ExplicitLod ? CI->getArgOperand(3)
                                 : ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *ExplicitLodFlag = Builder.getInt1(ExplicitLod);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1))).Index;
        Value *C0 = Builder.CreateExtractElement(Coord, uint64_t{0});
        Value *C1 = Builder.CreateExtractElement(Coord, uint64_t{1});
        CallInst *NewCall;
        switch (Shape) {
        case ImageShape::Plain2D: {
          // Roadmap H7i: an implicit-LOD sample's real mip level (and,
          // when enabled, an anisotropic footprint) needs this sample's
          // own screen-space derivatives of (C0, C1), synthesized only in
          // the fragment stage -- the only stage GLSL's implicit
          // `texture()` is ever legal from; an explicit-LOD
          // `textureLod()` ignores them, so zero constants (no extra IR)
          // are passed instead.
          SampleDerivatives D =
              !ExplicitLod
                  ? getOrSynthesizeSample2DDerivatives(
                        Builder, *CI->getFunction(), C0, C1)
                  : SampleDerivatives{ConstantFP::get(Builder.getFloatTy(),
                                                      0.0),
                                     ConstantFP::get(Builder.getFloatTy(),
                                                      0.0),
                                     ConstantFP::get(Builder.getFloatTy(),
                                                      0.0),
                                     ConstantFP::get(Builder.getFloatTy(),
                                                      0.0)};
          NewCall = createSample2D(Builder, Env, ImageIndex, SamplerIndex, C0,
                                   C1, D.DUdX, D.DUdY, D.DVdX, D.DVdY, Lod,
                                   ExplicitLodFlag, Mask, CI->getName());
          break;
        }
        case ImageShape::Array2D: {
          Value *ArrayLayer = Builder.CreateExtractElement(Coord, uint64_t{2});
          NewCall = createSample2DArray(Builder, Env, ImageIndex, SamplerIndex,
                                       C0, C1, ArrayLayer, Lod,
                                       ExplicitLodFlag, Mask, CI->getName());
          break;
        }
        case ImageShape::Cube: {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          NewCall = createSampleCube(Builder, Env, ImageIndex, SamplerIndex,
                                     C0, C1, C2, Lod, ExplicitLodFlag, Mask,
                                     CI->getName());
          break;
        }
        case ImageShape::CubeArray: {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          Value *ArrayLayer = Builder.CreateExtractElement(Coord, uint64_t{3});
          NewCall = createSampleCubeArray(Builder, Env, ImageIndex,
                                          SamplerIndex, C0, C1, C2, ArrayLayer,
                                          Lod, ExplicitLodFlag, Mask,
                                          CI->getName());
          break;
        }
        case ImageShape::Plain1D:
        case ImageShape::Plain3D:
          // Neither shape is ever reached here: `classifySampledImage2DHandle`
          // (unlike `classifyStorageImage2DHandle`, roadmap H19c) never
          // produces a `Plain1D`/`Plain3D` shape for a *sampled* image
          // handle -- only a storage-image handle can be 1D/3D today.
          llvm_unreachable(
              "no sampled-image shape produces Plain1D/Plain3D");
        }
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // `OpImageFetch`/`OpImageRead`/`OpImageWrite`: a `getpointer` whose
      // result is loaded from and/or (roadmap H19a, `StorageImage2D` only)
      // stored to. `hasOnlySupportedImageUses` already rejected this
      // branch for `Cube`/`CubeArray` (no fetch shape exists for either),
      // so only `Plain1D`/`Plain2D`/`Array2D`/`Plain3D` reach here.
      IRBuilder<> Builder(CI);
      Value *Coord = CI->getArgOperand(1);
      // `Plain1D`'s own coordinate is a bare scalar, not a 1-element
      // vector (see `isCoordN`'s comment, roadmap H19c) -- there is no
      // vector to extract from, so `X` is `Coord` itself and there is no
      // `Y`/third component to extract at all.
      Value *X = Shape == ImageShape::Plain1D
                    ? Coord
                    : Builder.CreateExtractElement(Coord, uint64_t{0});
      Value *Y = Shape == ImageShape::Plain1D
                    ? nullptr
                    : Builder.CreateExtractElement(Coord, uint64_t{1});
      // The coordinate's own third component: an array layer for
      // `Array2D` (roadmap H19b) or a real depth-slice `Z` coordinate for
      // `Plain3D` (roadmap H19c) -- two distinct concepts sharing one
      // component index, never both at once (`classifyStorageImage2DHandle`
      // never returns a shape that is both arrayed and 3D).
      Value *C2 = (Shape == ImageShape::Array2D || Shape == ImageShape::Plain3D)
                     ? Builder.CreateExtractElement(Coord, uint64_t{2})
                     : nullptr;
      for (User *PU : llvm::make_early_inc_range(CI->users())) {
        // `HandleKind::StorageImage2D` (roadmap H19a/H19b/H19c) accepts a
        // `StoreInst` user here too -- `hasOnlySupportedStorageImageUses`
        // already guaranteed its shape (matching value type, pointer
        // operand == this `getpointer` call), unlike
        // `hasOnlySupportedImageUses`, which never accepts a store against
        // a sampled image's own read-only fetch.
        if (auto *SI = dyn_cast<StoreInst>(PU)) {
          IRBuilder<> StoreBuilder(SI);
          Value *Texel = SI->getValueOperand();
          bool IsInteger = isV4I32(Texel->getType());
          switch (Shape) {
          case ImageShape::Plain1D:
            if (IsInteger)
              createStore1DI32(StoreBuilder, Env, ImageIndex, X, Texel, Mask);
            else
              createStore1D(StoreBuilder, Env, ImageIndex, X, Texel, Mask);
            break;
          case ImageShape::Plain2D:
            if (IsInteger)
              createStore2DI32(StoreBuilder, Env, ImageIndex, X, Y, Texel,
                               Mask);
            else
              createStore2D(StoreBuilder, Env, ImageIndex, X, Y, Texel, Mask);
            break;
          case ImageShape::Array2D:
            if (IsInteger)
              createStore2DArrayI32(StoreBuilder, Env, ImageIndex, X, Y, C2,
                                    Texel, Mask);
            else
              createStore2DArray(StoreBuilder, Env, ImageIndex, X, Y, C2,
                                 Texel, Mask);
            break;
          case ImageShape::Plain3D:
            if (IsInteger)
              createStore3DI32(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                               Mask);
            else
              createStore3D(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                            Mask);
            break;
          case ImageShape::Cube:
          case ImageShape::CubeArray:
            llvm_unreachable(
                "no storage-image write shape for Cube/CubeArray");
          }
          SI->eraseFromParent();
          continue;
        }
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
        // or float `feme.cpu.image.load.*` entry point.
        bool IsInteger = isV4I32(LI->getType());
        CallInst *Loaded;
        switch (Shape) {
        case ImageShape::Plain1D:
          Loaded = IsInteger
                      ? createLoad1DI32(LoadBuilder, Env, ImageIndex, X,
                                       LoadBuilder.getInt32(0), Mask,
                                       LI->getName())
                      : createLoad1D(LoadBuilder, Env, ImageIndex, X,
                                    LoadBuilder.getInt32(0),
                                    LoadBuilder.getInt32(0), Mask,
                                    LI->getName());
          break;
        case ImageShape::Plain2D:
          Loaded = IsInteger
                      ? createLoad2DI32(LoadBuilder, Env, ImageIndex, X, Y,
                                       LoadBuilder.getInt32(0), Mask,
                                       LI->getName())
                      : createLoad2D(LoadBuilder, Env, ImageIndex, X, Y,
                                    LoadBuilder.getInt32(0),
                                    LoadBuilder.getInt32(0), Mask,
                                    LI->getName());
          break;
        case ImageShape::Array2D:
          Loaded =
              IsInteger
                  ? createLoad2DArrayI32(LoadBuilder, Env, ImageIndex, X, Y,
                                        C2, LoadBuilder.getInt32(0), Mask,
                                        LI->getName())
                  : createLoad2DArray(LoadBuilder, Env, ImageIndex, X, Y,
                                      C2, LoadBuilder.getInt32(0),
                                      LoadBuilder.getInt32(0), Mask,
                                      LI->getName());
          break;
        case ImageShape::Plain3D:
          Loaded =
              IsInteger
                  ? createLoad3DI32(LoadBuilder, Env, ImageIndex, X, Y, C2,
                                   LoadBuilder.getInt32(0), Mask,
                                   LI->getName())
                  : createLoad3D(LoadBuilder, Env, ImageIndex, X, Y, C2,
                                LoadBuilder.getInt32(0),
                                LoadBuilder.getInt32(0), Mask, LI->getName());
          break;
        case ImageShape::Cube:
        case ImageShape::CubeArray:
          llvm_unreachable("no storage-image fetch shape for Cube/CubeArray");
        }
        LI->replaceAllUsesWith(Loaded);
        LI->eraseFromParent();
      }
      CI->eraseFromParent();
    }
  }

  // Erased last: a sampler handle still had the sample calls as users while
  // the image side of the loop above was rewriting them.
  for (const auto &[Handle, Entry] : HeapIndices) {
    (void)Entry;
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
    // A single `handlefrombinding` call already returning the combined
    // `{image, sampler}` pair (an ordinary GLSL `uniform sampler2D`
    // declaration's own shape) is split into two ordinary handles first
    // (see `splitCombinedSampledImageHandles`'s own comment for why
    // `foldSampledImageStructs` alone cannot fold this shape). A combined
    // sampled-image value built from two *separately*-declared handles
    // then has to be taken apart before a handle's own users can be
    // classified (see `foldSampledImageStructs`).
    splitCombinedSampledImageHandles(F);
    foldSampledImageStructs(F);
    std::optional<SmallVector<BoundHandle, 4>> Handles = collectHandles(F);
    if (!Handles || Handles->empty())
      continue;
    for (const BoundHandle &BH : *Handles) {
      auto It = Ranges.find(BH.Key);
      if (It == Ranges.end())
        Ranges.emplace(BH.Key, RangeEntry{BH.Kind, BH.Stride, BH.ElementStruct,
                                          BH.TexelElementType, BH.RangeSize,
                                          BH.Shape, /*Conflicting=*/false});
      else if (It->second.Kind != BH.Kind || It->second.Stride != BH.Stride ||
               It->second.ElementStruct != BH.ElementStruct ||
               It->second.TexelElementType != BH.TexelElementType ||
               It->second.RangeSize != BH.RangeSize ||
               It->second.Shape != BH.Shape)
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
    MapVector<CallInst *, ImageHeapEntry> ImageHeapIndices;
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
      Value *Index = computeClampedIndex(
          Builder, BH.Handle->getArgOperand(3), Entry.HeapBase, BH.RangeSize);
      ImageHeapIndices[BH.Handle] = ImageHeapEntry{Index, BH.Shape};
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
