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
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <tuple>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Whether the environment requests a diagnostic explaining exactly which
/// use/instruction caused `collectHandles` to decline a function -- unset,
/// empty, or "0" all mean disabled, matching
/// `feme::vulkan::creationErrorLoggingEnabled`'s own convention
/// (Diagnostics.cpp). Added for roadmap H130: before this, the only signal
/// a declined function produced was `UnsupportedOps.cpp`'s own generic
/// "cannot normalize" diagnostic on whichever handle happened to be first
/// in the module -- not necessarily the one whose actual use triggered the
/// decline (see that diagnostic's own comment) -- leaving no way to find
/// the real offending instruction without manually re-deriving every one
/// of this file's own accept/reject branches against the failing IR by
/// hand. Checked on every call rather than cached, matching
/// `creationErrorLoggingEnabled`'s own reasoning.
static bool resourceNormalizationLoggingEnabled() {
  const char *Env = std::getenv("FEME_CPU_LOG_RESOURCE_NORMALIZATION");
  return Env && *Env && StringRef(Env) != "0";
}

/// Prints \p Reason and \p V (if non-null) to `errs()` when
/// `resourceNormalizationLoggingEnabled()`, identifying which specific use
/// or instruction caused `collectHandles` (transitively,
/// `hasOnlySupportedUses`/`hasOnlySupportedPointerUses`) to decline the
/// enclosing function.
static void logNormalizationRejection(StringRef Reason,
                                      const Value *V = nullptr) {
  if (!resourceNormalizationLoggingEnabled())
    return;
  errs() << "FEME_CPU_LOG_RESOURCE_NORMALIZATION: " << Reason;
  if (V)
    errs() << ": " << *V;
  errs() << "\n";
}

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
  /// A `Texture1DArray` (roadmap H19e, `HandleKind::StorageImage2D` only):
  /// a 2-component `(X, Layer)` fetch coordinate -- distinct from
  /// `Array2D`'s 3-component `(X, Y, Layer)` and `Plain1D`'s bare scalar
  /// `X`.
  Array1D,
  /// A multisampled, non-arrayed `Texture2D` (roadmap H19g,
  /// `HandleKind::StorageImage2D` only): a 3-component `(X, Y, Sample)`
  /// fetch coordinate -- structurally identical in width to `Array2D`'s
  /// `(X, Y, Layer)`, but the 3rd component selects a sample of one texel
  /// rather than an array layer.
  Plain2DMS,
  /// An arrayed multisampled `Texture2D` (roadmap H19m,
  /// `HandleKind::StorageImage2D` only): a 4-component `(X, Y, Layer,
  /// Sample)` fetch coordinate -- `Array2D`'s own 3-component `(X, Y,
  /// Layer)` with a trailing sample-index component appended, the same
  /// way `Plain2DMS` appends one to `Plain2D`'s 2-component coordinate. A
  /// cube or 3D multisampled storage image remains out of scope (SPIR-V
  /// disallows a multisampled `Dim::Cube`/`Dim::3D` image outright, unlike
  /// `Dim::2D`, which both `Plain2DMS`/`Array2DMS` cover).
  Array2DMS,
};

/// Whether \p Shape addresses a discrete array layer as the last component
/// of its coordinate. SPIR-V never differentiates that layer -- it selects
/// a slice rather than a filtered axis -- so an arrayed shape's `Grad`
/// derivative operands are exactly one component narrower than its own
/// sample coordinate (see `hasOnlySupportedImageUses`'s own
/// `GradDerivativeWidth`).
///
/// The fetch-only arrayed shapes (`Array1D`/`Array2DMS`) are included for
/// consistency even though no sample intrinsic reaches them today.
bool isArrayedShape(ImageShape Shape) {
  return Shape == ImageShape::Array2D || Shape == ImageShape::CubeArray ||
         Shape == ImageShape::Array1D || Shape == ImageShape::Array2DMS;
}

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
/// (storage class, writability) or three (storage class, writability, and
/// an explicit `ArrayStride`).
///
/// A storage buffer's own stride usually recovers from `ElemTy`'s own
/// store size, mirroring how `feme::cpu::ResourceLoweringPass::
/// classifyHandle` recovers a DXIL `dx.RawBuffer`'s stride from its
/// element type parameter -- valid for every scalar, 2-/4-component
/// vector, or matrix element, whose std430 `ArrayStride` always equals
/// that natural size. A 3-component vector element is the one shape where
/// that is *not* true (std430, like std140, still pads every array
/// element up to a 16-byte multiple regardless of storage class -- roadmap
/// L106's own `dEQP-VK.compute.pipeline.builtin_var.*` regression: writes
/// past whatever the *wrong* stride's own footprint could reach inside a
/// fixed-size buffer were simply never addressed at all, silently
/// dropped), so `convertBufferBlockType` carries the real stride
/// explicitly as this handle's own third integer parameter whenever it
/// was decorated, exactly like a std140 uniform buffer array's own
/// always-possible mismatch (every array widens its element to a 16-byte
/// multiple regardless of the element's own size -- e.g. a scalar
/// `uint`'s 4-byte size against a 16-byte stride, the shape roadmap F12a's
/// own CTS case hits) already required (see
/// `feme::spirv::convertUniformBlockType`'s own comment for why the
/// marker array itself cannot carry it, and why storage class plus
/// writability -- not the parameter count -- is what disambiguates a
/// storage array from a uniform one below, both of which may now carry
/// this third parameter). The two array shapes are otherwise
/// indistinguishable from `Param`'s own type alone, which is why storage
/// class/writability, not `ElemTy`, is what distinguishes them here.
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
    // A storage buffer's own runtime array wrapper and a std140 uniform
    // buffer's fixed-size array both convert to this same 0-sized
    // `!llvm.array<0 x ElemTy>` shape (see convertBufferBlockType's own
    // comment), so -- exactly like the `StructType` branch just below --
    // storage class plus writability, not the type alone, is what tells
    // them apart: a real uniform block is never writable, so a writable
    // `Uniform`-class handle here must be the legacy (pre-SPIR-V-1.3)
    // `BufferBlock` storage-buffer spelling instead.
    bool Writable = HandleTy->getIntParameter(1) != 0;
    bool IsStorage =
        HandleTy->getIntParameter(0) == SPIRVStorageClassStorageBuffer ||
        (HandleTy->getIntParameter(0) == SPIRVStorageClassUniform && Writable);
    // Both kinds carry their own real `ArrayStride` as an explicit third
    // integer parameter whenever it was decorated (`convertBufferBlockType`/
    // `convertUniformBlockType`) -- a storage buffer's stride otherwise
    // falls back to its element's natural size, true for every element
    // shape except a 3-component vector's (see convertBufferBlockType's
    // own comment: std430 still pads a `vec3` array element up to 16
    // bytes, roadmap L106).
    uint64_t Stride = HandleTy->getNumIntParameters() > 2
                          ? HandleTy->getIntParameter(2)
                          : DL.getTypeStoreSize(ArrayTy->getElementType());
    if (IsStorage)
      return HandleClassification{HandleKind::Storage, Stride, nullptr};
    return HandleClassification{HandleKind::UniformArray, Stride, nullptr};
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
/// Returns whether \p Ty is `<4 x float>`/`<4 x i32>` (V4), `<2 x float>`/
/// `<2 x i32>` (V2, roadmap L7a), or (roadmap L9) a scalar `float`/`i32`,
/// the shader-side element shapes the CPU runtime's typed-load/store
/// helpers implement a format conversion for today (see
/// femeCpuResourceLoadTypedV4F32/StoreTypedV4F32,
/// femeCpuResourceLoadTypedV4I32/StoreTypedV4I32,
/// femeCpuResourceLoadTypedV2F32/StoreTypedV2F32,
/// femeCpuResourceLoadTypedV2I32/StoreTypedV2I32, and
/// femeCpuResourceLoadTypedF32/StoreTypedF32,
/// femeCpuResourceLoadTypedI32/StoreTypedI32 in
/// feme/runtime/CPU/FeMeRuntimeCPU.c). The scalar shapes are a
/// single-channel format's own shader-visible element type (e.g. plain
/// `i32`/`float` for `RWBuffer<int>`/`RWBuffer<float>`): SPIR-V's
/// `OpImageRead`/`OpImageFetch` always return a full four-component vector
/// regardless of the underlying format's real channel count (see
/// classifyTexelBufferHandle's comment), but `OpImageWrite`'s own Texel
/// operand instead takes exactly the shader-declared element shape -- a
/// bare scalar for a single-channel `RWBuffer<T>` -- confirmed via a direct
/// IR reduction (`RWBuffer<int> In/Out; Out[0] = In[0];` lowers its store
/// to a scalar `i32`, not `<4 x i32>`). Roadmap L7a's own real IR reduction
/// (a `dxc -spirv` compile of `Basic/Matrix/matrix_m-based_getter.test`'s
/// own `RWBuffer<float2> OutVec2` output, storing a matrix row's two
/// elements) found this doc comment's prior claim here -- that a 2- or
/// 3-component vector is "still unmodeled" because "neither `dxc` nor
/// glslang ever emits one for a texel-buffer access" -- was simply wrong for
/// width 2: `dxc` emits a genuine `OpImageWrite`/`v2float` Texel operand for
/// any 2-channel `RWBuffer<T2>` write, exactly mirroring V4's own shape one
/// width down. This 2-wide case is now accepted here the same way V4 is.
/// Width 3 remains believed absent (SPIR-V's own mandatory texel-buffer
/// formats are 1-, 2-, or 4-channel only -- there is no 3-channel storage
/// texel buffer format for `dxc`/glslang to ever target in the first
/// place), so is still rejected below; revisit this claim too if a future
/// session's own reduction disproves it, the same way this session's did
/// for width 2.
bool isSupportedTexelElementType(Type *Ty) {
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    unsigned NumElements = VecTy->getNumElements();
    if (NumElements != 2 && NumElements != 4)
      return false;
    Ty = VecTy->getElementType();
  }
  return Ty->isFloatTy() || Ty->isIntegerTy(32);
}

/// Whether \p Ty is a shape `feme::cpu::mangleResourceCallName`'s own
/// `appendScalarMangling` (ResourceCalls.cpp) can mangle -- a scalar
/// half/float/double/integer, or a fixed vector of one -- or (roadmap L20)
/// a struct or fixed-size array every one of whose own members/elements is
/// itself one of these shapes, recursively: `lowerRawPointerUses` below
/// decomposes a whole-aggregate load/store into one such call per leaf
/// field/element rather than ever mangling a call name for the aggregate
/// itself (see that function's own comment), so this never needs to reach
/// `appendScalarMangling` with anything but a leaf scalar/vector type --
/// true of a *top-level* array (e.g. `Feature/StructuredBuffer/matrix.test`'s
/// own `[3 x float2]` per-element matrix row storage, loaded/stored as a
/// whole array with no enclosing struct) exactly as much as one nested
/// inside a struct field (e.g. `Feature/StructuredBuffer/packed.test`'s own
/// `Doggo` struct, whose `int3 Legs`/`int2 Ears` vector fields convert to
/// fixed-size LLVM arrays, not LLVM vectors, once nested inside a
/// tightly-packed struct) -- both go through the identical, per-element
/// decomposition below, so this function does not need to distinguish
/// them. This also means a bare array value reaching a raw load/store --
/// e.g. this project's own
/// `dEQP-VK.spirv_assembly.instruction.spirv1p4.opselect.array_select`
/// case, an `OpSelect` between two array-typed operands -- is now
/// decomposed the same way rather than left unclassified for
/// `UnsupportedOps.cpp`'s generic diagnostic (see
/// `LowersStorageBufferArrayStoreToPerElementRawStores` in
/// SPIRVResourceLoweringTest.cpp).
bool isSupportedRawElementType(Type *Ty) {
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    return llvm::all_of(StructTy->elements(), [](Type *ElemTy) {
      return isSupportedRawElementType(ElemTy);
    });
  }
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty))
    return isSupportedRawElementType(ArrayTy->getElementType());
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
/// floating-point or 32-bit-integer 1D, 2D, or Cube `spirv.Image`/
/// `spirv.SignedImage` handle used *with* a sampler -- the shapes
/// `runtime/CPU`'s sampling/fetch helpers implement (see ImageCalls.h's own
/// scope note; roadmap H7b-a widened this beyond plain, non-arrayed 2D to
/// also cover `Texture2DArray`/`TextureCube`/`TextureCubeArray`, and
/// roadmap L52a further widened it to cover
/// `Texture1D`/`Texture1DArray`, recorded in the returned classification's
/// own `Shape`). Every other dimension and a storage image (`Sampled ==
/// 2`, handled instead by `classifyStorageImage2DHandle` below, roadmap
/// H19a) return `std::nullopt`. An integer-channel handle is classified
/// the same as a float one here -- `hasOnlySupportedImageUses` (roadmap
/// E26) is what narrows its *uses* to fetch only, since SPIR-V never
/// legalizes a filtered sample against an integer-sampled image.
///
/// Roadmap L73: a multisampled (`MS == 1`) 2D sampled image (`Dim ==
/// SPIRVDim2D` only -- SPIR-V disallows multisampling for any other
/// `Dim`) classifies as `ImageShape::Plain2DMS`/`ImageShape::Array2DMS`,
/// reusing the same two shape values `classifyStorageImage2DHandle`
/// already returns for a multisampled *storage* image, since the
/// coordinate-width semantics (`Plain2DMS`: 3-wide `(x, y, sample)`;
/// `Array2DMS`: 4-wide `(x, y, layer, sample)`) are shape-appropriate
/// regardless of whether the underlying handle is a storage or sampled
/// image. Unlike a single-sampled handle, no `runtime/CPU` sampling/fetch
/// helper exists for a multisampled *sampled* image yet -- `textureSamples()`
/// (`OpImageQuerySamples`, `isQuerySamplesCall`) is the sole operation
/// `hasOnlySupportedImageUses` accepts against this shape; a filtered
/// sample or `texelFetch()` against a `sampler2DMS` is still unstarted
/// follow-on work and must keep being explicitly rejected there.
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
  if (Dim != SPIRVDim1D && Dim != SPIRVDim2D && Dim != SPIRVDim3D &&
      Dim != SPIRVDimCube)
    return std::nullopt;
  bool Arrayed = HandleTy->getIntParameter(2) != 0;
  if (Dim == SPIRVDim3D && Arrayed)
    return std::nullopt; // Arrayed 3D is illegal in SPIR-V.
  bool Multisampled = HandleTy->getIntParameter(3) != 0;
  // Roadmap L73: multisampling is only legal against a 2D sampled image
  // (SPIR-V's own spec restriction) -- every other dimension keeps the
  // unconditional multisample rejection this classifier has always had.
  if (Multisampled && Dim != SPIRVDim2D)
    return std::nullopt;
  if (HandleTy->getIntParameter(4) != SPIRVSampledWithSampler)
    return std::nullopt;

  Type *ChannelType = HandleTy->getTypeParameter(0);
  if (!ChannelType->isFloatTy() && !ChannelType->isIntegerTy(32))
    return std::nullopt; // No other channel shape is decodable today.
  ImageShape Shape;
  if (Multisampled)
    // Roadmap L73: `Dim == SPIRVDim2D` already confirmed above.
    Shape = Arrayed ? ImageShape::Array2DMS : ImageShape::Plain2DMS;
  else if (Dim == SPIRVDim1D)
    Shape = Arrayed ? ImageShape::Array1D : ImageShape::Plain1D;
  else if (Dim == SPIRVDim2D)
    Shape = Arrayed ? ImageShape::Array2D : ImageShape::Plain2D;
  else if (Dim == SPIRVDim3D)
    // Roadmap L66(a): a plain, ordinary sampled `Plain3D` volume texture --
    // SPIR-V disallows an arrayed `Dim::3D` image outright (rejected
    // above), so there is no `Array3D` counterpart to give a shape to.
    Shape = ImageShape::Plain3D;
  else
    Shape = Arrayed ? ImageShape::CubeArray : ImageShape::Cube;
  return HandleClassification{HandleKind::SampledImage2D, 0, nullptr,
                              ChannelType, Shape};
}

/// Returns \p Handle's classification if its type is a plain, arrayed, 1D,
/// 3D, cube, or cube-array, non-multisampled storage-image handle
/// (`Sampled == 2`, roadmap H19a/H19b/H19c/H19d/H19e/H19g): the counterpart
/// of `classifySampledImage2DHandle` above for `OpImageRead`/`OpImageWrite`
/// rather than a filtered sample. A multisampled (`MS == 1`), non-arrayed,
/// non-cube 2D storage image handle (roadmap H19g) classifies as
/// `ImageShape::Plain2DMS`, whose 3-component `(x, y, sample)` coordinate
/// `SPIRVToLLVMPatterns.cpp`'s `ImageLoadPattern`/`ImageWritePattern`
/// synthesize by appending `OpImageRead`/`OpImageWrite`'s own `Sample`
/// image operand to the ordinary 2-component `(x, y)` coordinate -- an
/// arrayed or cube multisampled storage image (a 4-component coordinate)
/// is still unstarted follow-on work, so every other multisampled shape
/// still returns `std::nullopt` here, exactly like
/// `classifySampledImage2DHandle`'s own unconditional multisample
/// rejection (a multisampled *sampled* image is out of this row's scope
/// entirely -- see Roadmap.md's H19g breakdown).
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
/// `Arrayed` map directly to `Plain1D`/`Array1D`/`Plain2D`/`Array2D`/
/// `Plain3D`, with `Dim::Cube` folded into the `Array2D`/`Plain2D` branch
/// alongside `Dim::2D` rather than needing its own case; `Arrayed` is
/// meaningful for `Dim::1D`/`Dim::2D`/`Dim::Cube` here (SPIR-V disallows an
/// arrayed `Dim::3D` image outright).
///
/// An arrayed 1D handle (roadmap H19e) maps to its own `ImageShape::Array1D`
/// -- distinct from `Array2D`'s 3-component `(x, y, layer)` coordinate --
/// since a 1D image's fetch coordinate has only one spatial component to
/// begin with, giving a 2-component `(x, layer)` coordinate overall.
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
  bool Arrayed = HandleTy->getIntParameter(2) != 0; // Roadmap H19b/H19d/H19e.
  if (Dim == SPIRVDim3D && Arrayed)
    return std::nullopt; // Arrayed 3D is illegal in SPIR-V.
  bool MS = HandleTy->getIntParameter(3) != 0; // Roadmap H19g/H19m.
  // A multisampled handle only classifies for a `Dim::2D` image today
  // (plain or arrayed) -- SPIR-V itself disallows a multisampled
  // `Dim::3D`/`Dim::Cube` image, so no further scope limit is needed here
  // for those. Roadmap H19m added the arrayed case; only `Plain2DMS`
  // (roadmap H19g) was supported before it.
  if (MS && Dim != SPIRVDim2D)
    return std::nullopt;
  if (HandleTy->getIntParameter(4) != SPIRVSampledWithoutSampler)
    return std::nullopt;

  Type *ChannelType = HandleTy->getTypeParameter(0);
  if (!ChannelType->isFloatTy() && !ChannelType->isIntegerTy(32))
    return std::nullopt; // No other channel shape is decodable today.
  ImageShape Shape;
  if (Dim == SPIRVDim1D)
    Shape = Arrayed ? ImageShape::Array1D : ImageShape::Plain1D;
  else if (Dim == SPIRVDim3D)
    Shape = ImageShape::Plain3D;
  else if (Dim == SPIRVDimCube)
    // A cube's own face index always occupies the coordinate's third
    // component, even when `Arrayed == 0` (a plain, non-array cube still
    // has 6 faces to select between) -- unlike `Dim::2D`, where `Arrayed`
    // itself is what turns a 2-component coordinate into a 3-component
    // one, `Dim::Cube` always needs the 3-component `Array2D` shape.
    Shape = ImageShape::Array2D;
  else if (MS)
    Shape = Arrayed ? ImageShape::Array2DMS : ImageShape::Plain2DMS; // H19g/m.
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

/// Whether \p CI is one of the seven SPIR-V sample intrinsics this pass
/// lowers, setting \p ExplicitLod for `samplelevel`, \p HasMinLodClamp
/// for `sample.clamp`/`samplebias.clamp`/`samplegrad.clamp` (roadmap
/// L26/L58/L59: SPIR-V's own `ConstOffset`+`MinLod` image-operand
/// combination, HLSL's `Texture2D::Sample`'s trailing `clamp` overload --
/// always an implicit-LOD sample, see `ImageSampleImplicitLodPattern`'s
/// own comment, so `ExplicitLod` and `HasMinLodClamp` are never both
/// set), \p HasBias for `samplebias`/`samplebias.clamp` (roadmap L58:
/// SPIR-V's own `Bias` image operand, GLSL's `texture(sampler, coord,
/// bias)` -- like `MinLod`, never combined with an explicit LOD, so
/// `ExplicitLod` and `HasBias` are never both set either), and \p HasGrad
/// for `samplegrad`/`samplegrad.clamp` (roadmap L59: SPIR-V's own `Grad`
/// image operand, GLSL's `textureGrad()`/`textureGradOffset()` -- an
/// explicit pair of screen-space partial-derivative vectors, mutually
/// exclusive with both `ExplicitLod` and `HasBias`, see
/// `ImageSampleGradPattern`'s own doc in `SPIRVToLLVMPatterns.cpp`).
/// Every recognized shape's own operand order is `(image, sampler,
/// coord, [lod|bias,] [dPdx, dPdy,] offset, [clamp])`
/// (`spv_resource_sample`'s own `offset` is the last operand;
/// `spv_resource_samplelevel`/`samplebias` each insert an extra scalar --
/// `lod`/`bias` respectively -- before it; `spv_resource_samplegrad`
/// inserts two vector operands -- `dPdx`, `dPdy` -- before it instead;
/// `spv_resource_sample_clamp`/`samplebias_clamp`/`samplegrad_clamp`
/// each append `clamp` after it instead) -- `getSampleOffsetIdx`/
/// `getSampleClampIdx` below derive each operand's own index from this
/// same `ExplicitLod`/`HasBias`/`HasGrad`/`HasMinLodClamp` quadruple
/// rather than every caller re-deriving it.
bool isSampleIntrinsic(const CallInst &CI, bool &ExplicitLod,
                       bool &HasMinLodClamp, bool &HasBias, bool &HasGrad) {
  Intrinsic::ID ID = getIntrinsicID(&CI);
  if (ID == Intrinsic::spv_resource_sample) {
    ExplicitLod = false;
    HasMinLodClamp = false;
    HasBias = false;
    HasGrad = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_sample_clamp) {
    ExplicitLod = false;
    HasMinLodClamp = true;
    HasBias = false;
    HasGrad = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_samplelevel) {
    ExplicitLod = true;
    HasMinLodClamp = false;
    HasBias = false;
    HasGrad = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_samplebias) {
    ExplicitLod = false;
    HasMinLodClamp = false;
    HasBias = true;
    HasGrad = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_samplebias_clamp) {
    ExplicitLod = false;
    HasMinLodClamp = true;
    HasBias = true;
    HasGrad = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_samplegrad) {
    ExplicitLod = false;
    HasMinLodClamp = false;
    HasBias = false;
    HasGrad = true;
    return true;
  }
  if (ID == Intrinsic::spv_resource_samplegrad_clamp) {
    ExplicitLod = false;
    HasMinLodClamp = true;
    HasBias = false;
    HasGrad = true;
    return true;
  }
  return false;
}

/// The index of a sample intrinsic's own offset operand, given
/// `isSampleIntrinsic`'s own `ExplicitLod`/`HasBias`/`HasGrad` outputs:
/// `spv_resource_sample`/`spv_resource_sample_clamp` are `(image,
/// sampler, coord, offset, [clamp])` (offset at index 3);
/// `spv_resource_samplelevel`/`samplebias`/`samplebias_clamp` each insert
/// an extra scalar (`lod`/`bias` respectively) before it, `(image,
/// sampler, coord, lod|bias, offset, [clamp])` (offset at index 4);
/// `spv_resource_samplegrad`/`samplegrad_clamp` instead insert two vector
/// operands (`dPdx`, `dPdy`), `(image, sampler, coord, dPdx, dPdy,
/// offset, [clamp])` (offset at index 5) -- `ExplicitLod`, `HasBias`, and
/// `HasGrad` are pairwise mutually exclusive (SPIR-V forbids combining
/// `Lod`/`Bias`/`Grad`), so at most one ever shifts the offset.
unsigned getSampleOffsetIdx(bool ExplicitLod, bool HasBias, bool HasGrad) {
  if (HasGrad)
    return 5;
  return (ExplicitLod || HasBias) ? 4 : 3;
}

/// The index of a `spv_resource_sample_clamp`/`samplebias_clamp`/
/// `samplegrad_clamp` call's own trailing `clamp` operand, immediately
/// after its offset operand; meaningless (never called) for any other
/// sample intrinsic, which has no such operand.
unsigned getSampleClampIdx(bool ExplicitLod, bool HasBias, bool HasGrad) {
  return getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad) + 1;
}

/// Whether \p CI is one of the six SPIR-V depth-comparison sample
/// intrinsics this pass lowers today (roadmap L46/L52(b)/L52(c)/L66(c)/
/// L72(b)), setting \p ExplicitLod for `samplecmplevelzero`/
/// `samplecmplevel` (a real, possibly-nonzero `Lod` for the latter,
/// roadmap L72(b); `samplecmplevelzero` still always forces mip level 0 --
/// it has no Lod operand of its own at all, unlike `samplelevel`) and
/// leaving it false for
/// `samplecmp`/`samplecmp_clamp`/`samplecmpbias{,_clamp}`/
/// `samplecmpgrad{,_clamp}` (implicit LOD unless a real derivative is
/// given; `femeCpuImageSampleCmp2DF32` degenerates to level 0 when \p
/// HasGrad is false and no explicit derivatives are threaded through,
/// exactly mirroring `Sample2D`'s own implicit-LOD path once \p HasGrad is
/// true), setting \p HasClamp for the two `.clamp` forms' own
/// trailing `MinLod` clamp operand (roadmap L52(c); legalized upstream by
/// `ImageSampleDrefImplicitLodPattern`/`ImageSampleDrefGradPattern`
/// alongside `ConstOffset` today, mirroring `isSampleIntrinsic`'s own
/// `HasMinLodClamp` output for an ordinary sample), and setting \p
/// HasLevel for `samplecmplevel` alone (roadmap L72(b): a real `Lod`
/// operand, at the same position `samplecmpbias`'s own `Bias` occupies --
/// mutually exclusive with \p HasBias/\p HasGrad, SPIR-V forbidding
/// `Bias`/`Lod`/`Grad` from combining on the same instruction). All six
/// share the same base `(image, sampler, coord, dref, offset)` operand
/// order (see `llvm/test/CodeGen/SPIRV/hlsl-resources/
/// SampleCmp{,LevelZero}.ll`), with the `samplecmpbias`/`samplecmplevel`/
/// `samplecmpgrad` forms inserting their own extra operand(s) between
/// `dref` and `offset`, and each `.clamp` form appending one more scalar
/// (the clamp) after `offset`.
bool isDrefSampleIntrinsic(const CallInst &CI, bool &ExplicitLod,
                           bool &HasClamp, bool &HasBias, bool &HasGrad,
                           bool &HasLevel) {
  Intrinsic::ID ID = getIntrinsicID(&CI);
  ExplicitLod = false;
  HasClamp = false;
  HasBias = false;
  HasGrad = false;
  HasLevel = false;
  switch (ID) {
  case Intrinsic::spv_resource_samplecmp:
    return true;
  case Intrinsic::spv_resource_samplecmp_clamp:
    HasClamp = true;
    return true;
  case Intrinsic::spv_resource_samplecmpbias:
    HasBias = true;
    return true;
  case Intrinsic::spv_resource_samplecmpbias_clamp:
    HasClamp = true;
    HasBias = true;
    return true;
  case Intrinsic::spv_resource_samplecmpgrad:
    HasGrad = true;
    return true;
  case Intrinsic::spv_resource_samplecmpgrad_clamp:
    HasClamp = true;
    HasGrad = true;
    return true;
  case Intrinsic::spv_resource_samplecmplevelzero:
    ExplicitLod = true;
    return true;
  case Intrinsic::spv_resource_samplecmplevel:
    ExplicitLod = true;
    HasLevel = true;
    return true;
  default:
    return false;
  }
}

/// The fixed operand index of a `spv_resource_samplecmp`/
/// `samplecmp_clamp`/`samplecmplevelzero` call's own depth-comparison
/// reference value -- all three share the identical `(image, sampler,
/// coord, dref, offset, [clamp])` shape (unlike `isSampleIntrinsic`'s own
/// family, whose offset index shifts with `ExplicitLod`), so this is a
/// fixed constant rather than a function of anything.
constexpr unsigned DrefSampleDrefIdx = 3;

/// The operand index of a `spv_resource_samplecmpbias{,_clamp}` call's own
/// `Bias` operand, immediately after its dref operand (roadmap L52(b));
/// meaningless (never read) for the non-bias forms, none of which has
/// such an operand.
constexpr unsigned DrefSampleBiasIdx = DrefSampleDrefIdx + 1;

/// The operand index of a `spv_resource_samplecmpgrad{,_clamp}` call's
/// own `dPdx` operand, immediately after its dref operand (roadmap
/// L66(c)) -- `Grad` and `Bias` are mutually exclusive on the same
/// instruction, so this shares `DrefSampleBiasIdx`'s own position rather
/// than needing a distinct constant.
constexpr unsigned DrefSampleGradDPdxIdx = DrefSampleDrefIdx + 1;

/// The operand index of a `spv_resource_samplecmpgrad{,_clamp}` call's
/// own `dPdy` operand, immediately after `dPdx`.
constexpr unsigned DrefSampleGradDPdyIdx = DrefSampleGradDPdxIdx + 1;

/// The operand index of a `spv_resource_samplecmplevel` call's own real
/// `Lod` operand, immediately after its dref operand (roadmap L72(b)) --
/// `Lod` is mutually exclusive with both `Bias` and `Grad` on the same
/// instruction, so this shares `DrefSampleBiasIdx`'s own position too,
/// rather than needing a distinct constant.
constexpr unsigned DrefSampleLevelIdx = DrefSampleDrefIdx + 1;

/// The operand index of a dref sample call's own `ConstOffset` operand,
/// immediately after its dref operand -- or after its bias/level operand,
/// for the `samplecmpbias`/`samplecmplevel` forms, or after its
/// `dPdx`/`dPdy` pair, for the `samplecmpgrad` forms (per SPIR-V's own
/// fixed Image Operands bit order, `Bias`/`Lod`/`Grad` before
/// `ConstOffset`; `Bias`, `Lod`, and `Grad` are pairwise mutually
/// exclusive, so no combined case is needed).
constexpr unsigned getDrefSampleOffsetIdx(bool HasBias, bool HasGrad = false,
                                          bool HasLevel = false) {
  return DrefSampleDrefIdx + (HasGrad ? 3 : ((HasBias || HasLevel) ? 2 : 1));
}

/// The operand index of a dref sample call's own trailing `MinLod` clamp
/// operand, immediately after its offset operand (roadmap L52(c));
/// meaningless (never read) for the non-`.clamp` forms, none of which has
/// such an operand.
constexpr unsigned getDrefSampleClampIdx(bool HasBias, bool HasGrad = false,
                                         bool HasLevel = false) {
  return getDrefSampleOffsetIdx(HasBias, HasGrad, HasLevel) + 1;
}

/// Whether \p CI is the `spv_resource_gather_cmp` intrinsic
/// (`ImageDrefGatherPattern`, `SPIRVToLLVMPatterns.cpp`, legalizing
/// `spirv.ImageDrefGather`, roadmap L7d). Its own fixed `(image, sampler,
/// coord, dref, offset)` operand shape is identical to a plain
/// `spv_resource_samplecmp` call's (`DrefSampleDrefIdx`/
/// `getDrefSampleOffsetIdx(false)` both apply unchanged, since
/// `spirv.ImageDrefGather` has no `Bias`/`Lod`/`Grad` image operand of its
/// own at all -- a gather instruction always operates at mip level 0 per
/// the SPIR-V spec, with no way to request otherwise), but this is
/// intentionally *not* folded into `isDrefSampleIntrinsic`'s own family:
/// gather always returns a full `<4 x float>` (one comparison result per
/// one of the four texels its fixed footprint samples), never a single
/// filtered scalar the way every `isDrefSampleIntrinsic` case's own result
/// is, so it needs its own, separate `hasOnlySupportedImageUses`
/// acceptance branch and its own separate `lowerImageAccesses` codegen
/// dispatch below.
bool isGatherCmpIntrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_gather_cmp;
}

/// Whether \p CI is the `spv_resource_gather` intrinsic
/// (`ImageGatherPattern`, `SPIRVToLLVMPatterns.cpp`, legalizing
/// `spirv.ImageGather`, roadmap L7g). Its own fixed `(image, sampler,
/// coord, component, offset)` operand shape is identical to
/// `isGatherCmpIntrinsic`'s own `spv_resource_gather_cmp` shape (same
/// `DrefSampleDrefIdx`/`getDrefSampleOffsetIdx(false)` indices apply
/// unchanged), differing only in that the `Dref` position holds an
/// integer *component selector* (0-3, selecting which of the four
/// gathered texels' R/G/B/A channel to return) rather than a
/// floating-point depth-comparison reference, and the result is never a
/// comparison outcome the way `isGatherCmpIntrinsic`'s always is.
bool isGatherIntrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_gather;
}

/// Whether \p CI is one of the two SPIR-V LOD-query intrinsics
/// `ImageQueryLodPattern` (`SPIRVToLLVMPatterns.cpp`) legalizes an
/// `OpImageQueryLod` into (roadmap L52e), setting \p Unclamped to
/// distinguish which of the two lanes of `OpImageQueryLod`'s own
/// `<2 x float>` result \p CI itself computes: `calculate.lod` alone is
/// the clamped "level" (lane 0, \p Unclamped false); `calculate.lod
/// .unclamped` is the raw, unclamped LOD (lane 1, \p Unclamped true).
/// Both share the identical `(image, sampler, coord)` operand order and
/// return a scalar float each (unlike every sample intrinsic above, which
/// return a texel).
bool isQueryLodIntrinsic(const CallInst &CI, bool &Unclamped) {
  Intrinsic::ID ID = getIntrinsicID(&CI);
  if (ID == Intrinsic::spv_resource_calculate_lod) {
    Unclamped = false;
    return true;
  }
  if (ID == Intrinsic::spv_resource_calculate_lod_unclamped) {
    Unclamped = true;
    return true;
  }
  return false;
}

/// Whether \p CI is `llvm.spv.resource.getdimensions.xy` (roadmap L70): a
/// plain 2D image's mip-0 `(Width, Height)` extent query -- GLSL's own
/// `imageSize()`/`textureSize()` against a `sampler2D`/`image2D` with no
/// explicit LOD argument (SPIR-V `OpImageQuerySize`). Scoped to this one
/// variant only -- the mip-count-returning `.levels.*`/the
/// multisample-count-returning `.ms.*` variants (every other `ImageShape`'s
/// own `GetDimensions` counterpart) remain unstarted follow-on work; see
/// `isGetDimensions1Intrinsic` below for the `.x` (typed-buffer element
/// count) sibling and `isGetDimensions3Intrinsic` immediately below for the
/// 3-component `.xyz` sibling.
bool isGetDimensionsIntrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_getdimensions_xy;
}

/// Whether \p CI is `llvm.spv.resource.getdimensions.xyz` (roadmap H124s):
/// a storage image's own mip-0 `(Width, Height, Elements)` extent query
/// (SPIR-V `OpImageQuerySize`, `v3uint` result, no explicit LOD operand) --
/// the `RWTexture2DArray::GetDimensions(Width, Height, Elements)` overload.
/// A *sampled* `Texture2DArray`'s identical-looking no-mip-argument
/// `GetDimensions` overload lowers differently (Clang's own HLSL codegen
/// always synthesizes an explicit `Lod = 0` for a sampled image, emitting
/// `OpImageQuerySizeLod` instead, see `isQuerySizeLodCall`) -- a storage
/// image has no mip-chain concept to select a level from at all, so its
/// codegen emits the bare, Lod-less `OpImageQuerySize` opcode here
/// instead. Scoped to `Array2D` only (the one shape `H124s`'s own CTS
/// case exercises) -- `Plain3D`'s analogous `RWTexture3D::GetDimensions`
/// overload remains unstarted follow-on work.
bool isGetDimensions3Intrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_getdimensions_xyz;
}

/// Whether \p CI is `llvm.spv.resource.getdimensions.x` (roadmap H144): a
/// typed buffer's own (`Buffer<T>`/`RWBuffer<T>`, SPIR-V `Dim::Buffer`)
/// element-count query -- `OpImageQuerySize` against a 1-component-result
/// image handle. Unlike `isGetDimensionsIntrinsic`/`isGetDimensions3Intrinsic`
/// above (both scoped to an ordinary 2D/array-2D image handle classified
/// by `classifySampledImage2DHandle`/`classifyStorageImage2DHandle`), this
/// is the one `GetDimensions` shape a `classifyTexelBufferHandle` handle
/// (`HandleKind::TexelStorage`/`TexelUniform`) itself can produce -- a
/// texel buffer has no width/height/mip concept, only a flat element
/// count, matching the single scalar `.x` result width.
bool isGetDimensions1Intrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_getdimensions_x;
}

/// Whether \p CI is `llvm.spv.resource.getarraylength` (roadmap H160): a
/// storage-buffer runtime array's own element-count query -- SPIR-V's
/// `OpArrayLength`, converted by `ArrayLengthPattern`
/// (SPIRVToLLVMPatterns.cpp) from `spirv.ArrayLength`. Like
/// `isGetDimensions1Intrinsic`'s texel-buffer bare `getdimensions.x` call
/// above, this addresses no element -- it just reads the descriptor's own
/// byte size divided by the handle's already-tracked `BoundHandle::Stride`
/// -- so it is likewise modeled directly on the handle itself rather than
/// requiring the usual `getpointer` indirection. Confirmed via a real
/// `dxc -spirv` reduction of `ByteAddressBuffer::GetDimensions()` (which
/// returns a *byte* count, unlike `StructuredBuffer<T>::GetDimensions()`'s
/// element count) that `dxc`'s own codegen already emits an explicit
/// `OpIMul` by the element size (4, for `ByteAddressBuffer`'s `uint`
/// runtime-array element) after `OpArrayLength` itself -- so this
/// intrinsic's own raw element-count semantics need no further
/// byte/element distinction here; scaling, if any, is already baked into
/// the SPIR-V the frontend emits.
bool isGetArrayLengthIntrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_getarraylength;
}

/// Whether \p CI's callee is a `SPIRVImporter.cpp`-synthesized magic-named
/// external function whose own name begins with \p Prefix -- the
/// recognition mechanism roadmap L72(d)'s own `lowerImageQueryOpcodes`
/// import-time rewrite relies on: unlike `isGetDimensionsIntrinsic`'s own
/// real `llvm.spv.resource.*` intrinsic, `OpImageQuerySizeLod`/
/// `OpImageQueryLevels` have no real MLIR/LLVM intrinsic of their own at
/// all (MLIR's SPIR-V dialect has zero enum coverage for either opcode),
/// so that rewrite instead declares an ordinary external function with a
/// distinct, stable name prefix per opcode/shape and this pass simply
/// matches against that prefix by name.
bool isSyntheticQueryCall(const CallInst &CI, StringRef Prefix) {
  const Function *Callee = CI.getCalledFunction();
  return Callee && Callee->getName().starts_with(Prefix);
}

/// Whether \p CI is one of `SPIRVImporter.cpp`'s synthesized
/// `feme.query.size_lod.*` calls (roadmap L72(d)): `OpImageQuerySizeLod`,
/// a plain 2D image's own extent at an explicit, possibly non-zero mip
/// level -- GLSL's `textureSize(sampler, lod)`. See
/// `isSyntheticQueryCall`'s own doc for why this is a name-based, rather
/// than intrinsic-ID-based, recognizer.
bool isQuerySizeLodCall(const CallInst &CI) {
  return isSyntheticQueryCall(CI, "feme.query.size_lod.");
}

/// Whether \p CI is one of `SPIRVImporter.cpp`'s synthesized
/// `feme.query.levels.*` calls (roadmap L72(d)): `OpImageQueryLevels`, an
/// image's own total mip-level count -- GLSL's
/// `textureQueryLevels(sampler)`. See `isSyntheticQueryCall`'s own doc.
bool isQueryLevelsCall(const CallInst &CI) {
  return isSyntheticQueryCall(CI, "feme.query.levels.");
}

/// Whether \p CI is one of `SPIRVImporter.cpp`'s synthesized
/// `feme.query.samples.*` calls (roadmap L73): `OpImageQuerySamples`, a
/// multisampled image's own sample count -- GLSL's
/// `textureSamples(sampler2DMS)`. See `isSyntheticQueryCall`'s own doc.
bool isQuerySamplesCall(const CallInst &CI) {
  return isSyntheticQueryCall(CI, "feme.query.samples.");
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

/// Roadmap L76(a): a storage-image (`HandleKind::StorageImage2D`) Load's
/// result or Store's Texel operand takes exactly the shader's declared
/// `RWTexture*<T>` element width -- a bare scalar (e.g.
/// `RWTexture2D<float>`), or a fixed vector of width 2, 3, or 4 (e.g.
/// `RWTexture2D<float2>`/`<float3>`/`<float4>`) -- confirmed via a real
/// `dxc -spirv` compile of each width. This is unlike a texel *buffer*'s
/// own `RWBuffer<T>` (`isSupportedTexelElementType`'s doc, scoped to a
/// bare scalar or a full 4-wide vector only, since neither `dxc` nor
/// glslang ever emits anything narrower for that handle kind): a storage
/// *image*'s own `OpImageWrite`/its `getpointer` result's `Load`/`Store`
/// genuinely does take a narrower vector for a 2- or 3-channel format.
/// Returns the real component count (1-4) if \p Ty is one of these shapes
/// with an element type matching \p IsInteger (`i32` vs `float`), or 0 if
/// it is neither.
unsigned storageImageTexelWidth(Type *Ty, bool IsInteger) {
  unsigned Width = 1;
  Type *ElemTy = Ty;
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    Width = VecTy->getNumElements();
    if (Width < 2 || Width > 4)
      return 0;
    ElemTy = VecTy->getElementType();
  }
  if (IsInteger ? !ElemTy->isIntegerTy(32) : !ElemTy->isFloatTy())
    return 0;
  return Width;
}

/// Whether \p Ty's own element type (a bare scalar, or the element type of
/// a fixed vector of any width) is a 32-bit integer -- classifies a
/// storage-image Load's result type or a Store's Texel operand type
/// before `storageImageTexelWidth` has validated its exact width, mirroring
/// `isV4I32`'s own element-type check but for any width 1-4, not just 4.
bool isIntegerStorageTexelType(const Type *Ty) {
  if (const auto *VecTy = dyn_cast<FixedVectorType>(Ty))
    Ty = VecTy->getElementType();
  return Ty->isIntegerTy(32);
}

/// Widens \p Texel -- a bare scalar or a narrower-than-4 vector,
/// `storageImageTexelWidth` already validated it as one of the shapes this
/// function accepts -- up to a full `<4 x float>`/`<4 x i32>`, the fixed
/// width every `feme.cpu.image.store.*` runtime entry point's own Texel
/// parameter takes (see ImageCalls.cpp's `getOrInsertImageCall`). The
/// padding lanes' own value is never observed: `femeRTPackImageTexel`/
/// `femeRTPackImageTexelI32` (`FeMeRuntimeCPU.c`) only ever read back the
/// bound image's own real channel count, silently discarding the rest --
/// but a zero constant is used for them anyway (rather than `poison`),
/// so this can never itself introduce undefined behavior if some future
/// change ever taught the runtime to read a padding lane.
Value *widenStorageImageTexel(IRBuilderBase &Builder, Value *Texel,
                              bool IsInteger) {
  Type *ElemTy = IsInteger ? Builder.getInt32Ty() : Builder.getFloatTy();
  if (auto *VecTy = dyn_cast<FixedVectorType>(Texel->getType());
      VecTy && VecTy->getNumElements() == 4 &&
      VecTy->getElementType() == ElemTy)
    return Texel;
  Constant *ZeroLane = IsInteger ? cast<Constant>(Builder.getInt32(0))
                                 : cast<Constant>(ConstantFP::get(ElemTy, 0.0));
  Value *Result = ConstantVector::getSplat(ElementCount::getFixed(4), ZeroLane);
  if (auto *VecTy = dyn_cast<FixedVectorType>(Texel->getType())) {
    for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I)
      Result = Builder.CreateInsertElement(
          Result, Builder.CreateExtractElement(Texel, I), I);
    return Result;
  }
  return Builder.CreateInsertElement(Result, Texel, uint64_t{0});
}

/// Narrows \p V4 -- a full `<4 x float>`/`<4 x i32>`, every
/// `feme.cpu.image.load.*` runtime entry point's own fixed return width --
/// down to \p WantTy (a bare scalar or a narrower-than-4 vector,
/// `storageImageTexelWidth` already validated the `LoadInst`'s own result
/// type as one of these): the read-side mirror of `widenStorageImageTexel`
/// above.
Value *narrowStorageImageTexel(IRBuilderBase &Builder, Value *V4,
                               Type *WantTy) {
  if (V4->getType() == WantTy)
    return V4;
  if (auto *VecTy = dyn_cast<FixedVectorType>(WantTy)) {
    SmallVector<int, 4> ShuffleMask;
    for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I)
      ShuffleMask.push_back(I);
    return Builder.CreateShuffleVector(V4, ShuffleMask);
  }
  return Builder.CreateExtractElement(V4, uint64_t{0});
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

/// Whether \p CI is `llvm.spv.resource.load.level` (roadmap L72): an
/// explicit-mip texel fetch (SPIR-V `OpImageFetch` with a `Lod` image
/// operand -- see `feme::spirv::ImageFetchLodPattern` in
/// SPIRVToLLVMPatterns.cpp) against a sampled (combined sampler+image)
/// handle. GLSL's `texelFetch()` always supplies an explicit LOD, so
/// without recognizing this shape here `collectHandles` rejected every
/// real `texelFetch()` call against a sampled image outright (confirmed
/// via a real CTS `dEQP-VK.glsl.texture_functions.texelfetch.*` capture),
/// unlike the pre-existing `llvm.spv.resource.getpointer` texel-fetch path
/// this pass already accepted (see `hasOnlySupportedImageUses`'s own
/// header comment) -- `ImageFetchPattern` (no explicit `Lod`) and
/// `ImageFetchLodPattern` (this one) are two distinct raised forms of the
/// same GLSL builtin, chosen by whether the SPIR-V source supplied a
/// literal `Lod` image operand at all. \p Offset (the intrinsic's fourth
/// operand) is no longer required to be zero (roadmap L72(b)):
/// `ImageFetchLodPattern` now threads a real `ConstOffset` through rather
/// than always synthesizing zero, so `hasOnlySupportedImageUses`/its own
/// lowering below validate/apply the real value themselves via
/// `isSupportedOffset`, mirroring every other offset-carrying intrinsic
/// this pass already accepts.
bool isFetchLevelIntrinsic(const CallInst &CI) {
  return getIntrinsicID(&CI) == Intrinsic::spv_resource_load_level;
}

/// Whether \p Offset is an acceptable texel offset for a sample against
/// \p Shape. `Plain2D` (roadmap L26) accepts any compile-time-constant
/// integer vector -- SPIR-V's own `ConstOffset` image operand, which
/// `lowerImageAccesses` below applies to every fetched texel's own integer
/// address before the sampler's addressing mode
/// (`femeRTComputeBilinearSupport`/`femeRTSamplePoint2D`), matching the
/// design doc's own "the backend itself folds away an all-zero
/// `ConstOffset`" note -- a real, nonzero offset is now threaded through
/// rather than rejecting the whole handle outright. \p AllowArray2D
/// (roadmap L50d/L33) additionally accepts the same real, nonzero offset
/// for `Array2D` too, for both a depth-comparison sample's own caller
/// (roadmap L50d) and an ordinary (non-comparison) sample's own caller
/// (roadmap L33) below. `Plain3D` (roadmap L67(c)) unconditionally
/// accepts a real, nonzero offset too -- unlike `Array2D`, which is only
/// widened for an ordinary/depth-comparison sample's own caller
/// specifically (there being no depth-comparison `Plain3D` sample for
/// `AllowArray2D`'s own distinction to matter for). \p
/// AllowPlain1DArray1D (roadmap L66(d), widened to the depth-comparison
/// caller too by roadmap L66(k)) similarly accepts the same real, nonzero
/// offset for `Plain1D`/`Array1D` -- a real `deqp-vk` SPIR-V capture of
/// `sampler1d{,array}shadow_bias_fragment` confirms a depth-comparison
/// sample's own `ConstOffset` against these two shapes is the identical
/// bare scalar `i32` an ordinary sample's is, despite this shape's own
/// dref-widened `Coordinate` staying a genuine vector (unlike `Plain2D`/
/// `Array2D`, whose own `Offset` operand does mirror that widened
/// coordinate -- see `isSupportedOffset`'s own width-check comment
/// below). Only `Cube`/`CubeArray` still require the trivial always-zero
/// case unconditionally regardless of caller: SPIR-V disallows
/// `ConstOffset` against a cube image outright, so there is no real,
/// nonzero case to ever accept there.
///
/// For `Plain2D`/`Array2D`, only the offset's first two components (X/Y)
/// are ever read (see `lowerImageAccesses`'s own
/// `CreateExtractElement(Offset, 0/1)` below), so this deliberately does
/// not require an exact vector width there: an ordinary sample's own
/// `Offset` operand is always 2-wide (its
/// `ImageSampleImplicitLodPattern`-emitted type mirrors its 2-wide
/// `Plain2D` coordinate) even for `Array2D` (whose own 3-wide `(U, V,
/// Layer)` coordinate does not widen its `Offset` operand the way a
/// depth-comparison sample's own `Dref`-widened coordinate does below),
/// but a depth-comparison sample's own `Offset` operand mirrors its own
/// *Dref*-widened coordinate instead (`ImageSampleDrefImplicitLodPattern`'s
/// `OffsetType` -- 3-wide for `Plain2D`, 4-wide for `Array2D`), so a
/// single fixed width would reject one of the two callers. `Plain3D`'s
/// own `Offset` operand mirrors its own real 3-component `(U, V, W)`
/// coordinate (glslang always emits a genuine `<3 x i32>` `ConstOffset`
/// against a 3D sampler -- confirmed via a real `deqp-vk` SPIR-V capture,
/// roadmap L67(c)), so a minimum width of 3 (rather than 2) is required
/// there, with the third (Z) component read by `lowerImageAccesses`'s own
/// `Plain3D` branch alongside X/Y. `Plain1D`/`Array1D`'s own `Offset`
/// operand is a bare scalar `i32`, never a vector at all -- confirmed via
/// a real `deqp-vk` SPIR-V capture of both `sampler1d`/`sampler1darray`'s
/// own `textureOffset()` cases (roadmap L66(d)): SPIR-V's own
/// `ConstOffset` dimensionality tracks the image's real dimension count
/// (1 for a 1D image), *excluding* any array layer, the same "+1"
/// carve-out `GradDerivativeWidth` (roadmap L64) already applies to a
/// `Grad` derivative -- `Array1D`'s own 2-component `(U, ArrayLayer)`
/// coordinate does not widen its own `ConstOffset` into a vector the way
/// a depth-comparison sample's `Dref`-widened coordinate does for
/// `Plain2D`/`Array2D` above.
bool isSupportedOffset(const Value *Offset, ImageShape Shape,
                       bool AllowArray2D = false,
                       bool AllowPlain1DArray1D = false) {
  bool IsPlain3D = Shape == ImageShape::Plain3D;
  bool Is1D = AllowPlain1DArray1D &&
              (Shape == ImageShape::Plain1D || Shape == ImageShape::Array1D);
  if (Shape != ImageShape::Plain2D && !IsPlain3D && !Is1D &&
      !(AllowArray2D && Shape == ImageShape::Array2D))
    return isZeroOffset(Offset);
  if (!isa<Constant>(Offset))
    return false;
  if (Is1D)
    // Roadmap L66(k): accept a real scalar `i32` `ConstOffset` (the
    // depth-comparison and ordinary sample paths alike), but also still
    // accept the depth-comparison path's own *synthesized* always-zero
    // fallback for these two shapes, which stays a `DrefCoordWidth`-wide
    // vector matching `Coord`'s own dref-widened shape rather than a
    // scalar (see the Dref-lowering switch's own comment in
    // `lowerImageAccesses`) -- `isZeroOffset` accepts a null constant of
    // any type, so this does not loosen what a genuine nonzero vector
    // offset would be accepted here.
    return Offset->getType()->isIntegerTy(32) || isZeroOffset(Offset);
  const auto *VecTy = dyn_cast<FixedVectorType>(Offset->getType());
  unsigned MinWidth = IsPlain3D ? 3 : 2;
  return VecTy && VecTy->getNumElements() >= MinWidth &&
         VecTy->getElementType()->isIntegerTy(32);
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
/// Roadmap L52a: `Plain1D` samples a bare scalar (1-component, no vector
/// wrapping -- see `isCoordN`'s own comment); `Array1D` samples a
/// 2-component one (the array layer as its own float second component).
/// Neither shape's own `OpImageFetch` (`getpointer`) path is accepted yet
/// below -- only its ordinary sample intrinsic is -- since this session's
/// own real CTS-driven scope is ordinary sampling only (no real failing
/// case exercises a 1D `texelFetch` today); a future row can lift this
/// restriction the same way `Array2D`'s own fetch path already works.
/// Roadmap L66(a): `Plain3D` samples a real 3-component `(U, V, W)`
/// coordinate, same width as `Array2D`'s own arrayed one -- its own
/// `OpImageFetch` path is likewise not accepted yet (this row's own scope
/// is ordinary sampling only, mirroring `Plain1D`/`Array1D`'s identical
/// decision above). Roadmap L67(a) adds real `Bias`/`MinLodClamp` support
/// for `Plain3D`, and roadmap L67(b) adds real `Grad` support too (see the
/// checks below) -- roadmap L67(c) adds real `ConstOffset` support for
/// this shape too (see `isSupportedOffset`'s own updated doc).
bool hasOnlySupportedImageUses(const CallInst &Handle, bool IsInteger,
                               ImageShape Shape) {
  unsigned SampleCoordWidth =
      Shape == ImageShape::CubeArray ? 4
      : (Shape == ImageShape::Array2D || Shape == ImageShape::Cube ||
         Shape == ImageShape::Plain3D)
          ? 3
      : Shape == ImageShape::Plain1D ? 1
                                     : 2;
  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;

    // Roadmap L70: a plain 2D sampled image's own `imageSize()`/
    // `textureSize()` query (`OpImageQuerySize`, no explicit LOD operand)
    // needs no further validation beyond its shape -- see
    // `isGetDimensionsIntrinsic`'s own doc for why this is scoped to
    // `Plain2D` only.
    if (isGetDimensionsIntrinsic(*CI)) {
      if (Shape != ImageShape::Plain2D)
        return false;
      continue;
    }

    // Roadmap L72(d)/L75: `OpImageQuerySizeLod` -- unlike
    // `isGetDimensionsIntrinsic`'s own call (whose sole operand already
    // *is* the handle), this synthesized call's own Image operand is its
    // first argument, mirroring `isSampleIntrinsic`'s own
    // `CI->getArgOperand(0) != &Handle` convention. Every classifiable
    // non-multisampled shape now has its own correctly-widthed builder
    // (`QuerySizeLod2D` for `Plain2D`/`Cube`, whose formula is identical;
    // `QuerySizeLod1D`/`QuerySizeLod1DArray`/`QuerySizeLod2DArray`/
    // `QuerySizeLod3D`/`QuerySizeLodCubeArray` for every other shape, see
    // each one's own doc for its distinct result width/formula) --
    // `Plain2DMS`/`Array2DMS` remain unsupported: `OpImageQuerySizeLod`
    // is spec-legal against a multisampled image, but no real CTS case
    // has driven that combination's own scoping yet.
    if (isQuerySizeLodCall(*CI)) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      continue;
    }

    // Roadmap L74: `OpImageQueryLevels` -- an image's own total mip-level
    // count. Unlike `OpImageQuerySizeLod` immediately above,
    // `createQueryLevels`'s own scalar `i32` result never varies by
    // shape (`FemeRTImageDescriptor::MipLevels` is tracked identically
    // regardless of dimensionality/arrayed-ness), so this needs no new
    // per-shape builder at all -- every classifiable *non-multisampled*
    // shape is accepted (`Plain1D`/`Array1D`/`Plain2D`/`Array2D`/
    // `Plain3D`/`Cube`/`CubeArray`). `Plain2DMS`/`Array2DMS` are still
    // rejected: GLSL has no `textureQueryLevels()` overload for a
    // multisampled sampler in the first place (a multisampled image
    // always has exactly one mip level), so no real CTS case ever
    // exercises this combination, and accepting it would be an
    // unverified, untested widening for no real benefit.
    if (isQueryLevelsCall(*CI)) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      continue;
    }

    // Roadmap L73: `OpImageQuerySamples`, a multisampled image's own
    // sample count -- unlike `isQuerySizeLodCall`/`isQueryLevelsCall`
    // immediately above, this is spec-legal *only* against a multisampled
    // image (`Plain2DMS`/`Array2DMS`, the one shape pair those two calls
    // deliberately do not accept), so its own shape check is the inverse
    // of theirs rather than an overlapping widening.
    if (isQuerySamplesCall(*CI)) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (Shape != ImageShape::Plain2DMS && Shape != ImageShape::Array2DMS)
        return false;
      continue;
    }

    bool ExplicitLod = false;
    bool HasMinLodClamp = false;
    bool HasBias = false;
    bool HasGrad = false;
    if (isSampleIntrinsic(*CI, ExplicitLod, HasMinLodClamp, HasBias, HasGrad)) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      // Roadmap L109/L125(a)/L125(b): an ordinary sample against an
      // integer-channel (`_UINT`/`_SINT`) image is legal SPIR-V
      // (`OpImageSampleExplicitLod`/`OpImageSampleImplicitLod` against a
      // `usampler2D`/`isampler2D`/`usampler1D`/`isampler1D`), just
      // restricted, per the Vulkan spec, to `NEAREST` filtering: no
      // `Bias`/`Grad` (SPIR-V forbids both alongside the mandatory
      // `NEAREST` filtering in every case this pass has needed to
      // support so far), and no `MinLod` clamp (neither
      // `createSample2DI32` nor `createSample1DI32` has such an
      // operand). `Plain2D` (roadmap H109, widened to implicit-LOD by
      // L125(a)) and `Plain1D` (roadmap L125(b), mirroring `Plain2D`'s
      // own widening exactly) are the only shapes accepted so far;
      // `lowerImageAccesses` below already defaults `Lod` to a constant
      // `0.0` whenever `ExplicitLod` is false (see its own comment),
      // which is exactly right here too -- every real CTS case either
      // widening covers samples a single-mip-level image, so the true
      // (unimplemented) derivative-based implicit-LOD computation would
      // clamp to mip 0 regardless.
      if (IsInteger) {
        if ((Shape != ImageShape::Plain2D && Shape != ImageShape::Plain1D) ||
            HasMinLodClamp || HasBias || HasGrad)
          return false;
        unsigned OffsetIdx =
            getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad);
        if (!isCoordN(CI->getArgOperand(2), SampleCoordWidth,
                      /*Float=*/true) ||
            !isSupportedOffset(CI->getArgOperand(OffsetIdx), Shape,
                               /*AllowArray2D=*/false,
                               /*AllowPlain1DArray1D=*/Shape ==
                                   ImageShape::Plain1D) ||
            !isV4I32(CI->getType()))
          return false;
        continue;
      }
      // Roadmap L73: `Plain2DMS`/`Array2DMS` (a multisampled sampled
      // image) has no ordinary filtered-sample counterpart -- no
      // `runtime/CPU` helper exists to sample a multisampled image, and
      // SPIR-V itself never legalizes `OpImageSampleImplicitLod`/
      // `OpImageSampleExplicitLod` against one -- so this must be
      // explicitly rejected now that `classifySampledImage2DHandle`
      // produces this shape for `OpImageQuerySamples`'s own sole use.
      if (Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      // Roadmap L26/L60(a)/L61(c)/L67(a): `lowerImageAccesses` threads a
      // `MinLod` clamp through `Plain2D`'s/`Cube`'s/`CubeArray`'s/
      // `Array2D`'s own `createSample2D`/`createSampleCube`/
      // `createSampleCubeArray`/`createSample2DArray` calls, `Plain1D`'s/
      // `Array1D`'s own `createSample1D`/`createSample1DArray` calls
      // (roadmap L61(c)), and `Plain3D`'s own `createSample3D` call
      // (roadmap L67(a)) -- every ordinary-sampling shape now threads a
      // real clamp rather than silently dropping it.
      if (HasMinLodClamp && Shape != ImageShape::Plain2D &&
          Shape != ImageShape::Cube && Shape != ImageShape::CubeArray &&
          Shape != ImageShape::Array2D && Shape != ImageShape::Plain1D &&
          Shape != ImageShape::Array1D && Shape != ImageShape::Plain3D)
        return false;
      // Roadmap L58/L60(a)/L61(c)/L67(a): same restriction for `Bias`,
      // mirroring `HasMinLodClamp` immediately above -- `Plain2D`'s/
      // `Cube`'s/`CubeArray`'s/`Array2D`'s own `createSample2D`/
      // `createSampleCube`/`createSampleCubeArray`/`createSample2DArray`
      // calls all thread a real `Bias` operand through
      // `lowerImageAccesses`, `Plain1D`'s/`Array1D`'s own `createSample1D`/
      // `createSample1DArray` calls do too (roadmap L61(c)), and so does
      // `Plain3D`'s own `createSample3D` call now (roadmap L67(a)).
      if (HasBias && Shape != ImageShape::Plain2D &&
          Shape != ImageShape::Cube && Shape != ImageShape::CubeArray &&
          Shape != ImageShape::Array2D && Shape != ImageShape::Plain1D &&
          Shape != ImageShape::Array1D && Shape != ImageShape::Plain3D)
        return false;
      // Roadmap L59/L60(a)/L65: same restriction for `Grad`, mirroring
      // `HasBias` immediately above -- `Plain2D`'s/`Cube`'s/`CubeArray`'s/
      // `Array2D`'s own `createSample2D`/`createSampleCube`/
      // `createSampleCubeArray`/`createSample2DArray` calls all have a
      // real screen-space derivative pair to feed a caller-supplied
      // `Grad` into (see `lowerImageAccesses`'s own `HasGrad` handling
      // below, which reuses exactly the same `DUdX`/`DUdY`/`DVdX`/`DVdY`/
      // `DDirXdX`/... operands `getOrSynthesizeSample2DDerivatives`/
      // `getOrSynthesizeSampleCubeDerivatives` already populate for an
      // implicit-LOD sample, just with the caller's own real values
      // instead of a synthesized or zeroed one). Roadmap L65: `Plain1D`/
      // `Array1D`'s own `createSample1D`/`createSample1DArray` calls
      // already carry the identical `DUdX`/`DUdY` derivative pair (added
      // by roadmap L63 for synthesized implicit-LOD derivatives), so a
      // caller-supplied `Grad` reuses that same pair -- these two shapes
      // are not a materially bigger prerequisite the way `Plain3D`'s own
      // still-nonexistent ordinary-sampling infrastructure is. Roadmap
      // L67(b): `Plain3D`'s own `createSample3D` call already carries a
      // real `DUdX`/`DUdY`/`DVdX`/`DVdY`/`DWdX`/`DWdY` derivative triple
      // (added by roadmap L66(a) for synthesized implicit-LOD
      // derivatives), so a caller-supplied `Grad` reuses that same triple
      // the same way `Plain1D`/`Array1D` reuse their own pair above.
      if (HasGrad && Shape != ImageShape::Plain2D &&
          Shape != ImageShape::Cube && Shape != ImageShape::CubeArray &&
          Shape != ImageShape::Array2D && Shape != ImageShape::Plain1D &&
          Shape != ImageShape::Array1D && Shape != ImageShape::Plain3D)
        return false;
      unsigned OffsetIdx = getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad);
      // Roadmap L59/L64: `Grad`'s own `dPdx`/`dPdy` operands (indices 3,
      // 4) are each a screen-space partial derivative of `Coord`, one
      // component per *differentiable* coordinate component -- which is
      // not always `Coord`'s own width. SPIR-V requires a `Grad`
      // derivative to have "the number of components ... equal to the
      // number of dimensions of the image, not counting the array layer":
      // an arrayed sample's layer index selects a discrete slice rather
      // than addressing a filtered axis, so it has no derivative at all.
      // `Array2D` therefore pairs a 3-component `(U, V, Layer)` coordinate
      // with 2-component derivatives, and `CubeArray` a 4-component
      // `(X, Y, Z, Layer)` coordinate with 3-component ones, while the
      // two non-arrayed shapes' derivatives do match their coordinate.
      // (`lowerImageAccesses` below already reads exactly these narrower
      // widths -- elements 0/1 for `Array2D`, 0/1/2 for `CubeArray` --
      // so only this check ever disagreed.)
      unsigned GradDerivativeWidth =
          isArrayedShape(Shape) ? SampleCoordWidth - 1 : SampleCoordWidth;
      if (HasGrad && (!isCoordN(CI->getArgOperand(3), GradDerivativeWidth,
                                /*Float=*/true) ||
                      !isCoordN(CI->getArgOperand(4), GradDerivativeWidth,
                                /*Float=*/true)))
        return false;
      // Roadmap L33: an ordinary (non-comparison) sample's own
      // `ConstOffset` is now also accepted against `Array2D`, mirroring
      // the depth-comparison path's own pre-existing `AllowArray2D`
      // acceptance immediately below (roadmap L50d) -- SPIR-V's own
      // `ConstOffset` image operand is equally legal against an arrayed
      // `OpImageSampleImplicitLod`/`OpImageSampleExplicitLod` as it is
      // against a plain one. Roadmap L66(d): `AllowPlain1DArray1D`
      // additionally accepts the same real, nonzero offset against
      // `Plain1D`/`Array1D` -- roadmap L66(k) widens the depth-comparison
      // path below to accept the identical case too (see
      // `isSupportedOffset`'s own comment).
      if (!isCoordN(CI->getArgOperand(2), SampleCoordWidth, /*Float=*/true) ||
          !isSupportedOffset(CI->getArgOperand(OffsetIdx), Shape,
                             /*AllowArray2D=*/true,
                             /*AllowPlain1DArray1D=*/true) ||
          !isV4F32(CI->getType()))
        return false;
      continue;
    }

    // Roadmap L48: extended from a `Plain2D`-only depth-comparison sample
    // (roadmap L46) to also cover `Array2D`/`Cube`/`CubeArray`, each of
    // which now has its own `createSampleCmpArray2D`/`createSampleCmpCube`/
    // `createSampleCmpCubeArray` counterpart. Roadmap L54 further extends
    // this to `Plain1D`/`Array1D` (`SampleCmp1D`/`SampleCmpArray1D`), now
    // that L52a's own ordinary `Sample1D`/`Sample1DArray` infrastructure
    // exists as a prerequisite. `samplecmp_clamp`'s own trailing `MinLod`
    // clamp operand (roadmap L52(c)) is now recognized for
    // `Plain2D`/`Array2D`/`Cube`/`CubeArray` (mirroring `HasMinLodClamp`'s
    // own identical shape restriction for an ordinary sample), but is
    // deliberately still rejected for `Plain1D`/`Array1D` -- neither
    // `createSampleCmp1D` nor `createSampleCmpArray1D` threads a clamp
    // value through, no real CTS case having reached it yet; a nonzero
    // `ConstOffset` (roadmap L50d) is now accepted for `Plain2D`/
    // `Array2D`, mirroring `isSupportedOffset`'s identical `Plain2D`-only
    // precedent for an ordinary sample, and roadmap L66(k) further widens
    // this to `Plain1D`/`Array1D` too (`AllowPlain1DArray1D`, see
    // `isSupportedOffset`'s own comment) -- only `Cube`/`CubeArray` still
    // require the trivial always-zero case, since SPIR-V forbids a real
    // `ConstOffset` against a cube image outright.
    bool DrefExplicitLod = false;
    bool DrefHasClamp = false;
    bool DrefHasBias = false;
    bool DrefHasGrad = false;
    bool DrefHasLevel = false;
    if (isDrefSampleIntrinsic(*CI, DrefExplicitLod, DrefHasClamp, DrefHasBias,
                              DrefHasGrad, DrefHasLevel)) {
      if (IsInteger)
        return false; // No filtered/dref sample over an integer format.
      if (CI->getArgOperand(0) != &Handle)
        return false;
      // Roadmap L73: same rejection as the ordinary-sample branch above
      // -- no depth-comparison sample against a multisampled sampled
      // image is legal either.
      if (Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      // Roadmap L66(c) scoped a depth-comparison `Grad` sample to
      // `Plain2D` only; roadmap L66(f) widened this to also accept
      // `Plain1D`/`Array1D`; roadmap L66(g) further widens it to
      // `Array2D`; roadmap L66(h) further widens it to `Cube`, whose own
      // `dPdx`/`dPdy` are a 3-wide direction-vector derivative pair
      // (mirroring `GradDerivativeWidth`'s own unarrayed-shape case,
      // `SampleCoordWidth` itself already being 3 for `Cube`, and
      // matching `createSampleCube`'s own identical `Grad` precedent for
      // a non-`Dref` sample, roadmap L59); roadmap L66(i) further widens
      // it to `CubeArray` too, whose own `dPdx`/`dPdy` needed no further
      // width change at all -- `GradDerivativeWidth`'s own generalized
      // "arrayed shapes drop one component" formula already resolves
      // `CubeArray` (arrayed, `SampleCoordWidth == 4`) to the same 3-wide
      // derivative `Cube` itself uses, confirming the formula's own
      // design was already correct for this shape too, mirroring L66(g)'s
      // own identical finding for `Array2D` versus `Plain2D`.
      if (DrefHasGrad && Shape != ImageShape::Plain2D &&
          Shape != ImageShape::Plain1D && Shape != ImageShape::Array1D &&
          Shape != ImageShape::Array2D && Shape != ImageShape::Cube &&
          Shape != ImageShape::CubeArray)
        return false;
      // SPIR-V's own validation rules give a depth-comparison sample's
      // Coordinate operand one extra component beyond the shape's own
      // ordinary addressing width, capped at SPIR-V's own 4-component
      // vector ceiling -- glslang always emits this for e.g.
      // `texture(sampler2DShadow, vec3(u, v, compare))`, packing the
      // depth-reference value redundantly alongside `Dref` itself (a
      // separate operand, still read from its own fixed index below);
      // only the shape's own ordinary components are ever read as the
      // real address (see the `C0`/`C1`/... extraction in
      // `lowerImageAccesses`). Confirmed via a real `deqp-vk` SPIR-V
      // capture of `sampler2darrayshadow`/`samplercube{,array}shadow`
      // (roadmap L48): `CubeArray`'s own ordinary width (4) is already
      // this ceiling, so its own Dref sample coordinate stays 4-wide
      // with no further padding, and its `Dref` arrives as a genuinely
      // independent operand rather than an echo of the coordinate's own
      // last component (still read the same way below either way).
      // `Plain1D` is the one shape where this "+1" rule does not hold:
      // a real `deqp-vk` SPIR-V capture (roadmap L54, `--deqp-log-
      // decompiled-spirv=enable` against `sampler1dshadow_fragment`)
      // shows glslang always emits a fixed 3-component coordinate for a
      // 1D shadow sampler, mirroring GLSL's own `sampler1DShadow`
      // combined-coordinate convention (`vec3(u, <unused>, compare)`,
      // per the GLSL spec's own "Combined Texture and Shadow Samplers"
      // table) rather than padding `Plain1D`'s own bare-scalar ordinary
      // width (1) by one. `Array1D`'s own dref coordinate is genuinely
      // 3-wide too (`vec3(u, layer, compare)`, confirmed by the same
      // capture technique against `sampler1darrayshadow_fragment`), but
      // that already matches the generic "+1" rule (`SampleCoordWidth`
      // 2 + 1), so only `Plain1D` needs an explicit override here.
      // Roadmap L7b: `dxc`'s own real SPIR-V output for HLSL's
      // `Texture*::SampleCmp`/`SampleCmpLevelZero` (confirmed via a real
      // `vk::SampledTexture2D`+`SampleCmp` repro compiled with `dxc
      // -fspv-target-env=vulkan1.3`) does *not* follow glslang's own
      // redundant-padding convention above at all: its Coordinate operand
      // stays exactly `SampleCoordWidth` wide (the shape's own ordinary
      // addressing width, identical to an *ordinary*, non-comparison
      // sample's Coordinate), with `Dref` arriving purely through its own
      // separate operand and nothing echoed into the coordinate's own
      // trailing component. Both widths are accepted here for every
      // shape but `Plain1D` (`lowerImageAccesses` already only ever
      // reads the shape's own ordinary `C0`/`C1`/... components by fixed
      // index, so an unread, potentially-absent trailing padding
      // component was never actually load-bearing there) rather than
      // replacing the glslang-derived width outright, so real
      // GLSL-originated modules already exercising the padded shape
      // (roadmap L46/L48) keep working unchanged. `Plain1D` is excluded
      // from this widening: unlike every other shape, its own `C0`/`C1`
      // extraction just below is unconditional (not gated by `Shape`),
      // and a bare-scalar dxc-style coordinate (this shape's own
      // unpadded `SampleCoordWidth` of 1, per `isCoordN`'s own N==1
      // special case) is not a vector `CreateExtractElement` can apply
      // to at all -- no real HLSL/dxc `Texture1D::SampleCmp` case has
      // been confirmed to even reach this path yet, so accepting a
      // shape this pre-existing code cannot actually consume would only
      // trade one crash-free rejection for a real crash.
      unsigned DrefCoordWidth =
          Shape == ImageShape::Plain1D
              ? 3
              : (SampleCoordWidth + 1 > 4 ? 4 : SampleCoordWidth + 1);
      bool AcceptsUnpaddedDxcWidth = Shape != ImageShape::Plain1D;
      if (!(isCoordN(CI->getArgOperand(2), DrefCoordWidth, /*Float=*/true) ||
            (AcceptsUnpaddedDxcWidth &&
             isCoordN(CI->getArgOperand(2), SampleCoordWidth,
                      /*Float=*/true))) ||
          !CI->getArgOperand(DrefSampleDrefIdx)->getType()->isFloatTy() ||
          // Roadmap L66(k): `AllowPlain1DArray1D` now also accepts a
          // real, nonzero `ConstOffset` here, mirroring the ordinary
          // (non-comparison) sample path's own identical
          // `AllowPlain1DArray1D` acceptance above.
          !isSupportedOffset(CI->getArgOperand(getDrefSampleOffsetIdx(
                                 DrefHasBias, DrefHasGrad, DrefHasLevel)),
                             Shape,
                             /*AllowArray2D=*/true,
                             /*AllowPlain1DArray1D=*/true) ||
          (DrefHasBias &&
           !CI->getArgOperand(DrefSampleBiasIdx)->getType()->isFloatTy()) ||
          // Roadmap L72(b): `samplecmplevel`'s own real `Lod` operand is
          // validated the same way `DrefHasBias`'s own `Bias` operand is
          // just above -- a plain scalar float, mirroring
          // `isSampleIntrinsic`'s own `ExplicitLod` operand (never
          // shape-restricted beyond the general dref-sample rejections
          // already applied above).
          (DrefHasLevel &&
           !CI->getArgOperand(DrefSampleLevelIdx)->getType()->isFloatTy()) ||
          // Roadmap L66(c)/L66(f)/L66(g)/L66(h): `Grad`'s own `dPdx`/`dPdy`
          // pair is validated against the same derivative width an
          // ordinary sample's own `Grad` operand uses (`GradDerivativeWidth`,
          // narrowed by one for an arrayed shape since the array layer
          // has no derivative of its own) -- never the dref-padded
          // `DrefCoordWidth` -- mirroring `GradDerivativeWidth`'s own
          // identical precedent for a non-`Dref` `Grad` sample. For
          // `Plain2D` this is 2 (unarrayed, matches `SampleCoordWidth`);
          // for `Plain1D` it is 1 (unarrayed, `SampleCoordWidth` is
          // already 1); for `Array1D` it is 1 too (arrayed,
          // `SampleCoordWidth` 2 minus the array layer); for `Array2D` it
          // is 2 (arrayed, `SampleCoordWidth` 3 minus the array layer --
          // the same 2-wide derivative width `Plain2D` has, just over a
          // `(U, V)` pair extracted ahead of the array layer rather than
          // the whole coordinate); for `Cube` it is 3 (unarrayed,
          // `SampleCoordWidth` is already 3 -- a full 3-component
          // direction-vector derivative, mirroring `createSampleCube`'s
          // own identical `Grad` precedent, roadmap L59).
          (DrefHasGrad &&
           (!isCoordN(CI->getArgOperand(DrefSampleGradDPdxIdx),
                      isArrayedShape(Shape) ? SampleCoordWidth - 1
                                            : SampleCoordWidth,
                      /*Float=*/true) ||
            !isCoordN(CI->getArgOperand(DrefSampleGradDPdyIdx),
                      isArrayedShape(Shape) ? SampleCoordWidth - 1
                                            : SampleCoordWidth,
                      /*Float=*/true))) ||
          (DrefHasClamp &&
           !CI->getArgOperand(
                  getDrefSampleClampIdx(DrefHasBias, DrefHasGrad, DrefHasLevel))
                ->getType()
                ->isFloatTy()) ||
          !CI->getType()->isFloatTy())
        return false;
      continue;
    }

    // Roadmap L7d/H124q/H124r: `spirv.ImageDrefGather` (HLSL's
    // `Texture2D::GatherCmp()`/`Texture2DArray::GatherCmp()`/
    // `TextureCube::GatherCmp()`), scoped to `Plain2D`/`Array2D`/`Cube`
    // only for now (`CubeArray`'s counterpart -- legal per the op's own
    // SPIR-V type constraints -- remains unstarted follow-on work, no
    // real repro having reached it yet). Its own fixed `(image, sampler,
    // coord, dref, offset)` operand shape lets it reuse
    // `DrefSampleDrefIdx`/`getDrefSampleOffsetIdx(false)` from the plain
    // `spv_resource_samplecmp` family above, but its result is always a
    // full `<4 x float>` (roadmap L7d's own filing text), never a scalar
    // the way every `isDrefSampleIntrinsic` case's own result is, so it
    // cannot share that branch's own `!CI->getType()->isFloatTy()`
    // rejection, and needs `isV4F32` instead. `SampleCoordWidth` is
    // already 3 for both `Array2D` and `Cube` (this function's own
    // initial per-shape table above), so `isCoordN` widens automatically;
    // `AllowArray2D` is `true` in `isSupportedOffset` below since a real
    // case (`Array.GatherCmp.test`'s own `int2(1, 0)`-offset overload)
    // needs a genuine nonzero `Array2D` gather offset, not just the
    // always-accepted zero one -- `Cube`'s own offset always falls
    // through to `isSupportedOffset`'s unconditional `isZeroOffset` case
    // regardless (HLSL's `TextureCube::GatherCmp()` has no offset
    // overload at all, and SPIR-V forbids `ConstOffset` against
    // `Dim::Cube` outright), so no further widening was needed there.
    if (isGatherCmpIntrinsic(*CI)) {
      if (IsInteger ||
          (Shape != ImageShape::Plain2D && Shape != ImageShape::Array2D &&
           Shape != ImageShape::Cube))
        return false; // No filtered/dref/gather sample over an integer
                      // format; CubeArray remains unstarted follow-on
                      // work (roadmap L7d).
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (!isCoordN(CI->getArgOperand(2), SampleCoordWidth, /*Float=*/true) ||
          !CI->getArgOperand(DrefSampleDrefIdx)->getType()->isFloatTy() ||
          !isSupportedOffset(CI->getArgOperand(getDrefSampleOffsetIdx(false)),
                             Shape, /*AllowArray2D=*/true,
                             /*AllowPlain1DArray1D=*/false) ||
          !isV4F32(CI->getType()))
        return false;
      continue;
    }

    // Roadmap L7g/H124q/H124r: `spirv.ImageGather` (HLSL's
    // `Texture2D::Gather{,Red,Green,Blue,Alpha}()`/`Texture2DArray::
    // Gather{,Red,Green,Blue,Alpha}()`/`TextureCube::Gather{,Red,Green,
    // Blue,Alpha}()`), scoped to `Plain2D`/`Array2D`/`Cube`, non-integer
    // only for now -- identical scope to `isGatherCmpIntrinsic`'s own
    // just above (`CubeArray` and an integer-format image, both legal
    // per the op's own SPIR-V type constraints, remain unstarted
    // follow-on work, no real repro having reached either yet). Its own
    // fixed `(image, sampler, coord, component, offset)` operand shape
    // is identical to `isGatherCmpIntrinsic`'s own, except the
    // `DrefSampleDrefIdx` position holds an integer component selector
    // rather than a float `Dref`, so it needs its own
    // `isCoordN(..., /*Float=*/false)`-style integer check there
    // instead. `AllowArray2D` is `true` here too, mirroring
    // `isGatherCmpIntrinsic`'s own identical widening just above, for
    // consistency even though no real `Array.Gather.test` overload
    // currently exercises a nonzero offset.
    if (isGatherIntrinsic(*CI)) {
      if (IsInteger ||
          (Shape != ImageShape::Plain2D && Shape != ImageShape::Array2D &&
           Shape != ImageShape::Cube))
        return false; // No gather over an integer format; CubeArray
                      // remains unstarted follow-on work (roadmap L7g).
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (!isCoordN(CI->getArgOperand(2), SampleCoordWidth, /*Float=*/true) ||
          !CI->getArgOperand(DrefSampleDrefIdx)->getType()->isIntegerTy() ||
          !isSupportedOffset(CI->getArgOperand(getDrefSampleOffsetIdx(false)),
                             Shape, /*AllowArray2D=*/true,
                             /*AllowPlain1DArray1D=*/false) ||
          !isV4F32(CI->getType()))
        return false;
      continue;
    }

    // Roadmap L52e/H124t/H124u: `OpImageQueryLod`'s own two intrinsic
    // halves (`calculate.lod`/`calculate.lod.unclamped`), scoped to
    // `Plain2D`/`Array2D`/`Cube` -- `CubeArray`/`Plain1D`/`Array1D`/
    // `Plain3D` counterparts remain unstarted follow-on work, mirroring
    // this same narrowing's precedent (e.g. roadmap L46's own initial
    // `Plain2D`-only depth-comparison-sample scope, later widened by
    // L48). An integer-channel image is rejected the same way an
    // ordinary/dref sample is above -- SPIR-V never legalizes
    // `OpImageQueryLod` against one either. Unlike an ordinary sample,
    // `OpImageQueryLod`'s own coordinate is always exactly 2 components
    // against a `Plain2D`/`Array2D` handle (`Texture2DArray::
    // CalculateLevelOfDetail`'s own HLSL signature has no slice argument
    // at all -- confirmed via `spirv-dis`, `%v2float` regardless of
    // shape -- the array dimension plays no part in the LOD computation),
    // but a `Cube` handle's own coordinate is instead a 3-component
    // direction vector (`TextureCube::CalculateLevelOfDetail`'s own
    // `%v3float` coordinate -- also confirmed via `spirv-dis`, since
    // there is no 2D face-local UV until face selection happens inside
    // the runtime function itself), so this uses a fixed width of 2 for
    // `Plain2D`/`Array2D` but 3 for `Cube`, rather than `SampleCoordWidth`'s
    // own per-shape value.
    bool Unclamped = false;
    if (isQueryLodIntrinsic(*CI, Unclamped)) {
      if (IsInteger ||
          (Shape != ImageShape::Plain2D && Shape != ImageShape::Array2D &&
           Shape != ImageShape::Cube))
        return false;
      if (CI->getArgOperand(0) != &Handle)
        return false;
      unsigned QueryLodCoordWidth = Shape == ImageShape::Cube ? 3 : 2;
      if (!isCoordN(CI->getArgOperand(2), QueryLodCoordWidth, /*Float=*/true) ||
          !CI->getType()->isFloatTy())
        return false;
      continue;
    }

    // Roadmap L72/L72(b)/L72(c): an explicit-mip `texelFetch()`
    // (`llvm.spv.resource.load.level`, see `isFetchLevelIntrinsic`'s own
    // doc) against a sampled handle -- the second, `Lod`-carrying raised
    // form of `OpImageFetch`, alongside the zero-mip `getpointer`-based
    // one just below. `Plain2D`/`Array2D` were this intrinsic's original
    // scope (roadmap L72); `Plain1D`/`Array1D`/`Plain3D` (roadmap L72(c))
    // widen it to every shape the zero-mip `getpointer` fetch path below
    // already supports for a *storage* image, since `texelFetch()` always
    // supplies an explicit LOD for every shape GLSL defines it for, not
    // just `Plain2D`/`Array2D` -- confirmed via a real re-run of roadmap
    // L72's own 1,375-case caselist, whose every remaining "cannot
    // normalize" failure was exactly a `texelfetch.*1d*`/`texelfetch.*3d*`
    // variant. `Cube`/`CubeArray`/`Plain2DMS`/`Array2DMS` remain excluded,
    // mirroring the same restrictions the zero-mip path documents just
    // below. `SampleCoordWidth` (computed once above) already gives the
    // right fetch coordinate width for every shape accepted here --
    // `texelFetch()`'s own integer coordinate has the identical
    // dimensionality an ordinary sample's own coordinate does, just with
    // no `Dref`-style widening to account for (fetch has no depth
    // comparison). Roadmap L72(b): the intrinsic's fourth operand
    // (`Offset`) is now allowed to be a real, nonzero `ConstOffset` too
    // (GLSL's `texelFetchOffset()`), validated the same way an ordinary
    // sample's own `ConstOffset` is (`AllowArray2D=true`/
    // `AllowPlain1DArray1D=true`, mirroring the identical acceptance
    // `isSupportedOffset` already grants an ordinary sample against these
    // same shapes -- confirmed via a real `deqp-vk` SPIR-V capture).
    if (isFetchLevelIntrinsic(*CI)) {
      if (Shape == ImageShape::Cube || Shape == ImageShape::CubeArray ||
          Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (!isCoordN(CI->getArgOperand(1), SampleCoordWidth, /*Float=*/false) ||
          !CI->getArgOperand(2)->getType()->isIntegerTy(32) ||
          !isSupportedOffset(CI->getArgOperand(3), Shape,
                             /*AllowArray2D=*/true,
                             /*AllowPlain1DArray1D=*/true) ||
          !(IsInteger ? isV4I32(CI->getType()) : isV4F32(CI->getType())))
        return false;
      continue;
    }

    if (Shape == ImageShape::Cube || Shape == ImageShape::CubeArray ||
        Shape == ImageShape::Plain1D || Shape == ImageShape::Array1D ||
        Shape == ImageShape::Plain3D || Shape == ImageShape::Plain2DMS ||
        Shape == ImageShape::Array2DMS)
      return false; // No fetch shape exists for Cube/CubeArray/Plain1D/
                    // Array1D yet (roadmap L52a: ordinary sampling only),
                    // nor for a sampled `Plain3D` handle's own `OpImageFetch`
                    // (roadmap L66(a): ordinary *sample* only this row), nor
                    // for a multisampled sampled image's own `texelFetch()`
                    // (roadmap L73: `OpImageQuerySamples` is the sole
                    // operation this shape supports -- already dispatched
                    // and `continue`d above -- no `runtime/CPU` helper
                    // exists to fetch a multisampled sampled image's texel).
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
/// H19a/H19b/H19c/H19d/H19e/H19g) is a plain `getpointer` texel access
/// (`OpImageRead`/`OpImageWrite`, same shape `hasOnlySupportedImageUses`
/// accepts for a sampled image's own fetch), whose own users are `Load`s
/// and/or `Store`s of the matching `<4 x float>`/`<4 x i32>` type -- both
/// may appear on the *same* `getpointer` call, since the CTS's own
/// `load_store` shader pattern reads and writes the same binding (a
/// copy-shader idiom), unlike every other pointer-use check in this file,
/// which does not need to consider that. \p Shape is `Plain1D`, `Array1D`,
/// `Plain2D`, `Array2D`, `Plain3D`, `Plain2DMS`, or `Array2DMS` --
/// `classifyStorageImage2DHandle` never returns `Cube`/`CubeArray` for a
/// storage image today (a storage cube handle's own `Array2D` shape is
/// indistinguishable from an ordinary 2D array's here, roadmap H19d) --
/// and selects the coordinate width exactly like
/// `hasOnlySupportedImageUses`'s own `FetchCoordWidth`. `Array1D`'s
/// 2-component `(x, layer)` coordinate happens to share its width with
/// `Plain2D`'s `(x, y)`, so both fall into the same `else` branch below.
bool hasOnlySupportedStorageImageUses(const CallInst &Handle, bool IsInteger,
                                      ImageShape Shape) {
  unsigned CoordWidth =
      Shape == ImageShape::Plain1D     ? 1
      : Shape == ImageShape::Array2DMS ? 4
      : (Shape == ImageShape::Array2D || Shape == ImageShape::Plain3D ||
         Shape == ImageShape::Plain2DMS)
          ? 3
          : 2;
  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;

    // Roadmap L70: a plain 2D storage image's own `imageSize()` query
    // (`OpImageQuerySize`) -- see `hasOnlySupportedImageUses`'s own
    // identical check for why this is scoped to `Plain2D` only.
    if (isGetDimensionsIntrinsic(*CI)) {
      if (Shape != ImageShape::Plain2D)
        return false;
      continue;
    }

    // Roadmap H124s: an `Array2D` storage image's own Lod-less
    // `OpImageQuerySize` (`isGetDimensions3Intrinsic`) -- see its own doc
    // for why a storage image's `GetDimensions` lowers to this bare
    // 3-component opcode rather than `OpImageQuerySizeLod`.
    if (isGetDimensions3Intrinsic(*CI)) {
      if (Shape != ImageShape::Array2D)
        return false;
      continue;
    }

    // Roadmap L72(d)/L75: `OpImageQuerySizeLod` against a storage image --
    // see `hasOnlySupportedImageUses`'s own identical check for this
    // opcode's shape scoping and operand convention (a storage handle
    // never classifies as `Cube`/`CubeArray` -- `classifyStorageImage2DHandle`
    // folds those into `Array2D` -- so only `Plain2DMS`/`Array2DMS`
    // need excluding here, never `Cube`/`CubeArray` specifically).
    if (isQuerySizeLodCall(*CI)) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      continue;
    }

    // Roadmap L74: `OpImageQueryLevels` against a storage image -- see
    // `hasOnlySupportedImageUses`'s own identical check for why this
    // needs no per-shape builder and is widened to every non-multisampled
    // shape.
    if (isQueryLevelsCall(*CI)) {
      if (CI->getArgOperand(0) != &Handle)
        return false;
      if (Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
        return false;
      continue;
    }

    if (getIntrinsicID(CI) != Intrinsic::spv_resource_getpointer)
      return false;
    if (!isCoordN(CI->getArgOperand(1), CoordWidth, /*Float=*/false))
      return false;
    for (const User *PU : CI->users()) {
      if (const auto *LI = dyn_cast<LoadInst>(PU)) {
        // Roadmap L76(a): a Load's own result may be any width
        // `storageImageTexelWidth` accepts (1-4), not only the full
        // 4-wide vector -- `lowerImageAccesses` below narrows the fetch's
        // always-4-wide runtime result down to this real width.
        if (!storageImageTexelWidth(LI->getType(), IsInteger))
          return false;
        continue;
      }
      if (const auto *SI = dyn_cast<StoreInst>(PU)) {
        if (SI->getPointerOperand() != CI)
          return false;
        // Roadmap L76(a): same width widening as the Load case above,
        // but in reverse -- `lowerImageAccesses` widens this narrower
        // Texel operand up to the full 4-wide vector the runtime store
        // entry point requires.
        if (!storageImageTexelWidth(SI->getValueOperand()->getType(),
                                    IsInteger))
          return false;
        continue;
      }
      // A `getpointer` user materialized by `ImageTexelPointerPattern`
      // (`SPIRVToLLVMPatterns.cpp`, roadmap H8v) is scalar-typed rather
      // than a `<4 x i32>`/`<4 x float>` texel, so it never reaches the
      // `LoadInst`/`StoreInst` branches above -- an image atomic
      // (`AtomicRMWInst`/`AtomicCmpXchgInst`) only exists over a single-
      // 32-bit-scalar *integer* storage-image format
      // (`R32_SINT`/`R32_UINT`; SPIR-V disallows an atomic against a
      // float-channel image outright), and only `Plain2D` is implemented
      // today -- widening to every other `ImageShape` is left as future
      // work (see Design.md's own H8v note).
      if (const auto *RMW = dyn_cast<AtomicRMWInst>(PU)) {
        if (!IsInteger || Shape != ImageShape::Plain2D)
          return false;
        if (RMW->getPointerOperand() != CI ||
            !RMW->getValOperand()->getType()->isIntegerTy(32))
          return false;
        switch (RMW->getOperation()) {
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
          continue;
        default:
          return false;
        }
      }
      if (const auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(PU)) {
        if (!IsInteger || Shape != ImageShape::Plain2D)
          return false;
        if (CmpXchg->getPointerOperand() != CI ||
            !CmpXchg->getCompareOperand()->getType()->isIntegerTy(32))
          return false;
        // `AtomicCompareExchangePattern` (`SPIRVToLLVMPatterns.cpp`)
        // always follows an `llvm.cmpxchg` with exactly one
        // `extractvalue ..., 0` picking out the old value (SPIR-V's own
        // result); reject anything else so `lowerImageAccesses` below can
        // assume that shape unconditionally.
        for (const User *CU : CmpXchg->users()) {
          const auto *EV = dyn_cast<ExtractValueInst>(CU);
          if (!EV || EV->getIndices() != ArrayRef<unsigned>{0})
            return false;
        }
        continue;
      }
      return false;
    }
  }
  return true;
}

/// Checks that every use of a sampler handle is the sampler operand of a
/// sample intrinsic (`isSampleIntrinsic`), a depth-comparison sample
/// intrinsic (`isDrefSampleIntrinsic`, roadmap L46/L66(c)), a LOD-query
/// intrinsic (`isQueryLodIntrinsic`, roadmap L52e), or a depth-comparison
/// gather intrinsic (`isGatherCmpIntrinsic`, roadmap L7d). A sampler has
/// no accesses of its own -- it only ever pairs with an image -- so there
/// is nothing else it can legitimately be.
bool hasOnlySupportedSamplerUses(const CallInst &Handle) {
  for (const User *U : Handle.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    bool ExplicitLod = false;
    bool HasMinLodClamp = false;
    bool HasBias = false;
    bool HasGrad = false;
    bool HasClamp = false;
    bool DrefHasGrad = false;
    bool DrefHasLevel = false;
    bool Unclamped = false;
    if (!CI || !(isSampleIntrinsic(*CI, ExplicitLod, HasMinLodClamp, HasBias,
                                   HasGrad) ||
                 isDrefSampleIntrinsic(*CI, ExplicitLod, HasClamp, HasBias,
                                       DrefHasGrad, DrefHasLevel) ||
                 isQueryLodIntrinsic(*CI, Unclamped) ||
                 isGatherCmpIntrinsic(*CI) || isGatherIntrinsic(*CI)))
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
/// \p AllowGEPs, `getelementptr` shapes this pass can lower. When
/// \p Writable is set (`HandleKind::Storage`/`StorageStruct`/`TexelStorage`
/// only -- a uniform/uniform-array/texel-uniform buffer's own
/// `Writable == false` already excludes it, matching Vulkan's read-only
/// restriction on
/// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_UNIFORM_TEXEL_BUFFER`), a
/// scalar-`i32` `AtomicRMWInst`/`AtomicCmpXchgInst` direct user of \p Ptr is
/// also accepted (roadmap H8w/H8x): a storage-texel-buffer atomic
/// (`OpAtomicIAdd`/`OpAtomicExchange`/etc. against an
/// `OpImageTexelPointer` whose own image operand has `Dim == Buffer`) is
/// the identical SPIR-V/LLVM shape roadmap H8v's storage-*image* atomics
/// already use -- `ImageTexelPointerPattern`
/// (`SPIRVToLLVMPatterns.cpp`) materializes the same
/// `llvm.spv.resource.getpointer` call regardless of the underlying
/// image's `Dim`, so the same `getpointer` result this function already
/// walks is exactly what an `AtomicRMWInst`/`AtomicCmpXchgInst` here
/// operates on too, with no further conversion-layer work needed. An
/// ordinary storage-buffer/direct-field-storage-block atomic (roadmap H8x:
/// `OpAtomicIAdd`/etc. against a plain `OpAccessChain`-derived pointer, not
/// an `OpImageTexelPointer`) reaches this exact same code path too, since
/// `IsTexel` plays no role in this check below -- only in the element-type
/// restriction `LoadInst`/`StoreInst` still apply above, which an atomic
/// never goes through (the RMW-opcode switch and cmpxchg-comparator check
/// below are already scalar-`i32`-only regardless of `IsTexel`, so no
/// further widening was needed once the `Writable` gate itself widened).
/// SPIR-V disallows an atomic against a float-channel texel buffer outright
/// (only `R32_SINT`/`R32_UINT` are mandatory formats for
/// `STORAGE_TEXEL_BUFFER_ATOMIC_BIT`), hence the scalar-`i32` restriction
/// rather than reusing `isSupportedTexelElementType`'s broader shape; an
/// ordinary storage buffer's own atomic is restricted to scalar `i32` for
/// the identical reason (`STORAGE_BUFFER_ATOMIC_BIT` --unlike
/// `SPIRVToLLVMPatterns.cpp`'s generic `Atomic*Pattern`s -- SPIR-V itself
/// only guarantees a 32-bit integer atomic across every implementation).
bool hasOnlySupportedPointerUses(const Value &Ptr, bool Writable, bool IsTexel,
                                 bool AllowGEPs, const DataLayout &DL) {
  for (const User *U : Ptr.users()) {
    if (const auto *LI = dyn_cast<LoadInst>(U)) {
      if (IsTexel ? !isSupportedTexelElementType(LI->getType())
                  : !isSupportedRawElementType(LI->getType())) {
        logNormalizationRejection("load of unsupported element type", LI);
        return false;
      }
      continue;
    }
    if (Writable)
      if (const auto *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() != &Ptr) {
          logNormalizationRejection("store where pointer is not the operand",
                                    SI);
          return false;
        }
        if (IsTexel
                ? !isSupportedTexelElementType(SI->getValueOperand()->getType())
                : !isSupportedRawElementType(SI->getValueOperand()->getType())) {
          logNormalizationRejection("store of unsupported element type", SI);
          return false;
        }
        continue;
      }
    if (Writable) {
      if (const auto *RMW = dyn_cast<AtomicRMWInst>(U)) {
        if (RMW->getPointerOperand() != &Ptr ||
            !RMW->getValOperand()->getType()->isIntegerTy(32)) {
          logNormalizationRejection(
              "atomicrmw not a scalar-i32 direct-pointer use", RMW);
          return false;
        }
        switch (RMW->getOperation()) {
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
          continue;
        default:
          logNormalizationRejection("atomicrmw unsupported operation", RMW);
          return false;
        }
      }
      if (const auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(U)) {
        if (CmpXchg->getPointerOperand() != &Ptr ||
            !CmpXchg->getCompareOperand()->getType()->isIntegerTy(32)) {
          logNormalizationRejection(
              "cmpxchg not a scalar-i32 direct-pointer use", CmpXchg);
          return false;
        }
        // `AtomicCompareExchangePattern` (`SPIRVToLLVMPatterns.cpp`)
        // always follows an `llvm.cmpxchg` with exactly one
        // `extractvalue ..., 0` picking out the old value (SPIR-V's own
        // result); reject anything else so `lowerAccesses` below can
        // assume that shape unconditionally (mirroring
        // `hasOnlySupportedStorageImageUses`'s own identical check).
        for (const User *CU : CmpXchg->users()) {
          const auto *EV = dyn_cast<ExtractValueInst>(CU);
          if (!EV || EV->getIndices() != ArrayRef<unsigned>{0}) {
            logNormalizationRejection(
                "cmpxchg result used other than by extractvalue 0", CU);
            return false;
          }
        }
        continue;
      }
    }
    if (AllowGEPs)
      if (const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        if (GEP->getPointerOperand() != &Ptr) {
          logNormalizationRejection(
              "getelementptr where pointer is not the base operand", GEP);
          return false;
        }
        if (!hasResolvableGEPByteOffset(*GEP, DL)) {
          logNormalizationRejection(
              "getelementptr with an unresolvable byte offset", GEP);
          return false;
        }
        if (!hasOnlySupportedPointerUses(*GEP, Writable, IsTexel, AllowGEPs,
                                         DL))
          return false; // Already logged by the recursive call.
        continue;
      }
    logNormalizationRejection("pointer used by an unrecognized instruction",
                             U);
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
/// storage-buffer or direct-field-storage-block kind (roadmap L16: also a
/// direct-field uniform/cbuffer kind, since a struct-typed cbuffer member
/// needs the identical further field navigation), an ordinary
/// `getelementptr` chain ending in such a load/store. For
/// `HandleKind::Uniform` and `StorageStruct`, the `getpointer` index (the
/// field selected within the block's struct) must also be a compile-time
/// constant: a real cbuffer or direct-field storage block access is
/// statically typed there. For a texel-buffer kind, every load's result
/// type (or store's stored-value type) must be one of the shapes
/// `isSupportedTexelElementType` accepts -- see
/// `classifyTexelBufferHandle`'s comment for why that check belongs here
/// rather than on the handle type; for a storage/uniform kind, it must
/// instead be one `isSupportedRawElementType` accepts, since that is what
/// `createRawLoad`/`createRawStore` (and, transitively,
/// `feme::cpu::mangleResourceCallName`) can mangle a runtime call name for.
/// Any GEP whose byte offset is not recoverable from its indices is still
/// left unmodeled.
bool hasOnlySupportedUses(const CallInst &Handle, HandleKind Kind) {
  bool Writable = Kind == HandleKind::Storage ||
                  Kind == HandleKind::StorageStruct ||
                  Kind == HandleKind::TexelStorage;
  bool IsTexel = isTexelHandleKind(Kind);
  // (Roadmap L16) A struct-typed direct-field member -- e.g. a real
  // `cbuffer`'s own `X x1;` where `X` is itself a user-defined struct
  // (`Feature/CBuffer/structs.test`), reachable once
  // `feme::spirv::convertOffsetStructTypeIgnoringDecorations` (roadmap
  // L13a) legalizes the identified-struct-member shape at the SPIR-V-to-
  // LLVM conversion layer -- needs `getpointer`'s own top-level field
  // selection followed by a further `getelementptr` navigating into that
  // field's own struct body, the identical shape `StorageStruct` (a
  // direct-field *storage* block) already supports; only
  // `HandleKind::Uniform`/`UniformArray`'s always-read-only-ness (`Writable`
  // above) differs, not this shape itself.
  //
  // (Roadmap H128) `HandleKind::UniformArray` needs the identical further
  // `getelementptr` navigation whenever its own declared array is nested
  // (roadmap F12a's `convertUniformArrayContent` only ever widens the
  // *outer* dimension into `getpointer`'s own index -- see
  // `feme::spirv::convertUniformBlockType`'s comment -- any further
  // dimension, e.g. a real `uniform Block { uint data[3][4]; }`, converts
  // to an ordinary nested `!llvm.array` the substituted content type
  // still carries, reachable only through a ordinary GEP after
  // `getpointer`, the same shape `Uniform`'s own struct-member navigation
  // already needs). Omitted here previously -- confirmed via a real
  // `dEQP-VK.ubo.2_level_array`/`3_level_array` reduction that every case
  // in both groups (a nested-array uniform-block member, 1376 cases
  // total) failed with `UnsupportedOps.cpp`'s generic "register-bound
  // resource handle...cannot normalize" diagnostic purely because this
  // condition rejected the resulting GEP outright, even though
  // `lowerRawPointerUses` below already lowers such a GEP chain
  // generically (keyed on the GEP's own resolved offset, not `Kind`) with
  // no `UniformArray`-specific code needed at all.
  bool AllowGEPs = Kind == HandleKind::Storage ||
                   Kind == HandleKind::StorageStruct ||
                   Kind == HandleKind::Uniform ||
                   Kind == HandleKind::UniformArray;
  const DataLayout &DL = Handle.getModule()->getDataLayout();
  for (const User *U : Handle.users()) {
    // (Roadmap H144) A typed buffer's own bare `getdimensions.x` call is
    // not a `getpointer`-mediated access at all -- it addresses no
    // element, just reads the descriptor's own element count -- so it is
    // modeled directly on the handle itself rather than requiring the
    // usual `getpointer` indirection every other access shape goes
    // through. `lowerAccesses` below has the matching special-case branch
    // that lowers it to `createGetDimensionsTyped` without ever computing
    // an `ElementIndex`/`Offset`.
    if (IsTexel) {
      if (const auto *DimsCI = dyn_cast<CallInst>(U);
          DimsCI && isGetDimensions1Intrinsic(*DimsCI))
        continue;
    }
    // (Roadmap H160, extended by L124(a)) A storage buffer's own bare
    // `getarraylength` call is likewise not a `getpointer`-mediated access
    // -- see `isGetArrayLengthIntrinsic`'s comment. Scoped to
    // `HandleKind::Storage` (the one-member runtime-array wrapper
    // `StructuredBuffer`/`ByteAddressBuffer` classify as) and
    // `HandleKind::StorageStruct` (a real, multi-field storage-buffer
    // block whose own last member is the runtime array, e.g. `struct
    // SSBO_1 { vec4 data; uint not_set[]; }`, confirmed via `dEQP-VK.
    // compute.pipeline.basic.read_unbound_ssbo`'s own real shader) --
    // `lowerAccesses` below computes `StorageStruct`'s own element stride
    // and byte-prefix directly from `BH.ElementStruct`'s own last member,
    // rather than relying on a single, whole-handle `Stride` the way
    // `Storage` already does.
    if (Kind == HandleKind::Storage || Kind == HandleKind::StorageStruct) {
      if (const auto *LenCI = dyn_cast<CallInst>(U);
          LenCI && isGetArrayLengthIntrinsic(*LenCI))
        continue;
    }
    const auto *GetPtr = dyn_cast<CallInst>(U);
    if (!GetPtr ||
        getIntrinsicID(GetPtr) != Intrinsic::spv_resource_getpointer) {
      logNormalizationRejection("handle used other than by getpointer",
                                &Handle);
      return false;
    }
    if ((Kind == HandleKind::Uniform || Kind == HandleKind::StorageStruct) &&
        !isa<ConstantInt>(GetPtr->getArgOperand(1))) {
      logNormalizationRejection(
          "getpointer index is not a compile-time constant", GetPtr);
      return false;
    }
    if (!hasOnlySupportedPointerUses(*GetPtr, Writable, IsTexel, AllowGEPs,
                                     DL))
      return false; // Already logged by hasOnlySupportedPointerUses.
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
    if (!Classification) {
      // (Roadmap L108) A `Dim::SubpassData` handle (a GLSL `subpassInput`
      // variable's own `handlefrombinding`) is never one of the kinds
      // this pass classifies -- its `OpImageRead` converts directly to
      // `feme.stage.subpass.load` (`feme::spirv::SubpassLoadPattern`,
      // SPIRVToLLVMPatterns.cpp), never referencing this handle's own
      // result at all, so it is always left with no uses (see
      // `feme::cpu::checkSupportedRaisedOps`'s own comment for this same
      // shape, which already tolerates it there). Rejecting the *whole*
      // function over a handle nothing actually uses -- as opposed to one
      // some other, real resource access genuinely needs but this pass
      // cannot model -- would otherwise leave every *other*, perfectly
      // normalizable handle in the same function un-normalized purely
      // because a subpass input happened to be declared alongside them
      // (found via `dEQP-VK.pipeline.pipeline_library.graphics_library.
      // independent_sets_random.*.vert_frag.*`, whose generated shaders
      // routinely mix a `subpassInput` with several genuinely
      // unrelated, otherwise-supported resources in the same function).
      if (CI->use_empty())
        continue;
      logNormalizationRejection(
          "handle result type is not one of the kinds this pass normalizes",
          CI);
      return std::nullopt;
    }

    switch (Classification->Kind) {
    case HandleKind::SampledImage2D:
      if (!hasOnlySupportedImageUses(
              *CI, Classification->TexelElementType->isIntegerTy(32),
              Classification->Shape)) {
        logNormalizationRejection("unsupported sampled-image-2D use", CI);
        return std::nullopt;
      }
      break;
    case HandleKind::StorageImage2D:
      if (!hasOnlySupportedStorageImageUses(
              *CI, Classification->TexelElementType->isIntegerTy(32),
              Classification->Shape)) {
        logNormalizationRejection("unsupported storage-image-2D use", CI);
        return std::nullopt;
      }
      break;
    case HandleKind::Sampler:
      if (!hasOnlySupportedSamplerUses(*CI)) {
        logNormalizationRejection("unsupported sampler use", CI);
        return std::nullopt;
      }
      break;
    default:
      if (!hasOnlySupportedUses(*CI, Classification->Kind))
        return std::nullopt; // Already logged by hasOnlySupportedUses.
      break;
    }

    auto *SetC = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    auto *BindingC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    if (!SetC || !BindingC) {
      logNormalizationRejection("non-constant (set, binding) operand", CI);
      return std::nullopt; // Non-constant binding: not produced today.
    }

    // The array range size, unlike the array index below, must be a
    // compile-time constant: it is part of the (set, binding) identity
    // itself, exactly like DXIL's `handlefrombinding` range-size operand
    // (see `feme::cpu::BoundResourceNormalizationPass::collectBoundHandles`).
    auto *RangeSizeC = dyn_cast<ConstantInt>(CI->getArgOperand(2));
    if (!RangeSizeC) {
      logNormalizationRejection("non-constant range-size operand", CI);
      return std::nullopt; // Non-constant range size: not produced today.
    }
    uint32_t RangeSize = static_cast<uint32_t>(RangeSizeC->getZExtValue());
    if (RangeSize == 0) {
      logNormalizationRejection("unbounded (range size 0) range", CI);
      return std::nullopt; // Unbounded range: see the header comment.
    }

    RangeKey Key{static_cast<uint32_t>(SetC->getZExtValue()),
                 static_cast<uint32_t>(BindingC->getZExtValue()),
                 getResourceClass(Classification->Kind)};
    Handles.push_back(BoundHandle{
        CI, Key, Classification->Kind, Classification->Stride,
        Classification->ElementStruct, Classification->TexelElementType,
        RangeSize, Classification->Shape});
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

/// Emits a raw load of \p Ty at \p Offset -- one canonical
/// `feme.cpu.resource.load.raw.*` call if \p Ty is itself a leaf
/// scalar/vector `isSupportedRawElementType` shape, or (roadmap L20), if
/// \p Ty is a struct or fixed-size array, one such call per field/element
/// instead, reassembled with `insertvalue` -- mirroring how
/// `CompositeConstructPattern`'s own struct case already reassembles a
/// struct value from its own constituents at the SPIR-V-to-LLVM conversion
/// layer. Recurses for a field/element that is itself an aggregate
/// (`hasOnlySupportedUses`'s own `isSupportedRawElementType` check already
/// guarantees every leaf reached this way is a supported scalar/vector).
///
/// (roadmap H138) A `<3 x i64>`/`<4 x i64>` leaf (24/32 bytes) is handled
/// as its own decomposition tier, split into 2-wide (and, for the odd
/// `<3 x i64>` case, one trailing scalar) chunks reassembled with
/// `insertelement` -- see this tier's own comment below for why.
Value *lowerRawLoad(IRBuilderBase &Builder, const ResourceCallEnv &Env,
                    Value *DescriptorIndex, Value *Offset, Value *Mask,
                    Type *Ty, const DataLayout &DL, const Twine &Name) {
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(StructTy);
    Value *Result = PoisonValue::get(StructTy);
    for (unsigned I = 0, E = StructTy->getNumElements(); I != E; ++I) {
      Value *FieldOffset = Builder.CreateAdd(
          Offset, ConstantInt::get(Offset->getType(), SL->getElementOffset(I)));
      Value *Field = lowerRawLoad(Builder, Env, DescriptorIndex, FieldOffset,
                                  Mask, StructTy->getElementType(I), DL, "");
      Result = Builder.CreateInsertValue(Result, Field, I);
    }
    return Result;
  }
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty)) {
    Type *ElemTy = ArrayTy->getElementType();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
    Value *Result = PoisonValue::get(ArrayTy);
    for (unsigned I = 0, E = ArrayTy->getNumElements(); I != E; ++I) {
      Value *ElemOffset = Builder.CreateAdd(
          Offset, ConstantInt::get(Offset->getType(), I * ElemSize));
      Value *Elem = lowerRawLoad(Builder, Env, DescriptorIndex, ElemOffset,
                                 Mask, ElemTy, DL, "");
      Result = Builder.CreateInsertValue(Result, Elem, I);
    }
    return Result;
  }
  // (roadmap H138) `feme/runtime/CPU/FeMeRuntimeCPU.c` only defines
  // `feme.cpu.resource.load.raw.{i64,v2i64}` (both <=16 bytes, this
  // target's own direct-value-ABI threshold, per roadmap H137's own
  // closing note) -- never `v3i64`/`v4i64` (24/32 bytes), which Clang
  // would compile with an indirect (`sret`-return) calling convention this
  // call-building code's "logical, uncoerced" `FunctionType` does not
  // match, risking a silently wrong (not merely absent) result rather
  // than a safe link failure. Decompose into the already-ABI-safe
  // `v2i64`/scalar-`i64` primitives instead, exactly the same "split an
  // unsupported leaf into supported sub-pieces" shape the struct/array
  // tiers above already use.
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    Type *ElemTy = VecTy->getElementType();
    unsigned NumElts = VecTy->getNumElements();
    if (ElemTy->isIntegerTy(64) && NumElts > 2) {
      uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
      Type *V2Ty = FixedVectorType::get(ElemTy, 2);
      Value *Result = PoisonValue::get(VecTy);
      for (unsigned Base = 0; Base != NumElts;) {
        unsigned ChunkElts = NumElts - Base >= 2 ? 2 : 1;
        Value *ChunkOffset = Builder.CreateAdd(
            Offset, ConstantInt::get(Offset->getType(), Base * ElemSize));
        Value *Chunk =
            lowerRawLoad(Builder, Env, DescriptorIndex, ChunkOffset, Mask,
                         ChunkElts == 2 ? V2Ty : ElemTy, DL, "");
        for (unsigned I = 0; I != ChunkElts; ++I) {
          Value *Elem =
              ChunkElts == 2 ? Builder.CreateExtractElement(Chunk, I) : Chunk;
          Result = Builder.CreateInsertElement(Result, Elem, Base + I);
        }
        Base += ChunkElts;
      }
      return Result;
    }
  }
  return createRawLoad(Builder, Env, DescriptorIndex, Offset, Mask, Ty, Name);
}

/// The store-side mirror of `lowerRawLoad` above: one canonical
/// `feme.cpu.resource.store.raw.*` call per leaf scalar/vector field or
/// element, each split off \p Val with `extractvalue`, rather than one
/// call mangled for the aggregate type itself. (roadmap H138) A
/// `<3 x i64>`/`<4 x i64>` leaf is decomposed the same way `lowerRawLoad`
/// decomposes it, splitting \p Val's own elements out with
/// `extractelement` instead of `insertelement`.
void lowerRawStore(IRBuilderBase &Builder, const ResourceCallEnv &Env,
                   Value *DescriptorIndex, Value *Offset, Value *Val,
                   Value *Mask, const DataLayout &DL) {
  // (Roadmap L124(e)) `Val` is `undef`/`poison` whenever this call is
  // storing one of `layOutStructIfOffsetsMatch`'s own synthetic `[N x i8]`
  // interior/trailing alignment-gap members (`SPIRVToLLVMPatterns.cpp`) --
  // a std140/std430-decorated matrix or struct's own explicit padding,
  // never assigned a real value by any `insertvalue`/composite-construct
  // reaching this store (`CompositeConstructPattern`/`spirv.Constant`
  // materialization only ever fill in a gap-inserted struct's *real*,
  // logical member indices, per `PhysicalIndexOut`'s own mapping -- a
  // synthetic gap index is left exactly as `PoisonValue::get` initialized
  // it). Writing an unspecified byte pattern there is a pure no-op (no
  // SPIR-V-visible load can ever observe interior padding), so skip the
  // store entirely instead of recursing into the leaf and emitting a real
  // `feme.cpu.resource.store.raw.*` call for it -- avoiding both the
  // wasted per-byte call overhead and (roadmap L124's own original
  // finding) a hard dependency on runtime entry points
  // (`feme.cpu.resource.store.raw.i8` in particular) that would otherwise
  // need to exist purely to write throwaway padding. `isa<UndefValue>`
  // alone already covers `PoisonValue` too, since `PoisonValue` is itself
  // a subclass of `UndefValue`.
  if (isa<UndefValue>(Val))
    return;
  // A struct/array field's value is only ever a genuine `ExtractValueInst`
  // here when the aggregate builder chain feeding `Val` is itself not
  // wholly constant-foldable (e.g. any real per-lane runtime data); for a
  // synthetic `[N x i8]` alignment-gap member (see this function's own
  // comment above) that no `insertvalue` in that chain ever targets,
  // `llvm::FindInsertedValue` walks back through the chain to the original
  // (always `poison`) base aggregate's value at this index without needing
  // an later, separate InstCombine run to fold the resulting
  // `extractvalue` first -- letting the `isa<UndefValue>` recursive check
  // below see through an as-yet-unsimplified chain the same way it would
  // see a pre-folded literal `poison` operand.
  auto ExtractField = [&](unsigned Idx) -> Value * {
    if (Value *Found = FindInsertedValue(Val, {Idx}))
      return Found;
    return Builder.CreateExtractValue(Val, Idx);
  };
  if (auto *StructTy = dyn_cast<StructType>(Val->getType())) {
    const StructLayout *SL = DL.getStructLayout(StructTy);
    for (unsigned I = 0, E = StructTy->getNumElements(); I != E; ++I) {
      Value *FieldOffset = Builder.CreateAdd(
          Offset, ConstantInt::get(Offset->getType(), SL->getElementOffset(I)));
      Value *Field = ExtractField(I);
      lowerRawStore(Builder, Env, DescriptorIndex, FieldOffset, Field, Mask,
                    DL);
    }
    return;
  }
  if (auto *ArrayTy = dyn_cast<ArrayType>(Val->getType())) {
    Type *ElemTy = ArrayTy->getElementType();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
    for (unsigned I = 0, E = ArrayTy->getNumElements(); I != E; ++I) {
      Value *ElemOffset = Builder.CreateAdd(
          Offset, ConstantInt::get(Offset->getType(), I * ElemSize));
      Value *Elem = ExtractField(I);
      lowerRawStore(Builder, Env, DescriptorIndex, ElemOffset, Elem, Mask, DL);
    }
    return;
  }
  if (auto *VecTy = dyn_cast<FixedVectorType>(Val->getType())) {
    Type *ElemTy = VecTy->getElementType();
    unsigned NumElts = VecTy->getNumElements();
    if (ElemTy->isIntegerTy(64) && NumElts > 2) {
      uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
      for (unsigned Base = 0; Base != NumElts;) {
        unsigned ChunkElts = NumElts - Base >= 2 ? 2 : 1;
        Value *ChunkOffset = Builder.CreateAdd(
            Offset, ConstantInt::get(Offset->getType(), Base * ElemSize));
        Value *Chunk;
        if (ChunkElts == 2) {
          Chunk = PoisonValue::get(FixedVectorType::get(ElemTy, 2));
          for (unsigned I = 0; I != 2; ++I)
            Chunk = Builder.CreateInsertElement(
                Chunk, Builder.CreateExtractElement(Val, Base + I), I);
        } else {
          Chunk = Builder.CreateExtractElement(Val, Base);
        }
        lowerRawStore(Builder, Env, DescriptorIndex, ChunkOffset, Chunk, Mask,
                      DL);
        Base += ChunkElts;
      }
      return;
    }
  }
  createRawStore(Builder, Env, DescriptorIndex, Offset, Val, Mask);
}

/// Rewrites every raw-buffer load/store/atomic reachable from \p Ptr
/// (either directly or, for a structured storage block, through a GEP
/// chain) using the already-resolved descriptor index and byte offset. An
/// `AtomicRMWInst`/`AtomicCmpXchgInst` (roadmap H8x) is only reachable for
/// `HandleKind::Storage`/`StorageStruct`, mirroring `lowerAccesses`'s own
/// `IsTexel` branch's identical atomic handling (roadmap H8w) for
/// `HandleKind::TexelStorage` -- see `hasOnlySupportedPointerUses`'s own
/// comment for why an ordinary storage-buffer atomic reaches this exact
/// `getpointer`/GEP-chain result too. Dispatching to `createAtomic*Raw`
/// here, rather than only at the top-level call site, means a
/// `StorageStruct` direct-field member's own atomic (reached through a
/// GEP navigating to that field) is handled uniformly with a flat
/// `Storage` buffer's atomic: both already have their cumulative byte
/// \p Offset threaded in by the recursive GEP case below before reaching
/// here.
void lowerRawPointerUses(Value *Ptr, const ResourceCallEnv &Env,
                         Value *DescriptorIndex, Value *Offset,
                         const DataLayout &DL) {
  Value *Mask = ConstantInt::getTrue(Ptr->getContext());
  for (User *U : llvm::make_early_inc_range(Ptr->users())) {
    if (auto *LI = dyn_cast<LoadInst>(U)) {
      IRBuilder<> Builder(LI);
      Value *Loaded = lowerRawLoad(Builder, Env, DescriptorIndex, Offset, Mask,
                                   LI->getType(), DL, LI->getName());
      LI->replaceAllUsesWith(Loaded);
      LI->eraseFromParent();
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(U)) {
      IRBuilder<> Builder(SI);
      lowerRawStore(Builder, Env, DescriptorIndex, Offset,
                    SI->getValueOperand(), Mask, DL);
      SI->eraseFromParent();
      continue;
    }
    if (auto *RMW = dyn_cast<AtomicRMWInst>(U)) {
      IRBuilder<> Builder(RMW);
      Value *Val = RMW->getValOperand();
      CallInst *Old;
      switch (RMW->getOperation()) {
      case AtomicRMWInst::Add:
        Old = createAtomicAddRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                 Mask, RMW->getName());
        break;
      case AtomicRMWInst::Sub:
        Old = createAtomicSubRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                 Mask, RMW->getName());
        break;
      case AtomicRMWInst::And:
        Old = createAtomicAndRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                 Mask, RMW->getName());
        break;
      case AtomicRMWInst::Or:
        Old = createAtomicOrRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                Mask, RMW->getName());
        break;
      case AtomicRMWInst::Xor:
        Old = createAtomicXorRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                 Mask, RMW->getName());
        break;
      case AtomicRMWInst::Max:
        Old = createAtomicSMaxRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                  Mask, RMW->getName());
        break;
      case AtomicRMWInst::Min:
        Old = createAtomicSMinRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                  Mask, RMW->getName());
        break;
      case AtomicRMWInst::UMax:
        Old = createAtomicUMaxRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                  Mask, RMW->getName());
        break;
      case AtomicRMWInst::UMin:
        Old = createAtomicUMinRaw(Builder, Env, DescriptorIndex, Offset, Val,
                                  Mask, RMW->getName());
        break;
      case AtomicRMWInst::Xchg:
        Old = createAtomicExchangeRaw(Builder, Env, DescriptorIndex, Offset,
                                      Val, Mask, RMW->getName());
        break;
      default:
        llvm_unreachable("hasOnlySupportedPointerUses only accepts the RMW "
                         "kinds handled above");
      }
      RMW->replaceAllUsesWith(Old);
      RMW->eraseFromParent();
      continue;
    }
    if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(U)) {
      IRBuilder<> Builder(CmpXchg);
      CallInst *Old = createAtomicCompareExchangeRaw(
          Builder, Env, DescriptorIndex, Offset, CmpXchg->getCompareOperand(),
          CmpXchg->getNewValOperand(), Mask, CmpXchg->getName());
      // `hasOnlySupportedPointerUses` already guaranteed every user of
      // `CmpXchg` is an `extractvalue ..., 0` picking out the old value
      // (SPIR-V's own result) -- replace each with the new call directly,
      // since the call's own result *is* that old value (unlike
      // `llvm.cmpxchg`, no `{i32, i1}` struct to extract from). See
      // `lowerAccesses`'s own identical `IsTexel`-branch handling.
      for (User *EU : llvm::make_early_inc_range(CmpXchg->users())) {
        auto *EV = cast<ExtractValueInst>(EU);
        EV->replaceAllUsesWith(Old);
        EV->eraseFromParent();
      }
      CmpXchg->eraseFromParent();
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
    // (Roadmap H144) A bare `getdimensions.x` call on the handle itself --
    // see `hasOnlySupportedUses`'s matching special-case comment -- reads
    // no element, so it bypasses the `getpointer`-mediated rewriting below
    // entirely: lower it directly to `createGetDimensionsTyped` and move
    // on to the handle's next user.
    if (IsTexel) {
      if (auto *DimsCI = dyn_cast<CallInst>(U);
          DimsCI && isGetDimensions1Intrinsic(*DimsCI)) {
        IRBuilder<> Builder(DimsCI);
        CallInst *Dims = createGetDimensionsTyped(
            Builder, Env, DescriptorIndex, Mask, DimsCI->getName());
        DimsCI->replaceAllUsesWith(Dims);
        DimsCI->eraseFromParent();
        continue;
      }
    }
    // (Roadmap H160, extended by L124(a)) A storage buffer's own bare
    // `getarraylength` call -- see `hasOnlySupportedUses`'s matching
    // special-case comment -- reads no element either: lower it directly
    // to `createGetDimensionsRaw`. `HandleKind::Storage`'s own one-member
    // wrapper has no other fields ahead of its runtime array, so the
    // whole descriptor's byte size divides straight by the handle's
    // already-known element `Stride` with no prefix to subtract first.
    // `HandleKind::StorageStruct`'s own real, multi-field block instead
    // needs both derived fresh here: the runtime array's own element
    // stride (from `BH.ElementStruct`'s own last member -- always an
    // `ArrayType`, the substituted runtime-array shape
    // `convertBufferBlockType` produces) and that same last member's own
    // declared byte offset (from `BH.ElementStruct`'s `StructLayout`),
    // subtracted from the descriptor's whole byte size before dividing
    // by the stride -- computed inside the runtime call itself (not via
    // ordinary IR arithmetic around it), since an unbound descriptor's
    // `SizeInBytes` reads as `0` and naive post-hoc subtraction would
    // wrap around to a huge value instead of staying `0`.
    if (BH.Kind == HandleKind::Storage || BH.Kind == HandleKind::StorageStruct) {
      if (auto *LenCI = dyn_cast<CallInst>(U);
          LenCI && isGetArrayLengthIntrinsic(*LenCI)) {
        IRBuilder<> Builder(LenCI);
        Value *Stride;
        Value *PrefixOffset;
        if (BH.Kind == HandleKind::Storage) {
          Stride = ConstantInt::get(I64Ty, BH.Stride);
          PrefixOffset = ConstantInt::get(I64Ty, 0);
        } else {
          unsigned LastIdx = BH.ElementStruct->getNumElements() - 1;
          auto *ArrTy =
              cast<ArrayType>(BH.ElementStruct->getElementType(LastIdx));
          const StructLayout *SL = DL.getStructLayout(BH.ElementStruct);
          Stride =
              ConstantInt::get(I64Ty, DL.getTypeStoreSize(ArrTy->getElementType()));
          PrefixOffset =
              ConstantInt::get(I64Ty, SL->getElementOffset(LastIdx));
        }
        CallInst *Len =
            createGetDimensionsRaw(Builder, Env, DescriptorIndex, Stride,
                                   PrefixOffset, Mask, LenCI->getName());
        LenCI->replaceAllUsesWith(Len);
        LenCI->eraseFromParent();
        continue;
      }
    }
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
        if (auto *SI = dyn_cast<StoreInst>(PU)) {
          // Only reachable for HandleKind::TexelStorage.
          IRBuilder<> Builder(SI);
          createTypedStore(Builder, Env, DescriptorIndex, ElementIndex,
                           SI->getValueOperand(), Mask);
          SI->eraseFromParent();
          continue;
        }
        // `AtomicRMWInst`/`AtomicCmpXchgInst` (roadmap H8w): only
        // reachable for `HandleKind::TexelStorage`, mirroring
        // `lowerImageAccesses`'s own identical `Plain2D`-storage-image
        // atomic handling (roadmap H8v) -- see
        // `hasOnlySupportedPointerUses`'s own comment for why a texel
        // buffer's atomic reaches this exact `getpointer` result too.
        if (auto *RMW = dyn_cast<AtomicRMWInst>(PU)) {
          IRBuilder<> AtomicBuilder(RMW);
          Value *Val = RMW->getValOperand();
          CallInst *Old;
          switch (RMW->getOperation()) {
          case AtomicRMWInst::Add:
            Old = createAtomicAddTyped(AtomicBuilder, Env, DescriptorIndex,
                                       ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::Sub:
            Old = createAtomicSubTyped(AtomicBuilder, Env, DescriptorIndex,
                                       ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::And:
            Old = createAtomicAndTyped(AtomicBuilder, Env, DescriptorIndex,
                                       ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::Or:
            Old = createAtomicOrTyped(AtomicBuilder, Env, DescriptorIndex,
                                      ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::Xor:
            Old = createAtomicXorTyped(AtomicBuilder, Env, DescriptorIndex,
                                       ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::Max:
            Old =
                createAtomicSMaxTyped(AtomicBuilder, Env, DescriptorIndex,
                                      ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::Min:
            Old =
                createAtomicSMinTyped(AtomicBuilder, Env, DescriptorIndex,
                                      ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::UMax:
            Old =
                createAtomicUMaxTyped(AtomicBuilder, Env, DescriptorIndex,
                                      ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::UMin:
            Old =
                createAtomicUMinTyped(AtomicBuilder, Env, DescriptorIndex,
                                      ElementIndex, Val, Mask, RMW->getName());
            break;
          case AtomicRMWInst::Xchg:
            Old = createAtomicExchangeTyped(AtomicBuilder, Env, DescriptorIndex,
                                            ElementIndex, Val, Mask,
                                            RMW->getName());
            break;
          default:
            llvm_unreachable(
                "hasOnlySupportedPointerUses only accepts the RMW kinds "
                "handled above");
          }
          RMW->replaceAllUsesWith(Old);
          RMW->eraseFromParent();
          continue;
        }
        auto *CmpXchg = cast<AtomicCmpXchgInst>(PU);
        IRBuilder<> AtomicBuilder(CmpXchg);
        CallInst *Old = createAtomicCompareExchangeTyped(
            AtomicBuilder, Env, DescriptorIndex, ElementIndex,
            CmpXchg->getCompareOperand(), CmpXchg->getNewValOperand(), Mask,
            CmpXchg->getName());
        // `hasOnlySupportedPointerUses` already guaranteed every user of
        // `CmpXchg` is an `extractvalue ..., 0` picking out the old value
        // (SPIR-V's own result) -- replace each with the new call
        // directly, since the call's own result *is* that old value
        // (unlike `llvm.cmpxchg`, no `{i32, i1}` struct to extract from).
        for (User *EU : llvm::make_early_inc_range(CmpXchg->users())) {
          auto *EV = cast<ExtractValueInst>(EU);
          EV->replaceAllUsesWith(Old);
          EV->eraseFromParent();
        }
        CmpXchg->eraseFromParent();
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
/// canonical `feme.cpu.image.*` call (see ImageCalls.h), then erases each
/// handle that ends up with no remaining users.
///
/// `hasOnlySupportedImageUses`/`hasOnlySupportedSamplerUses` already
/// guaranteed at collection time that every use *this* handle has is one of
/// these shapes, so there is no partially-rewritten state to worry about
/// *for a single handle*: either all of it was accepted, or none of it was.
/// Roadmap L66(e): that guarantee does not extend to a sample/fetch call's
/// *other* handle (its image side's own sampler, or vice versa) -- \p
/// HeapIndices only contains handles from `run`'s own per-handle
/// `Entry.Conflicting` skip, so a sample call's image handle can be present
/// here while its paired sampler handle was excluded (its own, unrelated
/// (set, binding) identity conflicting with a different declaration
/// elsewhere in the module), or vice versa; such a call is deliberately left
/// entirely unrewritten below rather than only partially so (see each
/// shape's own `CI->getArgOperand(0) != Handle` skip), which means the
/// handle that *is* present here is not always fully dead by the time this
/// function returns.
void lowerImageAccesses(
    const MapVector<CallInst *, ImageHeapEntry> &HeapIndices,
    const ImageCallEnv &Env) {
  LLVMContext &Ctx = Env.ImageHeap->getContext();
  Value *Mask = ConstantInt::getTrue(Ctx);

  for (const auto &[Handle, Entry] : HeapIndices) {
    Value *ImageIndex = Entry.Index;
    ImageShape Shape = Entry.Shape;
    for (User *U : llvm::make_early_inc_range(Handle->users())) {
      auto *CI = cast<CallInst>(U);
      bool ExplicitLod = false;
      bool HasMinLodClamp = false;
      bool HasBias = false;
      bool HasGrad = false;
      if (isSampleIntrinsic(*CI, ExplicitLod, HasMinLodClamp, HasBias,
                            HasGrad)) {
        // A sample is reached twice -- once from its image handle, once
        // from its sampler handle -- so only rewrite it from the image
        // side, where both descriptor indices are already resolvable.
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(2);
        Value *Lod = ExplicitLod ? CI->getArgOperand(3)
                                 : ConstantFP::get(Builder.getFloatTy(), 0.0);
        // Roadmap L58: SPIR-V's own `Bias` image operand shares the same
        // fixed operand index 3 `Lod` occupies for an explicit-LOD
        // sample -- `ExplicitLod` and `HasBias` are mutually exclusive
        // (SPIR-V forbids `Bias` alongside `Lod`), so there is no
        // ambiguity about which one index 3 holds for a given call.
        Value *Bias = HasBias ? CI->getArgOperand(3)
                              : ConstantFP::get(Builder.getFloatTy(), 0.0);
        // Roadmap L59: `Grad`'s own `dPdx`/`dPdy` operands (indices 3, 4)
        // are the caller's own real screen-space partial-derivative
        // vectors of `Coord` -- unpacked into scalar components below,
        // per-shape, alongside `C0`/`C1`/`C2`, and fed directly into
        // `createSample2D`/`createSampleCube` in place of
        // `getOrSynthesizeSample2DDerivatives`/
        // `getOrSynthesizeSampleCubeDerivatives`'s own synthesized or
        // zeroed values -- the runtime's own implicit-LOD mip/anisotropy
        // math (`femeRTPlanImplicitLod`) is agnostic to whether a
        // derivative was synthesized from `feme.stage.derivative.*` or
        // supplied explicitly by the shader itself via `Grad`.
        Value *GradDPdx = HasGrad ? CI->getArgOperand(3) : nullptr;
        Value *GradDPdy = HasGrad ? CI->getArgOperand(4) : nullptr;
        Value *ExplicitLodFlag = Builder.getInt1(ExplicitLod);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1))).Index;
        // Roadmap H109/L125(b): an integer-channel sample is only ever
        // accepted by `hasOnlySupportedImageUses` as a `Plain2D`/`Plain1D`,
        // no-`Bias`/`Grad`/`MinLod` call (see its own comment) -- so this
        // narrower emission runs before, and instead of, the general
        // float-sample shape dispatch below, which would otherwise need
        // an `IsInteger` branch threaded through every shape's own case.
        if (isV4I32(CI->getType())) {
          // Roadmap L125(b): `Plain1D`'s own coordinate/offset are bare
          // scalars (see `isCoordN`'s/`isSupportedOffset`'s own comments
          // on why SPIR-V never vector-wraps a single-component
          // coordinate/offset), unlike `Plain2D`'s 2-wide
          // `CreateExtractElement` pair below -- so this is handled as
          // its own case rather than falling through to the generic
          // `Plain2D` extraction.
          if (Shape == ImageShape::Plain1D) {
            Value *IntOffset = CI->getArgOperand(
                getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad));
            CallInst *NewSampleI32Call =
                createSample1DI32(Builder, Env, ImageIndex, SamplerIndex,
                                  Coord, Lod, IntOffset, Mask, CI->getName());
            CI->replaceAllUsesWith(NewSampleI32Call);
            CI->eraseFromParent();
            continue;
          }
          Value *IntC0 = Builder.CreateExtractElement(Coord, uint64_t{0});
          Value *IntC1 = Builder.CreateExtractElement(Coord, uint64_t{1});
          Value *IntOffset = CI->getArgOperand(
              getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad));
          Value *IntOffsetX =
              Builder.CreateExtractElement(IntOffset, uint64_t{0});
          Value *IntOffsetY =
              Builder.CreateExtractElement(IntOffset, uint64_t{1});
          CallInst *NewSampleI32Call = createSample2DI32(
              Builder, Env, ImageIndex, SamplerIndex, IntC0, IntC1, Lod,
              IntOffsetX, IntOffsetY, Mask, CI->getName());
          CI->replaceAllUsesWith(NewSampleI32Call);
          CI->eraseFromParent();
          continue;
        }
        // Roadmap L52a: `Plain1D`/`Array1D` are handled separately, before
        // the generic `C0`/`C1` extraction below, since `Plain1D`'s own
        // coordinate is a bare scalar float (see `isCoordN`'s own comment
        // on why SPIR-V never vector-wraps a single-component coordinate),
        // not a vector `CreateExtractElement` could be applied to.
        if (Shape == ImageShape::Plain1D || Shape == ImageShape::Array1D) {
          // Roadmap L61(c): same `MinLod` clamp extraction as `Plain2D`'s
          // own below -- negative infinity (a no-op floor) for every
          // sample intrinsic other than `spv_resource_sample_clamp`/
          // `samplebias_clamp`, neither of which `Plain1D`/`Array1D`
          // could reach this branch through without `HasMinLodClamp`
          // already being true (see `hasOnlySupportedImageUses`'s own
          // updated restriction just above).
          Value *MinLodClamp =
              HasMinLodClamp ? CI->getArgOperand(getSampleClampIdx(
                                   ExplicitLod, HasBias, HasGrad))
                             : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                       /*Negative=*/true);
          // Roadmap L63/L65: an implicit-LOD `Plain1D`/`Array1D` sample's
          // real mip level needs this sample's own screen-space
          // derivative of its single addressed coordinate component,
          // synthesized only in the fragment stage, mirroring `Plain2D`'s
          // own `getOrSynthesizeSample2DDerivatives` handling above -- an
          // explicit-LOD `textureLod()` ignores them, so zero constants
          // (no extra IR) are passed instead. Roadmap L65: a `Grad`
          // sample instead supplies its own real derivative directly,
          // mirroring `Plain2D`'s own `HasGrad` handling -- `GradDPdx`/
          // `GradDPdy` are already bare scalar floats here (roadmap L64's
          // `GradDerivativeWidth` computation gives both `Plain1D` and
          // the arrayed `Array1D` a 1-wide derivative, since neither
          // shape's own single addressed coordinate component is a
          // vector `hasOnlySupportedImageUses`'s `isCoordN` would need to
          // unpack), so no `CreateExtractElement` is needed the way
          // `Plain2D`'s 2-wide derivative vectors require. Computed
          // separately per shape below (not once, unconditionally, up
          // here) since `Coord` itself is `Array1D`'s own 2-component
          // `(U, ArrayLayer)` vector, not a bare scalar -- differentiating
          // it directly here (as this code used to) synthesized a stray,
          // unused, mistyped derivative of the whole vector for every
          // `Array1D` sample, alongside the real scalar-`U` one `ArrayD`
          // below already computes correctly.
          // Roadmap L66(d): a real, nonzero `ConstOffset` against
          // `Plain1D`/`Array1D` is a bare scalar `i32`, unlike every
          // other supported shape's own vector-typed offset -- no
          // `CreateExtractElement` is needed the way `Plain2D`'s/
          // `Array2D`'s/`Plain3D`'s own vector offsets require (see
          // `isSupportedOffset`'s own updated comment for why).
          Value *Offset = CI->getArgOperand(
              getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad));
          CallInst *NewSample1DCall;
          if (Shape == ImageShape::Plain1D) {
            SampleDerivatives1D D =
                HasGrad ? SampleDerivatives1D{GradDPdx, GradDPdy}
                : !ExplicitLod
                    ? getOrSynthesizeSample1DDerivatives(
                          Builder, *CI->getFunction(), Coord)
                    : SampleDerivatives1D{
                          ConstantFP::get(Builder.getFloatTy(), 0.0),
                          ConstantFP::get(Builder.getFloatTy(), 0.0)};
            NewSample1DCall =
                createSample1D(Builder, Env, ImageIndex, SamplerIndex, Coord,
                               D.DUdX, D.DUdY, Lod, ExplicitLodFlag, Bias,
                               Offset, MinLodClamp, Mask, CI->getName());
          } else {
            Value *U = Builder.CreateExtractElement(Coord, uint64_t{0});
            Value *ArrayLayer =
                Builder.CreateExtractElement(Coord, uint64_t{1});
            SampleDerivatives1D ArrayD =
                HasGrad ? SampleDerivatives1D{GradDPdx, GradDPdy}
                : !ExplicitLod
                    ? getOrSynthesizeSample1DDerivatives(Builder,
                                                         *CI->getFunction(), U)
                    : SampleDerivatives1D{
                          ConstantFP::get(Builder.getFloatTy(), 0.0),
                          ConstantFP::get(Builder.getFloatTy(), 0.0)};
            NewSample1DCall = createSample1DArray(
                Builder, Env, ImageIndex, SamplerIndex, U, ArrayLayer,
                ArrayD.DUdX, ArrayD.DUdY, Lod, ExplicitLodFlag, Bias, Offset,
                MinLodClamp, Mask, CI->getName());
          }
          CI->replaceAllUsesWith(NewSample1DCall);
          CI->eraseFromParent();
          continue;
        }
        // Roadmap L66(a)/L67(a)/L67(b)/L67(c): `Plain3D` is handled
        // separately too, alongside `Plain1D`/`Array1D` above: a real
        // 3-component `(U, V, W)` coordinate, its own per-axis
        // derivatives -- either the caller's own real `Grad` derivative
        // triple (roadmap L67(b), extracted one component per axis from
        // `GradDPdx`/`GradDPdy`, which `hasOnlySupportedImageUses`'s own
        // `GradDerivativeWidth` check already guarantees is a real
        // 3-wide vector for this non-arrayed shape) or, absent `Grad`,
        // per-axis synthesized screen-space derivatives (reusing
        // `getOrSynthesizeSample1DDerivatives` three times, once per
        // axis -- there is no dedicated 3D derivative synthesis helper,
        // since each axis differentiates independently the same way
        // `Plain1D`'s own single axis does) -- a real `Bias`/`MinLodClamp`
        // pair (roadmap L67(a), mirroring `Plain1D`'s own extraction
        // immediately above), a real `ConstOffset` triple (roadmap
        // L67(c), split into its own X/Y/Z components the same way
        // `Plain2D`'s own `OffsetX`/`OffsetY` are, see
        // `isSupportedOffset`'s own updated doc for why a 3-wide vector
        // is now required here rather than the 2-wide one `Plain2D`/
        // `Array2D` share), and a direct `createSample3D` call.
        if (Shape == ImageShape::Plain3D) {
          Value *U = Builder.CreateExtractElement(Coord, uint64_t{0});
          Value *V = Builder.CreateExtractElement(Coord, uint64_t{1});
          Value *W = Builder.CreateExtractElement(Coord, uint64_t{2});
          Value *ZeroF = ConstantFP::get(Builder.getFloatTy(), 0.0);
          SampleDerivatives1D UD, VD, WD;
          if (HasGrad) {
            UD = {Builder.CreateExtractElement(GradDPdx, uint64_t{0}),
                  Builder.CreateExtractElement(GradDPdy, uint64_t{0})};
            VD = {Builder.CreateExtractElement(GradDPdx, uint64_t{1}),
                  Builder.CreateExtractElement(GradDPdy, uint64_t{1})};
            WD = {Builder.CreateExtractElement(GradDPdx, uint64_t{2}),
                  Builder.CreateExtractElement(GradDPdy, uint64_t{2})};
          } else if (!ExplicitLod) {
            UD = getOrSynthesizeSample1DDerivatives(Builder, *CI->getFunction(),
                                                    U);
            VD = getOrSynthesizeSample1DDerivatives(Builder, *CI->getFunction(),
                                                    V);
            WD = getOrSynthesizeSample1DDerivatives(Builder, *CI->getFunction(),
                                                    W);
          } else {
            UD = VD = WD = SampleDerivatives1D{ZeroF, ZeroF};
          }
          Value *Offset = CI->getArgOperand(
              getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad));
          Value *OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
          Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
          Value *OffsetZ = Builder.CreateExtractElement(Offset, uint64_t{2});
          Value *MinLodClamp =
              HasMinLodClamp ? CI->getArgOperand(getSampleClampIdx(
                                   ExplicitLod, HasBias, HasGrad))
                             : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                       /*Negative=*/true);
          CallInst *NewSample3DCall = createSample3D(
              Builder, Env, ImageIndex, SamplerIndex, U, V, W, UD.DUdX, UD.DUdY,
              VD.DUdX, VD.DUdY, WD.DUdX, WD.DUdY, Lod, ExplicitLodFlag, Bias,
              OffsetX, OffsetY, OffsetZ, MinLodClamp, Mask, CI->getName());
          CI->replaceAllUsesWith(NewSample3DCall);
          CI->eraseFromParent();
          continue;
        }
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
          // are passed instead. Roadmap L59: a `Grad` sample instead
          // supplies its own real (dPdx, dPdy) vectors directly -- no
          // synthesis needed (or possible: `Grad` sampling is legal from
          // any shader stage, unlike GLSL's stage-restricted implicit
          // `texture()`), so its two components are unpacked the same way
          // `Coord`'s own `C0`/`C1` are.
          SampleDerivatives D =
              HasGrad
                  ? SampleDerivatives{Builder.CreateExtractElement(GradDPdx,
                                                                   uint64_t{0}),
                                      Builder.CreateExtractElement(GradDPdy,
                                                                   uint64_t{0}),
                                      Builder.CreateExtractElement(GradDPdx,
                                                                   uint64_t{1}),
                                      Builder.CreateExtractElement(GradDPdy,
                                                                   uint64_t{1})}
              : !ExplicitLod ? getOrSynthesizeSample2DDerivatives(
                                   Builder, *CI->getFunction(), C0, C1)
                             : SampleDerivatives{
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0)};
          // Roadmap L26: SPIR-V's own `ConstOffset` image operand -- a
          // compile-time-constant `<2 x i32>` `hasOnlySupportedImageUses`
          // already validated via `isSupportedOffset` -- is a real,
          // possibly-nonzero texel offset now, not always the trivial
          // zero case; split its two components the same way `Coord`'s
          // own `C0`/`C1` are.
          Value *Offset = CI->getArgOperand(
              getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad));
          Value *OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
          Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
          // Roadmap L26: SPIR-V's own `MinLod` image operand
          // (`spv_resource_sample_clamp`'s trailing `clamp` operand,
          // `hasOnlySupportedImageUses` already restricted to `Plain2D`)
          // -- negative infinity (a no-op floor) for every other sample
          // intrinsic, which has no such operand of its own.
          Value *MinLodClamp =
              HasMinLodClamp ? CI->getArgOperand(getSampleClampIdx(
                                   ExplicitLod, HasBias, HasGrad))
                             : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                       /*Negative=*/true);
          NewCall = createSample2D(Builder, Env, ImageIndex, SamplerIndex, C0,
                                   C1, D.DUdX, D.DUdY, D.DVdX, D.DVdY, Lod,
                                   ExplicitLodFlag, Bias, OffsetX, OffsetY,
                                   MinLodClamp, Mask, CI->getName());
          break;
        }
        case ImageShape::Array2D: {
          Value *ArrayLayer = Builder.CreateExtractElement(Coord, uint64_t{2});
          // Roadmap L60(a): same derivative/Bias/MinLodClamp handling as
          // Plain2D above, reusing the same `getOrSynthesizeSample2D
          // Derivatives` helper (an arrayed sample's own (C0, C1)
          // face-local coordinate is differentiated identically to a
          // non-arrayed one -- the array layer itself is never
          // differentiated, mirroring CubeArray's own layer-agnostic
          // derivative handling). Roadmap L33: same `ConstOffset`
          // extraction as `Plain2D`'s own below now too (an ordinary
          // Array2D sample's own offset lowering is no longer future
          // work).
          SampleDerivatives D =
              HasGrad
                  ? SampleDerivatives{Builder.CreateExtractElement(GradDPdx,
                                                                   uint64_t{0}),
                                      Builder.CreateExtractElement(GradDPdy,
                                                                   uint64_t{0}),
                                      Builder.CreateExtractElement(GradDPdx,
                                                                   uint64_t{1}),
                                      Builder.CreateExtractElement(GradDPdy,
                                                                   uint64_t{1})}
              : !ExplicitLod ? getOrSynthesizeSample2DDerivatives(
                                   Builder, *CI->getFunction(), C0, C1)
                             : SampleDerivatives{
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0)};
          Value *Offset = CI->getArgOperand(
              getSampleOffsetIdx(ExplicitLod, HasBias, HasGrad));
          Value *OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
          Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
          Value *MinLodClamp =
              HasMinLodClamp ? CI->getArgOperand(getSampleClampIdx(
                                   ExplicitLod, HasBias, HasGrad))
                             : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                       /*Negative=*/true);
          NewCall = createSample2DArray(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, ArrayLayer,
              D.DUdX, D.DUdY, D.DVdX, D.DVdY, Lod, ExplicitLodFlag, Bias,
              OffsetX, OffsetY, MinLodClamp, Mask, CI->getName());
          break;
        }
        case ImageShape::Cube: {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          // Roadmap L26: unlike `Plain2D`, `Cube` never has a real offset
          // operand to extract (SPIR-V forbids `ConstOffset` against
          // `Dim::Cube`, see `isSupportedOffset`'s own comment) -- but it
          // can still carry a `MinLod` clamp, since `Dim` has no bearing
          // on that operand's own legality.
          Value *MinLodClamp =
              HasMinLodClamp ? CI->getArgOperand(getSampleClampIdx(
                                   ExplicitLod, HasBias, HasGrad))
                             : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                       /*Negative=*/true);
          // Roadmap L56: an implicit-LOD cube sample's real mip level
          // needs the caller's own screen-space derivatives of the raw
          // direction vector (C0, C1, C2) -- unlike Plain2D, the
          // face-local (U, V) coordinate these would otherwise describe
          // isn't known until `femeRTSelectCubeFace` picks a face at
          // runtime, so the direction vector itself (not a face-local
          // coordinate) is what gets differentiated here; see
          // `getOrSynthesizeSampleCubeDerivatives`'s doc. An explicit-LOD
          // `textureLod()` ignores them, so zero constants are passed
          // instead, mirroring Plain2D's own `ExplicitLod` gating above.
          // Roadmap L59: a `Grad` sample instead supplies its own real
          // 3-component direction-derivative vectors directly, mirroring
          // Plain2D's own `HasGrad` handling above.
          CubeDirectionDerivatives CD =
              HasGrad ? CubeDirectionDerivatives{Builder.CreateExtractElement(
                                                     GradDPdx, uint64_t{0}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdy, uint64_t{0}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdx, uint64_t{1}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdy, uint64_t{1}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdx, uint64_t{2}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdy, uint64_t{2})}
              : !ExplicitLod ? getOrSynthesizeSampleCubeDerivatives(
                                   Builder, *CI->getFunction(), C0, C1, C2)
                             : CubeDirectionDerivatives{
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0)};
          NewCall = createSampleCube(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, C2, CD.DDirXdX,
              CD.DDirXdY, CD.DDirYdX, CD.DDirYdY, CD.DDirZdX, CD.DDirZdY, Lod,
              ExplicitLodFlag, Bias, MinLodClamp, Mask, CI->getName());
          break;
        }
        case ImageShape::CubeArray: {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          Value *ArrayLayer = Builder.CreateExtractElement(Coord, uint64_t{3});
          // Roadmap L60(a): same `MinLod` clamp extraction as Cube above
          // -- `CubeArray` can carry one too, mirroring Cube's own
          // rationale (SPIR-V's `MinLod` image operand has no
          // dimensionality restriction).
          Value *MinLodClamp =
              HasMinLodClamp ? CI->getArgOperand(getSampleClampIdx(
                                   ExplicitLod, HasBias, HasGrad))
                             : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                       /*Negative=*/true);
          // Roadmap L56: same derivative synthesis as Cube above.
          // Roadmap L60(a): a `Grad` sample instead supplies its own real
          // 3-component direction-derivative vectors directly, mirroring
          // Cube's own `HasGrad` handling above.
          CubeDirectionDerivatives CD =
              HasGrad ? CubeDirectionDerivatives{Builder.CreateExtractElement(
                                                     GradDPdx, uint64_t{0}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdy, uint64_t{0}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdx, uint64_t{1}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdy, uint64_t{1}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdx, uint64_t{2}),
                                                 Builder.CreateExtractElement(
                                                     GradDPdy, uint64_t{2})}
              : !ExplicitLod ? getOrSynthesizeSampleCubeDerivatives(
                                   Builder, *CI->getFunction(), C0, C1, C2)
                             : CubeDirectionDerivatives{
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0),
                                   ConstantFP::get(Builder.getFloatTy(), 0.0)};
          NewCall = createSampleCubeArray(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, C2, CD.DDirXdX,
              CD.DDirXdY, CD.DDirYdX, CD.DDirYdY, CD.DDirZdX, CD.DDirZdY,
              ArrayLayer, Lod, ExplicitLodFlag, Bias, MinLodClamp, Mask,
              CI->getName());
          break;
        }
        case ImageShape::Plain1D:
        case ImageShape::Array1D:
          // Unreachable: handled by the early `continue` above, before
          // this switch is ever entered.
          llvm_unreachable("Plain1D/Array1D handled before this switch");
        case ImageShape::Plain3D:
          // Unreachable: roadmap L66(a) added its own early `continue`
          // above too, mirroring `Plain1D`/`Array1D`'s identical
          // precedent -- `classifySampledImage2DHandle` does now produce
          // this shape for a real, real-CTS-reachable sampled 3D volume
          // texture, unlike the two genuinely-unreachable shapes below.
          llvm_unreachable("Plain3D handled before this switch");
        case ImageShape::Plain2DMS:
        case ImageShape::Array2DMS:
          // Unreachable here even though `classifySampledImage2DHandle`
          // does now produce these two shapes for a multisampled sampled
          // image handle (roadmap L73): `hasOnlySupportedImageUses`'s own
          // `isSampleIntrinsic`/`isDrefSampleIntrinsic` branches explicitly
          // reject an ordinary/depth-comparison filtered sample against
          // either shape (no `runtime/CPU` sampling helper exists for a
          // multisampled sampled image), so a call reaching this ordinary
          // sample-dispatch switch can never actually carry this shape --
          // `OpImageQuerySamples`, the sole operation this shape supports,
          // is dispatched separately by `isQuerySamplesCall` below, not
          // through this switch at all.
          llvm_unreachable(
              "no sampled-image shape produces Plain2DMS/Array2DMS");
        }
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L48: a depth-comparison sample
      // (`spv_resource_samplecmp`/`samplecmplevelzero`/`samplecmp_clamp`),
      // extended from `Plain2D`-only (roadmap L46) to also cover
      // `Array2D`/`Cube`/`CubeArray`, mirroring the ordinary-sample
      // `switch (Shape)` just above. Every non-`Grad`, non-`samplecmplevel`
      // shape shares `createSampleCmp2D`'s own `Lod` parameter passed as
      // a constant zero: `samplecmplevelzero` always forces mip level 0
      // (no LOD operand of its own to read), and
      // `samplecmp`/`samplecmp_clamp`'s implicit LOD already degenerates
      // to the same level 0 today -- only `UseExplicitLod` itself differs
      // between them. Roadmap L66(c) adds a real, derivative-driven
      // implicit LOD for `Plain2D` alone, via `samplecmpgrad{,_clamp}`'s
      // own `dPdx`/`dPdy` pair (`femeCpuImageSampleCmp2DF32` itself now
      // branches on whether `UseExplicitLod` is set the same way it
      // always has, but computes a real implicit LOD from these
      // derivatives when it is not, in place of the always-level-0 result
      // every other intrinsic form still gets). Roadmap L72(b) adds
      // `samplecmplevel`'s own real (non-zero) `Lod` operand, read from
      // `DrefSampleLevelIdx` instead of synthesizing a constant zero.
      bool DrefExplicitLod = false;
      bool DrefHasClamp = false;
      bool DrefHasBias = false;
      bool DrefHasGrad = false;
      bool DrefHasLevel = false;
      if (isDrefSampleIntrinsic(*CI, DrefExplicitLod, DrefHasClamp, DrefHasBias,
                                DrefHasGrad, DrefHasLevel)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(2);
        Value *Dref = CI->getArgOperand(DrefSampleDrefIdx);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1))).Index;
        Value *C0 = Builder.CreateExtractElement(Coord, uint64_t{0});
        Value *C1 = Builder.CreateExtractElement(Coord, uint64_t{1});
        Value *Lod = DrefHasLevel ? CI->getArgOperand(DrefSampleLevelIdx)
                                  : ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *ExplicitLodFlag = Builder.getInt1(DrefExplicitLod);
        // Roadmap L52(c): SPIR-V's own `MinLod` image operand
        // (`spv_resource_samplecmp_clamp`'s trailing `clamp` operand,
        // `hasOnlySupportedImageUses` already restricted to
        // `Plain2D`/`Array2D`/`Cube`/`CubeArray`) -- negative infinity (a
        // no-op floor) for `samplecmp`/`samplecmplevelzero`, which have no
        // such operand of their own, mirroring the ordinary-sample
        // `MinLodClamp` fallback above.
        Value *MinLodClamp = DrefHasClamp
                                 ? CI->getArgOperand(getDrefSampleClampIdx(
                                       DrefHasBias, DrefHasGrad, DrefHasLevel))
                                 : ConstantFP::getInfinity(Builder.getFloatTy(),
                                                           /*Negative=*/true);
        // Roadmap L52(b): SPIR-V's own `Bias` image operand
        // (`spv_resource_samplecmpbias{,_clamp}`'s bias operand, which
        // `hasOnlySupportedImageUses` already restricted to the same four
        // shapes as `MinLodClamp` above) -- a zero constant (a no-op LOD
        // shift) for the non-bias forms, which have no such operand of
        // their own.
        Value *Bias = DrefHasBias ? CI->getArgOperand(DrefSampleBiasIdx)
                                  : ConstantFP::get(Builder.getFloatTy(), 0.0);
        // Roadmap L66(c)/L66(g): SPIR-V's own `Grad` image operand pair
        // (`spv_resource_samplecmpgrad{,_clamp}`'s `dPdx`/`dPdy`, which
        // `hasOnlySupportedImageUses` now restricts to `Plain2D`/
        // `Plain1D`/`Array1D`/`Array2D`/`Cube`) -- unpacked into four
        // scalars the same way `Coord`'s own `C0`/`C1` are for
        // `Plain2D`/`Array2D` (both have a genuine 2-wide derivative,
        // `Array2D`'s own array layer having none of its own), and
        // threaded through only for those two arms below. Zero constants
        // for the non-`Grad` forms, which have no such operand of their
        // own -- `femeCpuImageSampleCmp2DF32`/`femeCpuImageSampleCmpArray2DF32`
        // provably degenerate to the exact same level-0 result their own
        // narrower pre-L66(c)/pre-L66(g) implementations always computed
        // for all-zero derivatives.
        Value *DUdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *DUdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *DVdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *DVdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
        // Roadmap L66(f): `Plain1D`/`Array1D`'s own `dPdx`/`dPdy` are
        // already bare scalar floats (`hasOnlySupportedImageUses`'s own
        // narrowed `GradDerivativeWidth`-style check gives both shapes a
        // 1-wide derivative), not 2-wide vectors `CreateExtractElement`
        // could apply to the way `Plain2D`'s own pair requires -- read
        // directly into `Grad1DDUdX`/`Grad1DDUdY` instead, consumed only
        // by their own switch arms below.
        Value *Grad1DDUdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *Grad1DDUdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
        // Roadmap L66(h)/L66(i): `Cube`'s/`CubeArray`'s own `dPdx`/`dPdy`
        // are a genuine 3-wide direction-vector derivative
        // (`hasOnlySupportedImageUses`'s own `GradDerivativeWidth`-style
        // check gives both a width of 3 -- `Cube` is unarrayed with a
        // `SampleCoordWidth` of 3, while `CubeArray`'s own arrayed
        // `SampleCoordWidth` of 4 drops one component for its array
        // layer, resolving to the same width), unpacked into six scalars
        // the same way `createSampleCube`'s own `HasGrad` handling above
        // unpacks an ordinary sample's `Grad` operand -- read into their
        // own `CubeDDir*` variables, consumed by both the `Cube` and
        // `CubeArray` switch arms below.
        Value *CubeDDirXdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *CubeDDirXdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *CubeDDirYdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *CubeDDirYdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *CubeDDirZdX = ConstantFP::get(Builder.getFloatTy(), 0.0);
        Value *CubeDDirZdY = ConstantFP::get(Builder.getFloatTy(), 0.0);
        if (DrefHasGrad) {
          Value *GradDPdx = CI->getArgOperand(DrefSampleGradDPdxIdx);
          Value *GradDPdy = CI->getArgOperand(DrefSampleGradDPdyIdx);
          if (Shape == ImageShape::Plain2D || Shape == ImageShape::Array2D) {
            DUdX = Builder.CreateExtractElement(GradDPdx, uint64_t{0});
            DUdY = Builder.CreateExtractElement(GradDPdy, uint64_t{0});
            DVdX = Builder.CreateExtractElement(GradDPdx, uint64_t{1});
            DVdY = Builder.CreateExtractElement(GradDPdy, uint64_t{1});
          } else if (Shape == ImageShape::Cube ||
                     Shape == ImageShape::CubeArray) {
            CubeDDirXdX = Builder.CreateExtractElement(GradDPdx, uint64_t{0});
            CubeDDirXdY = Builder.CreateExtractElement(GradDPdy, uint64_t{0});
            CubeDDirYdX = Builder.CreateExtractElement(GradDPdx, uint64_t{1});
            CubeDDirYdY = Builder.CreateExtractElement(GradDPdy, uint64_t{1});
            CubeDDirZdX = Builder.CreateExtractElement(GradDPdx, uint64_t{2});
            CubeDDirZdY = Builder.CreateExtractElement(GradDPdy, uint64_t{2});
          } else {
            Grad1DDUdX = GradDPdx;
            Grad1DDUdY = GradDPdy;
          }
        }
        // Roadmap L50d: SPIR-V's own `ConstOffset` image operand --
        // `hasOnlySupportedImageUses` already validated it via
        // `isSupportedOffset`'s own `AllowArray2D` case, a real,
        // possibly-nonzero one for `Plain2D`/`Array2D` alike -- split
        // into its two components the same way `Coord`'s own `C0`/`C1`
        // are. `Cube`/`CubeArray` never have a real one to extract
        // (SPIR-V forbids `ConstOffset` against `Dim::Cube`, see
        // `isSupportedOffset`'s own comment), so their own `switch` arms
        // below still pass zero constants directly instead. Roadmap
        // L66(k): `Plain1D`/`Array1D` now also accept a real, nonzero
        // `ConstOffset` (`AllowPlain1DArray1D`, see `isSupportedOffset`'s
        // own comment) -- but a real deqp-vk SPIR-V capture confirms this
        // one is a bare scalar `i32`, unlike `Plain2D`/`Array2D`'s own
        // dref-coordinate-shaped vector; only the *synthesized* always-
        // zero fallback for these two shapes still mirrors `Coord`'s own
        // `DrefCoordWidth`-wide vector shape (a deliberate asymmetry from
        // roadmap L66(j), needed to keep this same `Offset` value legal
        // to unconditionally `CreateExtractElement` from for `Plain2D`/
        // `Array2D`/`Cube`/`CubeArray`'s own arms below), so the two
        // representations must be told apart dynamically here rather
        // than dispatched on `Shape` alone.
        Value *Offset = CI->getArgOperand(
            getDrefSampleOffsetIdx(DrefHasBias, DrefHasGrad, DrefHasLevel));
        Value *OffsetX = nullptr;
        Value *OffsetY = nullptr;
        Value *Offset1D = nullptr;
        if (Offset->getType()->isVectorTy()) {
          OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
          OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
          Offset1D = OffsetX;
        } else {
          Offset1D = Offset;
        }
        CallInst *NewCall;
        switch (Shape) {
        case ImageShape::Plain2D:
          NewCall = createSampleCmp2D(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, DUdX, DUdY, DVdX,
              DVdY, Lod, ExplicitLodFlag, Dref, Bias, OffsetX, OffsetY,
              MinLodClamp, Mask, CI->getName());
          break;
        case ImageShape::Array2D: {
          Value *ArrayLayer = Builder.CreateExtractElement(Coord, uint64_t{2});
          // Roadmap L66(g): `DUdX`/`DUdY`/`DVdX`/`DVdY` thread a real
          // derivative-driven implicit LOD through, mirroring `Plain2D`'s
          // own identical parameters -- zero constants for every
          // non-`Grad` form, degenerating to the same always-level-0
          // result as before.
          NewCall = createSampleCmpArray2D(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, ArrayLayer, DUdX,
              DUdY, DVdX, DVdY, Lod, ExplicitLodFlag, Dref, Bias, OffsetX,
              OffsetY, MinLodClamp, Mask, CI->getName());
          break;
        }
        case ImageShape::Cube: {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          // Roadmap L66(h): `CubeDDirXdX`/`CubeDDirXdY`/`CubeDDirYdX`/
          // `CubeDDirYdY`/`CubeDDirZdX`/`CubeDDirZdY` thread a real
          // direction-vector derivative sextuple through, mirroring
          // `createSampleCube`'s own identical parameters -- zero
          // constants for every non-`Grad` form, degenerating to the same
          // always-level-0 result as before.
          NewCall = createSampleCmpCube(Builder, Env, ImageIndex, SamplerIndex,
                                        C0, C1, C2, CubeDDirXdX, CubeDDirXdY,
                                        CubeDDirYdX, CubeDDirYdY, CubeDDirZdX,
                                        CubeDDirZdY, Lod, ExplicitLodFlag, Dref,
                                        Bias, MinLodClamp, Mask, CI->getName());
          break;
        }
        case ImageShape::CubeArray: {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          Value *ArrayLayer = Builder.CreateExtractElement(Coord, uint64_t{3});
          // Roadmap L66(i): `CubeDDirXdX`/`CubeDDirXdY`/`CubeDDirYdX`/
          // `CubeDDirYdY`/`CubeDDirZdX`/`CubeDDirZdY` thread a real
          // direction-vector derivative sextuple through, mirroring
          // `createSampleCubeArray`'s own identical parameters -- zero
          // constants for every non-`Grad` form, degenerating to the same
          // always-level-0 result as before.
          NewCall = createSampleCmpCubeArray(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, C2, CubeDDirXdX,
              CubeDDirXdY, CubeDDirYdX, CubeDDirYdY, CubeDDirZdX, CubeDDirZdY,
              ArrayLayer, Lod, ExplicitLodFlag, Dref, Bias, MinLodClamp, Mask,
              CI->getName());
          break;
        }
        case ImageShape::Plain1D:
          // Roadmap L54: unlike the ordinary-sample path (see the early
          // Plain1D/Array1D special case above), a real deqp-vk SPIR-V
          // capture confirms a 1D shadow sampler's own Coordinate operand
          // is already a genuine 3-component vector (`vec3(u, <unused>,
          // compare)`, GLSL's own `sampler1DShadow` convention), not a
          // bare scalar -- so the shared `C0`/`C1` extraction above
          // already works unmodified; only `C0` (the real `u`) is used.
          // Roadmap L62: `Bias`/`MinLodClamp` are the same two generically
          // extracted values every other shape's arm already passes.
          // Roadmap L66(f): `Grad1DDUdX`/`Grad1DDUdY` thread a real
          // derivative-driven implicit LOD through, mirroring `Plain2D`'s
          // own `DUdX`/`DUdY` -- zero constants for every non-`Grad` form,
          // degenerating to the same always-level-0 result as before.
          // Roadmap L66(k): `Offset1D` threads a real, bare-scalar
          // `ConstOffset` through, mirroring `createSample1D`'s own
          // identical `Offset` parameter for an ordinary sample.
          NewCall = createSampleCmp1D(Builder, Env, ImageIndex, SamplerIndex,
                                      C0, Grad1DDUdX, Grad1DDUdY, Lod,
                                      ExplicitLodFlag, Dref, Bias, Offset1D,
                                      MinLodClamp, Mask, CI->getName());
          break;
        case ImageShape::Array1D:
          // Same real-capture-confirmed shape as Plain1D just above, but
          // `vec3(u, layer, compare)` -- both `C0` (u) and `C1` (layer)
          // are real, meaningful components here. Roadmap L66(f):
          // `Grad1DDUdX`/`Grad1DDUdY` mirror `Plain1D`'s own identical new
          // parameters immediately above -- only `U`, never `ArrayLayer`,
          // is ever differentiated, matching `createSample1DArray`'s own
          // identical precedent for an ordinary sample. Roadmap L66(k):
          // `Offset1D` mirrors `Plain1D`'s own new `Offset1D` parameter
          // just above -- `Array1D`'s own `ConstOffset` never touches
          // `ArrayLayer` either, matching `createSample1DArray`'s own
          // identical precedent.
          NewCall = createSampleCmpArray1D(
              Builder, Env, ImageIndex, SamplerIndex, C0, C1, Grad1DDUdX,
              Grad1DDUdY, Lod, ExplicitLodFlag, Dref, Bias, Offset1D,
              MinLodClamp, Mask, CI->getName());
          break;
        case ImageShape::Plain3D:
        case ImageShape::Plain2DMS:
        case ImageShape::Array2DMS:
          // `hasOnlySupportedImageUses` already rejects a dref sample
          // against any of these shapes (roadmap L48's own comment),
          // so none of them is ever reached here.
          llvm_unreachable(
              "hasOnlySupportedImageUses should have rejected a dref "
              "sample against this shape");
        }
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L7d/H124q/H124r: `spirv.ImageDrefGather` (HLSL's
      // `Texture2D::GatherCmp()`/`Texture2DArray::GatherCmp()`/
      // `TextureCube::GatherCmp()`), `hasOnlySupportedImageUses` already
      // restricting this to `Plain2D`/`Array2D`/`Cube`, non-integer.
      // Reuses `femeRTComputeBilinearSupport` -- the exact four address-
      // mode-resolved texel corners an ordinary bilinear *sample* would
      // blend between are, by construction, the same four texels a
      // gather at the identical coordinate must return one component
      // from each of (SPIR-V/Vulkan's own fixed gather footprint), just
      // without actually blending them -- so `createGatherCmp2D`/
      // `createGatherCmpArray2D`/`createGatherCmpCube` below thread
      // `U`/`V`/(`ArrayLayer`)/`OffsetX`/`OffsetY` (or, for `Cube`, the
      // 3-component direction vector, with no offset at all) straight
      // through to `femeCpuImageGatherCmp2DV4F32`/
      // `femeCpuImageGatherCmpArray2DV4F32`/`femeCpuImageGatherCmpCubeV4F32`,
      // which themselves call that same runtime helper internally. Unlike
      // every `isDrefSampleIntrinsic` case above, there is no `Lod`/
      // `Bias`/`Grad`/`MinLod` operand to extract at all -- a gather
      // instruction always operates at mip level 0 per the SPIR-V spec,
      // with no way to request otherwise. `Array2D`'s/`Cube`'s own
      // coordinate is 3-wide (`Array2D`: `U`, `V`, `ArrayLayer`,
      // mirroring `Sample2DArray`'s own convention; `Cube`: a 3-component
      // direction vector, mirroring `SampleCube`'s own convention),
      // unlike `Plain2D`'s 2-wide one. `Cube`'s own `Offset` operand is
      // never read at all (unlike `Plain2D`/`Array2D`'s): SPIR-V forbids
      // `ConstOffset` against `Dim::Cube` outright, so
      // `hasOnlySupportedImageUses` above only ever accepts an always-
      // zero one there, which `createGatherCmpCube`'s own operand list
      // has no place for anyway.
      if (isGatherCmpIntrinsic(*CI)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(2);
        Value *Dref = CI->getArgOperand(DrefSampleDrefIdx);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1))).Index;
        Value *C0 = Builder.CreateExtractElement(Coord, uint64_t{0});
        Value *C1 = Builder.CreateExtractElement(Coord, uint64_t{1});
        CallInst *NewCall;
        if (Shape == ImageShape::Cube) {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          NewCall = createGatherCmpCube(Builder, Env, ImageIndex, SamplerIndex,
                                        C0, C1, C2, Dref, Mask, CI->getName());
        } else {
          Value *Offset = CI->getArgOperand(getDrefSampleOffsetIdx(false));
          Value *OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
          Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
          if (Shape == ImageShape::Array2D) {
            Value *ArrayLayer =
                Builder.CreateExtractElement(Coord, uint64_t{2});
            NewCall = createGatherCmpArray2D(
                Builder, Env, ImageIndex, SamplerIndex, C0, C1, ArrayLayer,
                Dref, OffsetX, OffsetY, Mask, CI->getName());
          } else {
            NewCall = createGatherCmp2D(Builder, Env, ImageIndex, SamplerIndex,
                                        C0, C1, Dref, OffsetX, OffsetY, Mask,
                                        CI->getName());
          }
        }
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L7g/H124q/H124r: `spirv.ImageGather` (HLSL's
      // `Texture2D::Gather{,Red,Green,Blue,Alpha}()`/`Texture2DArray::
      // Gather{,Red,Green,Blue,Alpha}()`/`TextureCube::Gather{,Red,Green,
      // Blue,Alpha}()`), `hasOnlySupportedImageUses` already restricting
      // this to `Plain2D`/`Array2D`/`Cube`, non-integer. Structurally
      // identical to the `isGatherCmpIntrinsic` case just above (same
      // bilinear-footprint reuse, same lack of a `Lod`/`Bias`/`Grad`/
      // `MinLod` operand, same `Array2D`/`Cube` 3-wide-coordinate
      // handling, same `Cube`-has-no-offset handling), but threads the
      // integer component selector through to `createGather2D`/
      // `createGatherArray2D`/`createGatherCube`/
      // `femeCpuImageGather2DV4F32`/`femeCpuImageGatherArray2DV4F32`/
      // `femeCpuImageGatherCubeV4F32` in place of a float `Dref`.
      if (isGatherIntrinsic(*CI)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(2);
        Value *Component = CI->getArgOperand(DrefSampleDrefIdx);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1))).Index;
        Value *C0 = Builder.CreateExtractElement(Coord, uint64_t{0});
        Value *C1 = Builder.CreateExtractElement(Coord, uint64_t{1});
        CallInst *NewCall;
        if (Shape == ImageShape::Cube) {
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          NewCall = createGatherCube(Builder, Env, ImageIndex, SamplerIndex, C0,
                                     C1, C2, Component, Mask, CI->getName());
        } else {
          Value *Offset = CI->getArgOperand(getDrefSampleOffsetIdx(false));
          Value *OffsetX = Builder.CreateExtractElement(Offset, uint64_t{0});
          Value *OffsetY = Builder.CreateExtractElement(Offset, uint64_t{1});
          if (Shape == ImageShape::Array2D) {
            Value *ArrayLayer =
                Builder.CreateExtractElement(Coord, uint64_t{2});
            NewCall = createGatherArray2D(
                Builder, Env, ImageIndex, SamplerIndex, C0, C1, ArrayLayer,
                Component, OffsetX, OffsetY, Mask, CI->getName());
          } else {
            NewCall = createGather2D(Builder, Env, ImageIndex, SamplerIndex, C0,
                                     C1, Component, OffsetX, OffsetY, Mask,
                                     CI->getName());
          }
        }
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L52e: `OpImageQueryLod`'s clamped/unclamped intrinsic
      // halves (`hasOnlySupportedImageUses` already restricts this to
      // `Plain2D`, non-integer). Unlike an ordinary sample,
      // `OpImageQueryLod` always measures the implicit LOD a
      // coordinate's own derivatives would produce -- there is no
      // explicit-LOD form to fall back to -- so derivatives are
      // unconditionally synthesized via `getOrSynthesizeSample2DDeriv
      // atives`, the same helper `Sample2D`'s own implicit-LOD path
      // calls (which itself still only produces a *real* derivative in
      // the one stage, `Fragment`, this instruction is ever legal from;
      // outside it, it degenerates to zero constants exactly as
      // `Sample2D`'s own call site does). `ImageQueryLodPattern` always
      // legalizes a single `OpImageQueryLod` into two separate
      // intrinsic calls sharing the same `(image, sampler, coord)`
      // operands, so each is lowered to its own independent
      // `createQueryLod2D` call here (redundantly recomputing the same
      // `<2 x float>` result twice, once per lane) rather than attempting
      // to share one call between them -- this pass does no cross-call
      // CSE of its own anywhere else either, relying on a later
      // optimization pipeline pass to fold the resulting duplicate,
      // side-effect-free calls if it chooses to.
      bool Unclamped = false;
      if (isQueryLodIntrinsic(*CI, Unclamped)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(2);
        Value *SamplerIndex =
            HeapIndices.lookup(cast<CallInst>(CI->getArgOperand(1))).Index;
        Value *C0 = Builder.CreateExtractElement(Coord, uint64_t{0});
        Value *C1 = Builder.CreateExtractElement(Coord, uint64_t{1});
        CallInst *NewCall;
        if (Shape == ImageShape::Cube) {
          // Roadmap H124u: a `Cube` handle's own `OpImageQueryLod`
          // coordinate is the 3-component direction vector itself (see
          // `hasOnlySupportedImageUses`'s own doc above) -- mirroring
          // `SampleCube`'s own implicit-LOD derivative synthesis, this
          // unconditionally synthesizes real (Fragment stage) or zero
          // (otherwise) derivatives via `getOrSynthesizeSampleCube
          // Derivatives`, then hands both the raw direction vector and
          // those derivatives to `createQueryLodCube`, which defers face
          // selection and face-local UV-derivative remapping to the
          // runtime function itself (`femeCpuImageQueryLodCubeV2F32`).
          Value *C2 = Builder.CreateExtractElement(Coord, uint64_t{2});
          CubeDirectionDerivatives CD = getOrSynthesizeSampleCubeDerivatives(
              Builder, *CI->getFunction(), C0, C1, C2);
          NewCall = createQueryLodCube(Builder, Env, ImageIndex, SamplerIndex,
                                       C0, C1, C2, CD.DDirXdX, CD.DDirXdY,
                                       CD.DDirYdX, CD.DDirYdY, CD.DDirZdX,
                                       CD.DDirZdY, Mask, "querylodcube");
        } else {
          SampleDerivatives D = getOrSynthesizeSample2DDerivatives(
              Builder, *CI->getFunction(), C0, C1);
          NewCall =
              createQueryLod2D(Builder, Env, ImageIndex, SamplerIndex, D.DUdX,
                               D.DUdY, D.DVdX, D.DVdY, Mask, "querylod2d");
        }
        // Lane 0 is the clamped level (`calculate.lod`), lane 1 the raw
        // unclamped LOD (`calculate.lod.unclamped`) -- mirroring
        // `ImageQueryLodPattern`'s own lane convention.
        Value *Lane = Builder.CreateExtractElement(
            NewCall, Unclamped ? uint64_t{1} : uint64_t{0});
        CI->replaceAllUsesWith(Lane);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L70: `OpImageQuerySize` (`isGetDimensionsIntrinsic`,
      // `llvm.spv.resource.getdimensions.xy`) against a plain 2D image --
      // unlike every sample/fetch/query-lod call above, this call's sole
      // operand *is* the handle itself (no separate `(image, ...)`
      // leading operand pair the way `isSampleIntrinsic`/
      // `isQueryLodIntrinsic` calls have), so there is no
      // `CI->getArgOperand(0) != Handle` guard to apply here.
      if (isGetDimensionsIntrinsic(*CI)) {
        IRBuilder<> Builder(CI);
        CallInst *NewCall = createGetDimensions2D(Builder, Env, ImageIndex,
                                                  Mask, "getdimensions2d");
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap H124s: `OpImageQuerySize` against an `Array2D` storage
      // image (`isGetDimensions3Intrinsic`, `hasOnlySupportedStorageImageUses`
      // already restricted this branch to `Array2D`) -- reuses
      // `QuerySizeLod2DArray`'s own runtime call with a synthesized
      // constant `Lod = 0`, since a storage image has exactly one mip
      // level and its formula for that level is otherwise identical.
      if (isGetDimensions3Intrinsic(*CI)) {
        IRBuilder<> Builder(CI);
        Value *ZeroLod = Builder.getInt32(0);
        CallInst *NewCall = createQuerySizeLod2DArray(
            Builder, Env, ImageIndex, ZeroLod, Mask, "getdimensions2darray");
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L72(d)/L75: `OpImageQuerySizeLod` (`isQuerySizeLodCall`) --
      // an explicit, possibly non-zero mip-level extent query, unlike
      // `GetDimensions2D`'s own always-mip-0 query above. Its own Image
      // operand is `getArgOperand(0)` (mirroring `isSampleIntrinsic`'s
      // own convention, unlike `isGetDimensionsIntrinsic`'s bare-handle
      // call), so the `CI->getArgOperand(0) != Handle` guard does apply
      // here. Dispatches to the builder matching this handle's own
      // classified `Shape` -- `Cube` reuses `QuerySizeLod2D` unchanged
      // (identical `v2i32` result/formula to `Plain2D`), every other
      // shape uses its own dedicated builder (see each one's own doc for
      // its distinct result width/formula).
      if (isQuerySizeLodCall(*CI)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        Value *Lod = CI->getArgOperand(1);
        CallInst *NewCall;
        switch (Shape) {
        case ImageShape::Plain1D:
          NewCall = createQuerySizeLod1D(Builder, Env, ImageIndex, Lod, Mask,
                                         "querysizelod1d");
          break;
        case ImageShape::Array1D:
          NewCall = createQuerySizeLod1DArray(Builder, Env, ImageIndex, Lod,
                                              Mask, "querysizelod1darray");
          break;
        case ImageShape::Array2D:
          NewCall = createQuerySizeLod2DArray(Builder, Env, ImageIndex, Lod,
                                              Mask, "querysizelod2darray");
          break;
        case ImageShape::Plain3D:
          NewCall = createQuerySizeLod3D(Builder, Env, ImageIndex, Lod, Mask,
                                         "querysizelod3d");
          break;
        case ImageShape::CubeArray:
          NewCall = createQuerySizeLodCubeArray(Builder, Env, ImageIndex, Lod,
                                                Mask, "querysizelodcubearray");
          break;
        case ImageShape::Plain2D:
        case ImageShape::Cube:
        default:
          NewCall = createQuerySizeLod2D(Builder, Env, ImageIndex, Lod, Mask,
                                         "querysizelod2d");
          break;
        }
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L72(d): `OpImageQueryLevels` (`isQueryLevelsCall`) -- an
      // image's own total mip-level count, needing neither a `Mask` (no
      // per-invocation side effect to guard) nor an explicit mip level of
      // its own.
      if (isQueryLevelsCall(*CI)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        CallInst *NewCall =
            createQueryLevels(Builder, Env, ImageIndex, "querylevels");
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L73: `OpImageQuerySamples` (`isQuerySamplesCall`) -- a
      // multisampled image's own sample count. Structurally identical to
      // `QueryLevels` immediately above (no `Mask`, no explicit mip
      // level), just a different runtime field read --
      // `hasOnlySupportedImageUses` already restricted this branch to
      // `Plain2DMS`/`Array2DMS`.
      if (isQuerySamplesCall(*CI)) {
        if (CI->getArgOperand(0) != Handle)
          continue;
        IRBuilder<> Builder(CI);
        CallInst *NewCall =
            createQuerySamples(Builder, Env, ImageIndex, "querysamples");
        CI->replaceAllUsesWith(NewCall);
        CI->eraseFromParent();
        continue;
      }

      // Roadmap L72/L72(b)/L72(c): an explicit-mip `texelFetch()`/
      // `texelFetchOffset()` (`llvm.spv.resource.load.level`, see
      // `isFetchLevelIntrinsic`'s own doc) -- unlike the zero-mip
      // `getpointer`-based fetch handled below (whose call itself is only
      // a pointer, addressed by a separate `LoadInst` user), this
      // intrinsic's own result *is* the fetched texel, and its own third
      // operand is a real, non-zero mip level -- so both the coordinate
      // extraction and the runtime call happen right here, rather than
      // falling through to that shared `LoadInst`-dispatch switch below.
      // `hasOnlySupportedImageUses` already restricted this branch to
      // `Plain1D`/`Array1D`/`Plain2D`/`Array2D`/`Plain3D`. None of
      // `createLoad1D`/`createLoad1DArray`/`createLoad2D`/
      // `createLoad2DArray`/`createLoad3D` (nor their `I32` counterparts)
      // take an offset operand of their own (unlike the sampling helpers'
      // `OffsetX`/`OffsetY`), so a real `ConstOffset`
      // (`hasOnlySupportedImageUses` already validated it via
      // `isSupportedOffset`) is folded into the coordinate components
      // themselves here instead, before the runtime call. `Plain1D`'s own
      // `ConstOffset` is a bare scalar `i32` (mirroring
      // `isSupportedOffset`'s own `Is1D` acceptance), so only its lone `X`
      // component ever needs folding; `Array1D`'s own 2-wide `(x, layer)`
      // coordinate shares that same scalar-offset convention -- only `X`
      // folds in a real offset, `Layer` is untouched, since SPIR-V's own
      // `ConstOffset` dimensionality tracks the image's real dimension
      // count, excluding any array layer (see `isSupportedOffset`'s own
      // comment).
      if (isFetchLevelIntrinsic(*CI)) {
        IRBuilder<> Builder(CI);
        Value *Coord = CI->getArgOperand(1);
        Value *Lod = CI->getArgOperand(2);
        Value *Offset = CI->getArgOperand(3);
        bool IsInteger = isV4I32(CI->getType());
        CallInst *Fetched;
        switch (Shape) {
        case ImageShape::Plain1D: {
          Value *X = Builder.CreateAdd(Coord, Offset);
          Fetched = IsInteger ? createLoad1DI32(Builder, Env, ImageIndex, X,
                                                Lod, Mask, CI->getName())
                              : createLoad1D(Builder, Env, ImageIndex, X, Lod,
                                             Builder.getInt32(0), Mask,
                                             CI->getName());
          break;
        }
        case ImageShape::Array1D: {
          Value *X = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{0}), Offset);
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{1});
          Fetched =
              IsInteger
                  ? createLoad1DArrayI32(Builder, Env, ImageIndex, X, Layer,
                                         Lod, Mask, CI->getName())
                  : createLoad1DArray(Builder, Env, ImageIndex, X, Layer, Lod,
                                      Builder.getInt32(0), Mask, CI->getName());
          break;
        }
        case ImageShape::Plain2D: {
          Value *X = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{0}),
              Builder.CreateExtractElement(Offset, uint64_t{0}));
          Value *Y = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{1}),
              Builder.CreateExtractElement(Offset, uint64_t{1}));
          Fetched =
              IsInteger
                  ? createLoad2DI32(Builder, Env, ImageIndex, X, Y, Lod,
                                    Builder.getInt32(0), Mask, CI->getName())
                  : createLoad2D(Builder, Env, ImageIndex, X, Y, Lod,
                                 Builder.getInt32(0), Mask, CI->getName());
          break;
        }
        case ImageShape::Array2D: {
          Value *X = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{0}),
              Builder.CreateExtractElement(Offset, uint64_t{0}));
          Value *Y = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{1}),
              Builder.CreateExtractElement(Offset, uint64_t{1}));
          Value *Layer = Builder.CreateExtractElement(Coord, uint64_t{2});
          Fetched = IsInteger
                        ? createLoad2DArrayI32(Builder, Env, ImageIndex, X, Y,
                                               Layer, Lod, Builder.getInt32(0),
                                               Mask, CI->getName())
                        : createLoad2DArray(Builder, Env, ImageIndex, X, Y,
                                            Layer, Lod, Builder.getInt32(0),
                                            Mask, CI->getName());
          break;
        }
        case ImageShape::Plain3D: {
          Value *X = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{0}),
              Builder.CreateExtractElement(Offset, uint64_t{0}));
          Value *Y = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{1}),
              Builder.CreateExtractElement(Offset, uint64_t{1}));
          Value *Z = Builder.CreateAdd(
              Builder.CreateExtractElement(Coord, uint64_t{2}),
              Builder.CreateExtractElement(Offset, uint64_t{2}));
          Fetched = IsInteger ? createLoad3DI32(Builder, Env, ImageIndex, X, Y,
                                                Z, Lod, Mask, CI->getName())
                              : createLoad3D(Builder, Env, ImageIndex, X, Y, Z,
                                             Lod, Builder.getInt32(0), Mask,
                                             CI->getName());
          break;
        }
        default:
          llvm_unreachable("hasOnlySupportedImageUses should have rejected "
                           "every other shape for isFetchLevelIntrinsic");
        }
        CI->replaceAllUsesWith(Fetched);
        CI->eraseFromParent();
        continue;
      }

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
      // `Array1D` (roadmap H19e) has no spatial `Y` component at all --
      // its 2-component coordinate is `(x, layer)`, not `(x, y)` -- so `Y`
      // stays null for it too, just like `Plain1D`.
      Value *Y = (Shape == ImageShape::Plain1D || Shape == ImageShape::Array1D)
                     ? nullptr
                     : Builder.CreateExtractElement(Coord, uint64_t{1});
      // The coordinate's own array-layer/depth-slice/sample component: an
      // array layer for `Array1D` (roadmap H19e, the coordinate's 2nd
      // component), `Array2D` (roadmap H19b, the coordinate's 3rd
      // component), or `Array2DMS` (roadmap H19m, also the 3rd
      // component), a real depth-slice `Z` coordinate for `Plain3D`
      // (roadmap H19c, also the 3rd component), or a multisample index for
      // `Plain2DMS` (roadmap H19g, also the 3rd component,
      // `SPIRVToLLVMPatterns.cpp`'s own `Sample`-image-operand-to-
      // coordinate widening) -- distinct concepts sharing one variable
      // here, never more than one at once (`classifyStorageImage2DHandle`
      // never returns a shape that is both arrayed and 3D, nor an
      // ordinary-vs-multisample distinction at this same component;
      // `Array2DMS` alone needs a 4th component too, see `C3` below).
      Value *C2 =
          Shape == ImageShape::Array1D
              ? Builder.CreateExtractElement(Coord, uint64_t{1})
          : (Shape == ImageShape::Array2D || Shape == ImageShape::Plain3D ||
             Shape == ImageShape::Plain2DMS || Shape == ImageShape::Array2DMS)
              ? Builder.CreateExtractElement(Coord, uint64_t{2})
              : nullptr;
      // `Array2DMS`'s own 4th coordinate component (roadmap H19m): the
      // multisample index, the same concept `Plain2DMS`'s own `C2`
      // carries for the non-arrayed case -- `Array2DMS`'s coordinate is
      // `(x, y, layer, sample)`, one component wider than every other
      // shape's own coordinate, since it is the only shape combining an
      // array layer and a multisample index at once.
      Value *C3 = Shape == ImageShape::Array2DMS
                      ? Builder.CreateExtractElement(Coord, uint64_t{3})
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
          bool IsInteger = isIntegerStorageTexelType(Texel->getType());
          // Roadmap L76(a): a narrower-than-4-wide Texel (e.g. a bare
          // scalar for `RWTexture2D<float>`, or a 2/3-wide vector for a
          // 2/3-channel format) needs widening up to the fixed 4-wide
          // width every `feme.cpu.image.store.*` runtime entry point
          // below requires -- see `widenStorageImageTexel`'s own doc.
          Texel = widenStorageImageTexel(StoreBuilder, Texel, IsInteger);
          switch (Shape) {
          case ImageShape::Plain1D:
            if (IsInteger)
              createStore1DI32(StoreBuilder, Env, ImageIndex, X, Texel, Mask);
            else
              createStore1D(StoreBuilder, Env, ImageIndex, X, Texel, Mask);
            break;
          case ImageShape::Array1D:
            if (IsInteger)
              createStore1DArrayI32(StoreBuilder, Env, ImageIndex, X, C2, Texel,
                                    Mask);
            else
              createStore1DArray(StoreBuilder, Env, ImageIndex, X, C2, Texel,
                                 Mask);
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
              createStore2DArray(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                                 Mask);
            break;
          case ImageShape::Plain3D:
            if (IsInteger)
              createStore3DI32(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                               Mask);
            else
              createStore3D(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                            Mask);
            break;
          case ImageShape::Plain2DMS:
            // Roadmap H19g: `C2` is the multisample index here, not an
            // array layer/depth slice -- `createStore2DMS`/`I32` are the
            // write-side counterparts of `createLoad2D`/`I32`'s own
            // `Sample` operand.
            if (IsInteger)
              createStore2DMSI32(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                                 Mask);
            else
              createStore2DMS(StoreBuilder, Env, ImageIndex, X, Y, C2, Texel,
                              Mask);
            break;
          case ImageShape::Array2DMS:
            // Roadmap H19m: `C2` is the array layer and `C3` is the
            // multisample index -- `createStore2DArrayMS`/`I32` are new,
            // dedicated call kinds (not a widened `Store2DArray`/
            // `Store2DMS`) since neither existing kind had a spare operand
            // slot for the other axis.
            if (IsInteger)
              createStore2DArrayMSI32(StoreBuilder, Env, ImageIndex, X, Y, C2,
                                      C3, Texel, Mask);
            else
              createStore2DArrayMS(StoreBuilder, Env, ImageIndex, X, Y, C2, C3,
                                   Texel, Mask);
            break;
          case ImageShape::Cube:
          case ImageShape::CubeArray:
            llvm_unreachable("no storage-image write shape for Cube/CubeArray");
          }
          SI->eraseFromParent();
          continue;
        }
        // `AtomicRMWInst`/`AtomicCmpXchgInst` (roadmap H8v): the only
        // storage-image shape reaching either is `Plain2D`
        // (`hasOnlySupportedStorageImageUses` already rejected every
        // other shape for these two instruction kinds), so `X`/`Y` are
        // always meaningful here with no `Array1D`/`Array2D`/`Plain3D`/
        // multisample coordinate component ever needed.
        if (auto *RMW = dyn_cast<AtomicRMWInst>(PU)) {
          IRBuilder<> AtomicBuilder(RMW);
          Value *Val = RMW->getValOperand();
          CallInst *Old;
          switch (RMW->getOperation()) {
          case AtomicRMWInst::Add:
            Old = createAtomicAdd2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                    Mask, RMW->getName());
            break;
          case AtomicRMWInst::Sub:
            Old = createAtomicSub2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                    Mask, RMW->getName());
            break;
          case AtomicRMWInst::And:
            Old = createAtomicAnd2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                    Mask, RMW->getName());
            break;
          case AtomicRMWInst::Or:
            Old = createAtomicOr2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                   Mask, RMW->getName());
            break;
          case AtomicRMWInst::Xor:
            Old = createAtomicXor2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                    Mask, RMW->getName());
            break;
          case AtomicRMWInst::Max:
            Old = createAtomicSMax2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                     Mask, RMW->getName());
            break;
          case AtomicRMWInst::Min:
            Old = createAtomicSMin2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                     Mask, RMW->getName());
            break;
          case AtomicRMWInst::UMax:
            Old = createAtomicUMax2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                     Mask, RMW->getName());
            break;
          case AtomicRMWInst::UMin:
            Old = createAtomicUMin2D(AtomicBuilder, Env, ImageIndex, X, Y, Val,
                                     Mask, RMW->getName());
            break;
          case AtomicRMWInst::Xchg:
            Old = createAtomicExchange2D(AtomicBuilder, Env, ImageIndex, X, Y,
                                         Val, Mask, RMW->getName());
            break;
          default:
            llvm_unreachable(
                "hasOnlySupportedStorageImageUses only accepts the RMW "
                "kinds handled above");
          }
          RMW->replaceAllUsesWith(Old);
          RMW->eraseFromParent();
          continue;
        }
        if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(PU)) {
          IRBuilder<> AtomicBuilder(CmpXchg);
          CallInst *Old = createAtomicCompareExchange2D(
              AtomicBuilder, Env, ImageIndex, X, Y,
              CmpXchg->getCompareOperand(), CmpXchg->getNewValOperand(), Mask,
              CmpXchg->getName());
          // `hasOnlySupportedStorageImageUses` already guaranteed every
          // user of `CmpXchg` is an `extractvalue ..., 0` picking out the
          // old value (SPIR-V's own result) -- replace each with the new
          // call directly, since the call's own result *is* that old
          // value (unlike `llvm.cmpxchg`, no `{i32, i1}` struct to
          // extract from).
          for (User *EU : llvm::make_early_inc_range(CmpXchg->users())) {
            auto *EV = cast<ExtractValueInst>(EU);
            EV->replaceAllUsesWith(Old);
            EV->eraseFromParent();
          }
          CmpXchg->eraseFromParent();
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
        // F8c). The loaded type -- an integer or float scalar/vector of
        // any width `storageImageTexelWidth` accepts (roadmap L76(a)),
        // `hasOnlySupportedStorageImageUses`'s own per-handle check
        // already guaranteed one or the other -- selects the integer
        // (roadmap E26) or float `feme.cpu.image.load.*` entry point;
        // its own always-4-wide result is narrowed to this real width
        // below.
        bool IsInteger = isIntegerStorageTexelType(LI->getType());
        CallInst *Loaded;
        switch (Shape) {
        case ImageShape::Plain1D:
          Loaded = IsInteger ? createLoad1DI32(LoadBuilder, Env, ImageIndex, X,
                                               LoadBuilder.getInt32(0), Mask,
                                               LI->getName())
                             : createLoad1D(LoadBuilder, Env, ImageIndex, X,
                                            LoadBuilder.getInt32(0),
                                            LoadBuilder.getInt32(0), Mask,
                                            LI->getName());
          break;
        case ImageShape::Array1D:
          Loaded = IsInteger
                       ? createLoad1DArrayI32(LoadBuilder, Env, ImageIndex, X,
                                              C2, LoadBuilder.getInt32(0), Mask,
                                              LI->getName())
                       : createLoad1DArray(LoadBuilder, Env, ImageIndex, X, C2,
                                           LoadBuilder.getInt32(0),
                                           LoadBuilder.getInt32(0), Mask,
                                           LI->getName());
          break;
        case ImageShape::Plain2D:
          Loaded = IsInteger ? createLoad2DI32(LoadBuilder, Env, ImageIndex, X,
                                               Y, LoadBuilder.getInt32(0),
                                               LoadBuilder.getInt32(0), Mask,
                                               LI->getName())
                             : createLoad2D(LoadBuilder, Env, ImageIndex, X, Y,
                                            LoadBuilder.getInt32(0),
                                            LoadBuilder.getInt32(0), Mask,
                                            LI->getName());
          break;
        case ImageShape::Array2D:
          Loaded = IsInteger
                       ? createLoad2DArrayI32(LoadBuilder, Env, ImageIndex, X,
                                              Y, C2, LoadBuilder.getInt32(0),
                                              LoadBuilder.getInt32(0), Mask,
                                              LI->getName())
                       : createLoad2DArray(LoadBuilder, Env, ImageIndex, X, Y,
                                           C2, LoadBuilder.getInt32(0),
                                           LoadBuilder.getInt32(0), Mask,
                                           LI->getName());
          break;
        case ImageShape::Plain3D:
          Loaded = IsInteger ? createLoad3DI32(LoadBuilder, Env, ImageIndex, X,
                                               Y, C2, LoadBuilder.getInt32(0),
                                               Mask, LI->getName())
                             : createLoad3D(LoadBuilder, Env, ImageIndex, X, Y,
                                            C2, LoadBuilder.getInt32(0),
                                            LoadBuilder.getInt32(0), Mask,
                                            LI->getName());
          break;
        case ImageShape::Plain2DMS:
          // Roadmap H19g: `C2` is the real multisample index here, unlike
          // every other shape's own `getInt32(0)` `Sample`/`Mip` argument
          // -- `Load2D`/`Load2DI32` already accept a `Sample` operand
          // (roadmap F8c added it to `Load2D` for `subpassLoad`'s
          // explicit-sample form; this row widens `Load2DI32` to match).
          Loaded = IsInteger ? createLoad2DI32(LoadBuilder, Env, ImageIndex, X,
                                               Y, LoadBuilder.getInt32(0), C2,
                                               Mask, LI->getName())
                             : createLoad2D(LoadBuilder, Env, ImageIndex, X, Y,
                                            LoadBuilder.getInt32(0), C2, Mask,
                                            LI->getName());
          break;
        case ImageShape::Array2DMS:
          // Roadmap H19m: `C2` is the array layer and `C3` is the real
          // multisample index. Unlike the write side, neither read-side
          // runtime helper needed new vocabulary: `Load2DArray`'s own
          // float entry point already accepted a real `Sample` operand
          // (it just always saw `getInt32(0)` before this row); only
          // `Load2DArrayI32` needed widening to add the same operand
          // `Load2DArray` already had, mirroring how roadmap H19g widened
          // `Load2DI32` to match `Load2D`.
          Loaded = IsInteger
                       ? createLoad2DArrayI32(LoadBuilder, Env, ImageIndex, X,
                                              Y, C2, LoadBuilder.getInt32(0),
                                              C3, Mask, LI->getName())
                       : createLoad2DArray(LoadBuilder, Env, ImageIndex, X, Y,
                                           C2, LoadBuilder.getInt32(0), C3,
                                           Mask, LI->getName());
          break;
        case ImageShape::Cube:
        case ImageShape::CubeArray:
          llvm_unreachable("no storage-image fetch shape for Cube/CubeArray");
        }
        // Roadmap L76(a): narrow the runtime call's always-4-wide result
        // down to `LI`'s own real result width (a bare scalar or a
        // narrower vector) before replacing its uses -- see
        // `narrowStorageImageTexel`'s own doc.
        Value *Result =
            narrowStorageImageTexel(LoadBuilder, Loaded, LI->getType());
        LI->replaceAllUsesWith(Result);
        LI->eraseFromParent();
      }
      CI->eraseFromParent();
    }
  }

  // Erased last: a sampler handle still had the sample calls as users while
  // the image side of the loop above was rewriting them. Roadmap L66(e): a
  // handle can still have real, live users here even after the loop above --
  // not the "either the whole function was accepted, or none of it was"
  // state this function's own header comment describes -- when its own
  // paired image/sampler handle was excluded from `HeapIndices` entirely
  // because *that* handle's own (set, binding) identity conflicted with a
  // different declaration elsewhere in the module (`run`'s own
  // `Entry.Conflicting` skip, checked per handle, not per function). Every
  // sample/fetch call reached only from such an excluded handle's own
  // partner is deliberately left unrewritten above
  // (`CI->getArgOperand(0) != Handle` for a sample, mirrored for a fetch's
  // own image-only pointer), so this handle is not actually dead:
  // unconditionally erasing it here was a real use-after-free (confirmed via
  // a minimal repro: two functions each declaring an image handle at an
  // identical binding for two different shapes, sharing one consistent,
  // non-conflicting sampler binding between them) -- `Instruction::
  // eraseFromParent` deletes a handle a still-live call elsewhere still
  // references. Leave any handle with remaining users alone instead, for
  // `feme::cpu::checkSupportedRaisedOps` to reject, exactly like a
  // conflicting buffer handle already is (see `lowerAccesses`'s own
  // `Conflicting` skip in `run` above).
  for (const auto &[Handle, Entry] : HeapIndices) {
    (void)Entry;
    if (Handle->use_empty())
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
/// register-bound root constant, so there is nothing else to report),
/// \p RootConstantMinOffset (roadmap H6u: see
/// `feme::cpu::ResourceInfo::RootConstantMinOffset`'s own comment; 0 if
/// this function has no push-constant access of its own, matching
/// `RootConstantSize`'s own "0 if it has none" default), and an empty
/// statically-known-heap-index tail. That tail always stays empty here
/// regardless of whether a given access went through a compile-time-
/// constant or (roadmap R26) dynamic array index -- it is
/// `feme::cpu::ResourceLoweringPass`'s own dynamic-heap discovery
/// mechanism, unrelated to the bound-range assignment
/// `attachBoundResourceMetadata` below records unconditionally (see "Heap
/// usage discovery" in feme/docs/FeMeCPUDesign.md).
void attachResourceMetadata(Function &F, uint32_t RootConstantSize,
                            bool UsesSamplerHeap,
                            uint32_t RootConstantMinOffset) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Metadata *Ops[] = {
      MDString::get(Ctx, F.getName()),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSize)),
      ConstantAsMetadata::get(ConstantInt::getBool(Ctx, UsesSamplerHeap)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantMinOffset))};
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
      Value *Index = computeClampedIndex(Builder, BH.Handle->getArgOperand(3),
                                         Entry.HeapBase, BH.RangeSize);
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
    uint32_t RootConstantMinOffset = 0;
    if (std::optional<SPIRVPushConstantAccess> PCAccess =
            matchSPIRVPushConstantAccess(*NewF)) {
      PushConstantAccessSpan Span = lowerSPIRVPushConstantAccess(
          *PCAccess, Env.RootConstants, Env.RootConstantSize);
      RootConstantSize = Span.MaxOffset;
      RootConstantMinOffset = Span.MinOffset;
    }

    attachResourceMetadata(*NewF, RootConstantSize, UsesSamplerHeap,
                           RootConstantMinOffset);
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
        ID == Intrinsic::spv_resource_getpointer ||
        ID == Intrinsic::spv_resource_getdimensions_xy)
      F.eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
