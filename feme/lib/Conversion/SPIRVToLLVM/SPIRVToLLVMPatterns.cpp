//===- SPIRVToLLVMPatterns.cpp - FeMe's spirv -> llvm patterns -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"

#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"

namespace {

/// Benefit given to FeMe's patterns, so they win over the MLIR pattern for
/// the same op where both exist.
constexpr unsigned FeMeBenefit = 2;

/// A SPIR-V builtin input variable and the `llvm.spv.*` intrinsic LLVM's
/// SPIRV backend reads the same value with. A `PerComponent` intrinsic takes
/// the index of the component being read and returns a scalar, so a
/// vector-valued builtin is read one component at a time.
struct BuiltInMapping {
  mlir::spirv::BuiltIn BuiltIn;
  llvm::StringLiteral Intrinsic;
  bool PerComponent;
};

constexpr BuiltInMapping BuiltInMappings[] = {
    {mlir::spirv::BuiltIn::GlobalInvocationId, "llvm.spv.thread.id", true},
    {mlir::spirv::BuiltIn::WorkgroupId, "llvm.spv.group.id", true},
    {mlir::spirv::BuiltIn::LocalInvocationId, "llvm.spv.thread.id.in.group",
     true},
    {mlir::spirv::BuiltIn::WorkgroupSize, "llvm.spv.workgroup.size", true},
    {mlir::spirv::BuiltIn::NumWorkgroups, "llvm.spv.num.workgroups", true},
    {mlir::spirv::BuiltIn::GlobalSize, "llvm.spv.global.size", true},
    {mlir::spirv::BuiltIn::GlobalOffset, "llvm.spv.global.offset", true},
    {mlir::spirv::BuiltIn::LocalInvocationIndex,
     "llvm.spv.flattened.thread.id.in.group", false},
    {mlir::spirv::BuiltIn::SubgroupSize, "llvm.spv.subgroup.size", false},
    {mlir::spirv::BuiltIn::NumSubgroups, "llvm.spv.num.subgroups", false},
    {mlir::spirv::BuiltIn::SubgroupId, "llvm.spv.subgroup.id", false},
    {mlir::spirv::BuiltIn::SubgroupLocalInvocationId,
     "llvm.spv.subgroup.local.invocation.id", false},
};

/// Returns the intrinsic reading \p Global's builtin, or nullptr if \p Global
/// is not a builtin variable, or is one with no LLVM equivalent.
const BuiltInMapping *getBuiltInMapping(mlir::spirv::GlobalVariableOp Global) {
  std::optional<llvm::StringRef> Name = Global.getBuiltIn();
  if (!Name)
    return nullptr;
  std::optional<mlir::spirv::BuiltIn> BuiltIn =
      mlir::spirv::symbolizeBuiltIn(*Name);
  if (!BuiltIn)
    return nullptr;
  for (const BuiltInMapping &Mapping : BuiltInMappings)
    if (Mapping.BuiltIn == *BuiltIn)
      return &Mapping;
  return nullptr;
}

/// One of the five `GroupNonUniformBallot`-gated subgroup mask builtin
/// *variables*, each a `uvec4` naming the invocations in the subgroup that
/// compare a given way against the current one.
///
/// Unlike every `BuiltInMappings[]` entry, none of these is a single
/// `llvm.spv.*` intrinsic read: each is a small computed function of
/// `SubgroupLocalInvocationId` and `SubgroupSize` that `buildSubgroupMask`
/// below emits directly (roadmap L89g).
enum class SubgroupMaskKind { Eq, Ge, Gt, Le, Lt };

/// Returns which subgroup mask builtin \p Global is, or `std::nullopt` if it
/// is not one.
std::optional<SubgroupMaskKind>
getSubgroupMaskBuiltIn(mlir::spirv::GlobalVariableOp Global) {
  std::optional<llvm::StringRef> Name = Global.getBuiltIn();
  if (!Name)
    return std::nullopt;
  std::optional<mlir::spirv::BuiltIn> BuiltIn =
      mlir::spirv::symbolizeBuiltIn(*Name);
  if (!BuiltIn)
    return std::nullopt;
  switch (*BuiltIn) {
  case mlir::spirv::BuiltIn::SubgroupEqMask:
    return SubgroupMaskKind::Eq;
  case mlir::spirv::BuiltIn::SubgroupGeMask:
    return SubgroupMaskKind::Ge;
  case mlir::spirv::BuiltIn::SubgroupGtMask:
    return SubgroupMaskKind::Gt;
  case mlir::spirv::BuiltIn::SubgroupLeMask:
    return SubgroupMaskKind::Le;
  case mlir::spirv::BuiltIn::SubgroupLtMask:
    return SubgroupMaskKind::Lt;
  default:
    return std::nullopt;
  }
}

/// Returns true if \p Global is a builtin variable this conversion models as
/// a *value* -- either one `BuiltInMappings[]` reads with a single intrinsic
/// or one of `getSubgroupMaskBuiltIn`'s computed masks -- rather than as the
/// interface memory an ordinary stage-IO variable converts to.
bool isValueModeledBuiltIn(mlir::spirv::GlobalVariableOp Global) {
  return getBuiltInMapping(Global) || getSubgroupMaskBuiltIn(Global);
}

/// Returns the global variable \p Op takes the address of, or a null op if
/// there is no such symbol.
mlir::spirv::GlobalVariableOp getReferencedGlobal(mlir::spirv::AddressOfOp Op) {
  return mlir::SymbolTable::lookupNearestSymbolFrom<
      mlir::spirv::GlobalVariableOp>(Op->getParentOp(), Op.getVariableAttr());
}

/// Returns true if \p Type is a pointer to a SPIR-V resource -- an image,
/// a sampled image or a sampler -- which LLVM models as an opaque handle
/// value obtained from its binding rather than as memory.
bool isResourcePointer(mlir::spirv::PointerType Type) {
  if (Type.getStorageClass() != mlir::spirv::StorageClass::UniformConstant)
    return false;
  return mlir::isa<mlir::spirv::ImageType, mlir::spirv::SampledImageType,
                   mlir::spirv::SamplerType>(Type.getPointeeType());
}

/// A storage/uniform buffer block's real content, and how
/// `spirv.AccessChain` reaches it, recovered from the block variable's
/// pointer type by getBufferBlockElement/getUniformBlockElement below.
///
/// FeMe's own upstream HLSL resource representation always spells a block
/// as a `Block`-decorated struct with exactly one member wrapping the real
/// content -- a runtime array for `RWStructuredBuffer<T>`/
/// `StructuredBuffer<T>`, or the block's own field struct for `cbuffer`/
/// `ConstantBuffer<T>` (see
/// https://github.com/llvm/wg-hlsl/blob/main/proposals/0018-spirv-resource-representation.md).
/// glslang's own output never adds that wrapper: the block variable points
/// directly at its own `Block`/`BufferBlock`-decorated struct, which is
/// free to declare more than one member (fixed header fields alongside a
/// trailing runtime array -- the only place SPIR-V allows one, and the
/// shape a plain GLSL `buffer`/`uniform` block with a leading scalar and a
/// trailing unsized array compiles to), a sized-array member, or a matrix
/// member. Both shapes convert to the same `spirv.VulkanBuffer` handle (see
/// convertBlockType below); they differ only in whether `spirv.AccessChain`'s
/// leading index is a dummy selector into the wrapper (always the constant
/// 0, dropped by the access chain patterns below) or the content's own
/// first real member selector.
///
/// A uniform block whose sole member is itself a fixed-size array (a plain
/// GLSL `uniform Input { uint data[16]; }`, roadmap F12a) is recognized the
/// same way: `getUniformBlockElement` treats that array as `Content` with
/// `HasWrapper` true, exactly as if it were the dxc wrapper's own sole
/// member, since one member wrapping the real (dynamically-indexed)
/// content is the same shape either way -- a std140 array's own
/// `ArrayStride` need not equal its element's natural size the way a
/// std430 storage buffer array's always does, so it cannot be converted
/// through the ordinary `TypeConverter` the way every other `Content` shape
/// is (see `convertUniformBlockType`'s own comment).
struct BlockElement {
  /// What convertBlockType converts to `spirv.VulkanBuffer`'s sole type
  /// parameter.
  mlir::Type Content;
  /// True if `spirv.AccessChain`'s leading index selects FeMe's own
  /// wrapper's sole member (always 0) rather than a member of Content
  /// directly.
  bool HasWrapper;
};

/// Returns true if \p Struct is decorated as a storage buffer block, either
/// the SPIR-V-1.3-and-later spelling (a `StorageBuffer`-class pointer to a
/// `Block`-decorated struct) or the pre-1.3 one (a `Uniform`-class pointer
/// to a `BufferBlock`-decorated struct) -- glslang emits the latter when
/// targeting an older client API version, and the two decorations are
/// otherwise interchangeable (see the SPIR-V spec's `BufferBlock` entry).
bool isBufferBlockStorage(mlir::spirv::PointerType Type,
                          mlir::spirv::StructType Struct) {
  if (Type.getStorageClass() == mlir::spirv::StorageClass::StorageBuffer)
    return Struct.hasDecoration(mlir::spirv::Decoration::Block);
  return Type.getStorageClass() == mlir::spirv::StorageClass::Uniform &&
         Struct.hasDecoration(mlir::spirv::Decoration::BufferBlock);
}

/// Returns \p Type's storage buffer block content (see BlockElement's own
/// comment for the two shapes covered), or `std::nullopt` if \p Type is not
/// a storage buffer block pointer at all.
std::optional<BlockElement>
getBufferBlockElement(mlir::spirv::PointerType Type) {
  auto Struct = mlir::dyn_cast<mlir::spirv::StructType>(Type.getPointeeType());
  if (!Struct || Struct.getNumElements() == 0 ||
      !isBufferBlockStorage(Type, Struct))
    return std::nullopt;
  if (Struct.getNumElements() == 1) {
    if (auto Array = mlir::dyn_cast<mlir::spirv::RuntimeArrayType>(
            Struct.getElementType(0)))
      return BlockElement{Array, /*HasWrapper=*/true};
  }
  return BlockElement{Struct, /*HasWrapper=*/false};
}

/// Returns true if \p Type is a pointer to a storage buffer block -- see
/// getBufferBlockElement.
bool isBufferBlockPointer(mlir::spirv::PointerType Type) {
  return static_cast<bool>(getBufferBlockElement(Type));
}

/// Returns the index of \p Struct's trailing runtime array member (the only
/// place SPIR-V allows one), or `std::nullopt` if it has none -- a plain
/// multi-member uniform block spelled as a `BufferBlock`/`Block` struct with
/// no dynamically-indexed content, which is unusual but not invalid.
std::optional<unsigned>
getTrailingRuntimeArrayMember(mlir::spirv::StructType Struct) {
  unsigned Last = Struct.getNumElements() - 1;
  if (mlir::isa<mlir::spirv::RuntimeArrayType>(Struct.getElementType(Last)))
    return Last;
  return std::nullopt;
}

/// Returns false if \p Struct's member \p Index -- the runtime array a
/// storage buffer block wraps, whether that is its sole member (see
/// BlockElement's `HasWrapper` case) or its trailing one (see
/// getTrailingRuntimeArrayMember) -- carries a `NonWritable` decoration,
/// i.e. the buffer is a `StructuredBuffer<T>` (SRV) rather than a
/// `RWStructuredBuffer<T>` (UAV) or a GLSL `readonly buffer`; true
/// otherwise.
bool isBufferBlockWritable(mlir::spirv::StructType Struct, unsigned Index) {
  llvm::SmallVector<mlir::spirv::StructType::MemberDecorationInfo, 1>
      Decorations;
  Struct.getMemberDecorations(Index, Decorations);
  for (const auto &Decoration : Decorations)
    if (Decoration.decoration == mlir::spirv::Decoration::NonWritable)
      return false;
  return true;
}

/// Returns \p Type's uniform buffer block content (the `cbuffer`/
/// `ConstantBuffer<T>` HLSL construct, or a GLSL `uniform` block), or
/// `std::nullopt` if \p Type is not a uniform buffer block pointer at all
/// -- see BlockElement's own comment for the shapes covered. A
/// `BufferBlock`-decorated struct is the pre-1.3 storage buffer spelling
/// getBufferBlockElement matches above, not a uniform block, even though
/// both use the `Uniform` storage class.
std::optional<BlockElement>
getUniformBlockElement(mlir::spirv::PointerType Type) {
  if (Type.getStorageClass() != mlir::spirv::StorageClass::Uniform)
    return std::nullopt;
  auto Struct = mlir::dyn_cast<mlir::spirv::StructType>(Type.getPointeeType());
  if (!Struct || Struct.getNumElements() == 0 ||
      Struct.hasDecoration(mlir::spirv::Decoration::BufferBlock))
    return std::nullopt;
  if (Struct.getNumElements() == 1) {
    mlir::Type Sole = Struct.getElementType(0);
    if (auto Field = mlir::dyn_cast<mlir::spirv::StructType>(Sole))
      return BlockElement{Field, /*HasWrapper=*/true};
    // A plain GLSL `uniform Input { uint data[16]; }` block (roadmap
    // F12a): dynamically-indexed exactly like a storage buffer's own
    // wrapped runtime array, so it is recognized the same way.
    if (auto Array = mlir::dyn_cast<mlir::spirv::ArrayType>(Sole))
      return BlockElement{Array, /*HasWrapper=*/true};
  }
  return BlockElement{Struct, /*HasWrapper=*/false};
}

/// Returns true if \p Type is a pointer to a uniform buffer block -- see
/// getUniformBlockElement.
bool isUniformBlockPointer(mlir::spirv::PointerType Type) {
  return static_cast<bool>(getUniformBlockElement(Type));
}

/// Returns the descriptor count of \p Type if it is an array-of-blocks
/// pointer -- `T blocks[N]` in GLSL, a single binding covering `N`
/// descriptors, each its own storage/uniform buffer block instance -- or
/// `std::nullopt` if it is not an array of blocks at all (an ordinary,
/// non-arrayed block, an array of some other resource kind, or not a
/// resource at all).
std::optional<uint32_t> getArrayedBlockCount(mlir::spirv::PointerType Type) {
  auto Array = mlir::dyn_cast<mlir::spirv::ArrayType>(Type.getPointeeType());
  if (!Array)
    return std::nullopt;
  auto ElementPointerType = mlir::spirv::PointerType::get(
      Array.getElementType(), Type.getStorageClass());
  if (!isBufferBlockPointer(ElementPointerType) &&
      !isUniformBlockPointer(ElementPointerType))
    return std::nullopt;
  return Array.getNumElements();
}

/// (Roadmap L12a) Returns the descriptor count of \p Type if it is an
/// array-of-resources pointer -- `RWBuffer<T> Buf[N]`/`Texture2D Tex[N]`/
/// a `SamplerState`-array's own SPIR-V shape, an image, sampled image, or
/// sampler array, as opposed to getArrayedBlockCount's array-of-*blocks*
/// (a storage/uniform buffer or cbuffer array, a structurally distinct
/// resource kind whose element is a memory-backed struct rather than an
/// opaque handle) -- or `std::nullopt` if \p Type is not an array of
/// resources at all. Two distinct SPIR-V shapes both count: a compile-time
/// `spirv.array` (`RWBuffer<T> Buf[3]`, a fixed descriptor count known
/// without any pipeline-creation-time information) returns its own real
/// element count; a `spirv.rtarray` (`RWBuffer<T> Buf[]`, an *unbounded*
/// array whose real, in-use descriptor count is a pipeline/descriptor-set-
/// layout-time property -- Vulkan's `VARIABLE_DESCRIPTOR_COUNT` binding
/// flag -- never encoded in the SPIR-V module itself) returns `0`, this
/// map's own reserved sentinel for "unbounded"; every caller that branches
/// on `ResourceInfo::Count` must treat `0` the same as any other multi-
/// descriptor count (i.e. `!= 1`, never `> 1`), since an unbounded array's
/// handle needs the exact same per-access-chain-index handle-from-binding
/// indirection a bounded one does, not the single, index-free handle a
/// non-arrayed resource's own address-of converts to directly.
std::optional<uint32_t>
getArrayedResourceCount(mlir::spirv::PointerType Type) {
  mlir::Type Pointee = Type.getPointeeType();
  mlir::Type ElementType;
  uint32_t Count;
  if (auto Array = mlir::dyn_cast<mlir::spirv::ArrayType>(Pointee)) {
    ElementType = Array.getElementType();
    Count = Array.getNumElements();
  } else if (auto RTArray =
                mlir::dyn_cast<mlir::spirv::RuntimeArrayType>(Pointee)) {
    ElementType = RTArray.getElementType();
    Count = 0;
  } else {
    return std::nullopt;
  }
  auto ElementPointerType =
      mlir::spirv::PointerType::get(ElementType, Type.getStorageClass());
  if (!isResourcePointer(ElementPointerType))
    return std::nullopt;
  return Count;
}


/// Emits a call to \p Intrinsic returning \p ResultType, with \p Args.
mlir::Value createIntrinsicCall(mlir::ConversionPatternRewriter &Rewriter,
                                mlir::Location Loc, llvm::StringRef Intrinsic,
                                mlir::Type ResultType, mlir::ValueRange Args) {
  return mlir::LLVM::CallIntrinsicOp::create(
             Rewriter, Loc, ResultType,
             mlir::StringAttr::get(Rewriter.getContext(), Intrinsic), Args)
      .getResults();
}

/// Converts `spirv.Switch` to `llvm.switch`, which MLIR has no pattern for
/// at all (see the "`spirv.Switch` op is not supported at the moment" note
/// in `mlir::populateSPIRVToLLVMConversionPatterns`'s structured-loop
/// pattern). The two ops match almost one-to-one: both are a selector value
/// compared against a set of case literals, each branching to its own
/// successor with its own successor operands, with a required default
/// successor for the selector matching none of them. `spirv-opt`'s
/// merge-return pass emits a case-less `spirv.Switch` (branching
/// unconditionally to its default successor) to skip the rest of a function
/// after an early return, which is otherwise indistinguishable from any
/// other switch here.
class SwitchConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::SwitchOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::SwitchOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::SwitchOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    // `literals`' element type is the selector's (pre-conversion) SPIR-V
    // type, which may be signed or unsigned; the LLVM op's verifier requires
    // its case values to have exactly the (post-conversion, always signless)
    // selector type, so the literals have to be rebuilt against it rather
    // than reused as-is.
    mlir::DenseIntElementsAttr CaseValues;
    if (mlir::DenseIntElementsAttr Literals = Op.getLiteralsAttr()) {
      llvm::SmallVector<llvm::APInt> Values(Literals.getValues<llvm::APInt>());
      auto CaseValueType = mlir::VectorType::get(
          static_cast<int64_t>(Values.size()), Adaptor.getSelector().getType());
      CaseValues = mlir::DenseIntElementsAttr::get(CaseValueType, Values);
    }

    llvm::SmallVector<mlir::ValueRange> TargetOperands =
        Adaptor.getTargetOperands();
    Rewriter.replaceOpWithNewOp<mlir::LLVM::SwitchOp>(
        Op, Adaptor.getSelector(), Op.getDefaultTarget(),
        Adaptor.getDefaultOperands(), CaseValues, Op.getTargets(),
        llvm::ArrayRef(TargetOperands));
    return mlir::success();
  }
};

/// Converts `spirv.DemoteToHelperInvocation` -- which, like `spirv.Switch`
/// above, MLIR has no pattern for at all (indeed no op at all, until this
/// same roadmap milestone added one) -- into a call to the
/// `llvm.spv.demote.to.helper.invocation` intrinsic.
/// `feme::graphics::CanonicalizeStagePass` later raises that intrinsic call
/// into `feme.stage.demote(true)`: the unconditional form, since SPIR-V's
/// `OpDemoteToHelperInvocation` -- unlike `feme.stage.demote`'s own
/// conditional HLSL `discard`-family origin -- always demotes
/// unconditionally when reached. This mirrors how `llvm.spv.discard`
/// (SPIR-V's `OpKill`) is handled the same way, except that op is a
/// terminator and this one, matching HLSL `discard`'s own non-terminating
/// semantics, is not: execution continues in the now-demoted invocation.
class DemoteToHelperInvocationConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::DemoteToHelperInvocationOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::DemoteToHelperInvocationOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::DemoteToHelperInvocationOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::LLVM::CallIntrinsicOp::create(
        Rewriter, Op.getLoc(),
        mlir::StringAttr::get(Rewriter.getContext(),
                              "llvm.spv.demote.to.helper.invocation"),
        mlir::ValueRange{});
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Converts `spirv.Kill` (roadmap H99) -- which, like `spirv.Switch` above,
/// MLIR has no pattern for at all -- into an unconditional discard-and-return:
/// a call to the `llvm.spv.discard` intrinsic (already raised into
/// `feme.stage.discard(true)` by `feme::graphics::CanonicalizeStagePass`),
/// followed by an `llvm.return`. This is the exact same lowering
/// `spirv.TerminateInvocation` below uses -- `OpKill`'s own SPIR-V spec
/// wording ("results in the invocation being terminated") is functionally
/// identical to `OpTerminateInvocation`'s -- but `OpKill` is the far more
/// common of the two in real shaders (it is what HLSL's unconditional
/// `discard` legalizes to, whereas `OpTerminateInvocation` requires the
/// dedicated `SPV_KHR_terminate_invocation` extension), so this op is
/// converted independently rather than folded into that one.
class KillConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::KillOp> {
public:
  using mlir::SPIRVToLLVMConversion<mlir::spirv::KillOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::KillOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::LLVM::CallIntrinsicOp::create(
        Rewriter, Op.getLoc(),
        mlir::StringAttr::get(Rewriter.getContext(), "llvm.spv.discard"),
        mlir::ValueRange{});
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ReturnOp>(Op, mlir::ValueRange{});
    return mlir::success();
  }
};

/// Converts `spirv.TerminateInvocation` (roadmap E12,
/// VK_KHR_shader_terminate_invocation) -- which, like `spirv.Switch` above,
/// MLIR has no pattern for at all (indeed no op at all, until this same
/// roadmap milestone added one) -- into an unconditional discard-and-return:
/// a call to the same `llvm.spv.discard` intrinsic `OpKill` itself would use
/// (already raised into `feme.stage.discard(true)` by
/// `feme::graphics::CanonicalizeStagePass`, unmodified by this milestone),
/// followed by an `llvm.return`. Unlike `spirv.DemoteToHelperInvocation`,
/// this op is a true terminator -- SPIR-V requires it be the last
/// instruction in its block and no further instructions of the invocation
/// execute -- so, unlike that op's conversion, this one has to replace the
/// terminator itself rather than simply erase the (non-terminator) op in
/// place.
class TerminateInvocationConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::TerminateInvocationOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::TerminateInvocationOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::TerminateInvocationOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::LLVM::CallIntrinsicOp::create(
        Rewriter, Op.getLoc(),
        mlir::StringAttr::get(Rewriter.getContext(), "llvm.spv.discard"),
        mlir::ValueRange{});
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ReturnOp>(Op, mlir::ValueRange{});
    return mlir::success();
  }
};

/// Converts `spirv.ControlBarrier` -- a real SPIR-V import's own
/// `OpControlBarrier` (as opposed to the `llvm.{dx,spv}.*_memory_barrier
/// [_with_group_sync]` intrinsic shape a DXIL/HLSL `GroupMemoryBarrier
/// WithGroupSync()`-family call already produces, via `feme::dxil::
/// OpRaisingPass::raiseBarrierCall`/its SPIR-V counterpart, long before
/// this pattern ever runs) -- directly into one of the three
/// `llvm.spv.*_memory_barrier_with_group_sync` intrinsics
/// `feme::cpu::matchBarrierCall` (`feme/lib/Transforms/CPU/
/// BarrierCalls.cpp`) already recognizes, rather than letting it fall
/// through to upstream's own default `ControlBarrierPattern`
/// (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`), which lowers it to
/// a call to a mangled external declaration
/// (`_Z22__spirv_ControlBarrieriii`, `CallingConv::SPIR_FUNC`) with no
/// runtime-provided definition and no lowering for that calling
/// convention on this project's CPU target at all -- an unresolved
/// external call that survives, unmodified, all the way to X86 JIT
/// codegen, which aborts the whole process with a fatal
/// `"unsupported calling convention"` error (roadmap L44, root-caused via
/// a real captured pre-`LinearizePass` IR dump of a real
/// `dEQP-VK.mesh_shader.ext.query.no_queries.*.mesh_only.*` CTS case).
///
/// `OpControlBarrier` always requires every invocation within its own
/// `execution_scope` to reach this point before any proceeds (per the
/// SPIR-V spec: "All invocations of this module within Execution scope
/// must reach this point of execution before any invocation will proceed
/// beyond it") -- unconditionally true regardless of `memory_semantics`,
/// unlike `spirv.MemoryBarrier`, which never implies convergence -- so
/// this always picks one of the three `_with_group_sync` (convergence
/// *and* fence) intrinsics, never a plain memory-only one. Which of the
/// three is chosen is decided by `memory_scope` alone (`Workgroup` maps to
/// the narrowest, `group`, intrinsic; every broader scope -- `Device`,
/// `CrossDevice`, `QueueFamily`, and the sub-group-shaped `Subgroup`/
/// `Invocation`, none of which this milestone's whole-group barrier
/// support distinguishes further -- conservatively maps to the widest,
/// `all`, intrinsic, a safe superset fence in every case): `memory_
/// semantics`'s own individual ordering/memory-class bits are not parsed
/// further, mirroring roadmap H4b's own `isSPIRVGroupSyncBarrier`, which
/// already treats every control barrier as the one splitting point it
/// cares about "regardless of its own execution/memory scope operands".
/// If `memory_semantics` is `None` (per spec, "Memory is ignored" in that
/// case -- a convergence-only barrier with no memory-ordering
/// requirement at all), the narrowest `group` intrinsic is still emitted:
/// harmless, since a fence stronger than the (here, absent) requirement
/// is never a correctness problem, only unneeded work no real CTS case
/// exercises today.
class ControlBarrierConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ControlBarrierOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ControlBarrierOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ControlBarrierOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    llvm::StringRef Intrinsic =
        Op.getMemoryScope() == mlir::spirv::Scope::Workgroup
            ? "llvm.spv.group.memory.barrier.with.group.sync"
            : "llvm.spv.all.memory.barrier.with.group.sync";
    mlir::LLVM::CallIntrinsicOp::create(
        Rewriter, Op.getLoc(),
        mlir::StringAttr::get(Rewriter.getContext(), Intrinsic),
        mlir::ValueRange{});
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Converts `spirv.MemoryBarrier` (roadmap L7l) -- a real SPIR-V import's
/// own `OpMemoryBarrier`, e.g. as glslang emits for a GLSL
/// `subgroupMemoryBarrier()`-family call -- into one of the three plain
/// (non-`_with_group_sync`) `llvm.spv.*_memory_barrier` intrinsics
/// `feme::cpu::matchBarrierCall` already recognizes, mirroring
/// `ControlBarrierConversionPattern` above but for the memory-only-fence
/// shape rather than the group-sync-plus-fence one: unlike
/// `spirv.ControlBarrier`, `spirv.MemoryBarrier` never implies any
/// convergence requirement (no `execution_scope` operand exists at all),
/// so this always picks a plain barrier intrinsic, never a `_with_group_
/// sync` one.
///
/// Which of the three plain intrinsics is chosen is decided by
/// `memory_scope` alone, mapping every one of the six SPIR-V scopes onto
/// the three `feme::cpu::BarrierMemoryScope` granularities the CPU
/// runtime's own barrier-region-splitting/fence lowering
/// (`feme/lib/Transforms/CPU/EntryWrapper.cpp`) already implements (it
/// already consumes all six raised intrinsics uniformly, regardless of
/// whether a DXIL or SPIR-V frontend produced them, so no further CPU-
/// runtime change is needed here): `Workgroup` maps to the narrowest,
/// `group`; `Device` maps to `device`; every broader or narrower scope
/// this milestone's whole-group barrier support doesn't distinguish
/// further (`CrossDevice`, `QueueFamily`, and the sub-group-shaped
/// `Subgroup`/`Invocation`) conservatively maps to the widest, `all`, a
/// safe superset fence in every case -- mirroring
/// `ControlBarrierConversionPattern`'s own conservative-superset
/// convention for the scopes it doesn't distinguish further either.
/// `memory_semantics`'s own individual ordering/memory-class bits are not
/// parsed further, for the same reason `ControlBarrierConversionPattern`
/// doesn't: this milestone's whole-group barrier support only
/// distinguishes barriers by scope, not by which memory classes they
/// order.
class MemoryBarrierConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::MemoryBarrierOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::MemoryBarrierOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::MemoryBarrierOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    llvm::StringRef Intrinsic;
    switch (Op.getMemoryScope()) {
    case mlir::spirv::Scope::Workgroup:
      Intrinsic = "llvm.spv.group.memory.barrier";
      break;
    case mlir::spirv::Scope::Device:
      Intrinsic = "llvm.spv.device.memory.barrier";
      break;
    default:
      Intrinsic = "llvm.spv.all.memory.barrier";
      break;
    }
    mlir::LLVM::CallIntrinsicOp::create(
        Rewriter, Op.getLoc(),
        mlir::StringAttr::get(Rewriter.getContext(), Intrinsic),
        mlir::ValueRange{});
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Converts `spirv.KHR.AssumeTrue` (roadmap F4, `VK_KHR_shader_expect_assume`
/// / `shaderExpectAssume`) directly into the `llvm.assume` intrinsic: both
/// take a single `i1` condition and produce no result, an exact match
/// needing no expansion at all.
class AssumeTrueConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::KHRAssumeTrueOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::KHRAssumeTrueOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::KHRAssumeTrueOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    Rewriter.replaceOpWithNewOp<mlir::LLVM::AssumeOp>(Op,
                                                      Adaptor.getCondition());
    return mlir::success();
  }
};

/// Converts `spirv.KHR.Expect` (roadmap F4, same extension as
/// `spirv.KHR.AssumeTrue` above) into the `llvm.expect` intrinsic. Unlike
/// `AssumeTrue`'s condition, this op's operand may be a vector of
/// integer/bool, not just a scalar -- but LLVM's `llvm.expect` intrinsic is
/// only defined for scalar integers (see "`llvm.expect`" in LangRef.md), so
/// a vector operand is expanded into one `llvm.expect` call per lane,
/// mirroring `DotConversionPattern`'s own per-lane vector expansion above.
class ExpectConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::KHRExpectOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::KHRExpectOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::KHRExpectOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::Value Value = Adaptor.getValue();
    mlir::Value ExpectedValue = Adaptor.getExpectedValue();

    auto VectorTy = mlir::dyn_cast<mlir::VectorType>(Value.getType());
    if (!VectorTy) {
      Rewriter.replaceOpWithNewOp<mlir::LLVM::ExpectOp>(Op, Value,
                                                        ExpectedValue);
      return mlir::success();
    }

    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, VectorTy);
    for (int64_t I = 0, E = VectorTy.getNumElements(); I != E; ++I) {
      mlir::Value Index = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI64Type(), Rewriter.getI64IntegerAttr(I));
      mlir::Value Lane = mlir::LLVM::ExpectOp::create(
          Rewriter, Loc,
          mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Value, Index),
          mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, ExpectedValue,
                                               Index));
      Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result, Lane,
                                                   Index);
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformRotateKHR` (roadmap F2,
/// `VK_KHR_shader_subgroup_rotate`) -- the first `spirv.GroupNonUniform*` op
/// this pass converts at all (`Vulkan14FeatureInventory.md` previously found
/// none did, making `shaderSubgroupExtendedTypes` vacuously true for the same
/// reason) -- into the invocation-id arithmetic the SPIR-V spec itself
/// defines for this op, followed by an `llvm.spv.wave.readlane` shuffle to
/// that invocation (which already lowers to `OpGroupNonUniformShuffle`).
/// LLVM's SPIR-V backend has no generic `llvm.spv.*` intrinsic for the
/// rotate operation itself (unlike `wave.readlane`'s direct mapping); its
/// only path to `OpGroupNonUniformRotateKHR` is through OpenCL C's
/// `sub_group[_clustered]_rotate` builtin *function calls*, a mechanism no
/// other pattern in this file uses (every one calls an `llvm.spv.*`
/// intrinsic directly, never an external function), so this expands the op
/// into the equivalent shuffle instead, mirroring how `DotConversionPattern`
/// below expands `spirv.Dot` into equivalent IR rather than relying on a
/// call. Only `Subgroup` execution scope is implemented: `Workgroup`-scope
/// rotate has no real HLSL/GLSL source in this ICD's frontend surface
/// (`subgroupRotate`/`WaveRotate*`-family intrinsics are subgroup-only) and
/// would need a different, shared-memory-based lowering this pattern does
/// not provide.
class RotateConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformRotateKHROp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformRotateKHROp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformRotateKHROp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope rotate is not supported");

    mlir::Type I32 = Rewriter.getI32Type();
    if (Adaptor.getDelta().getType() != I32 ||
        (Adaptor.getClusterSize() &&
         Adaptor.getClusterSize().getType() != I32))
      return Rewriter.notifyMatchFailure(
          Op, "delta/cluster_size must be 32-bit (as every known producer "
              "of this op emits)");

    mlir::Location Loc = Op.getLoc();
    // `LocalId`/`RotationGroupSize` and the rest of this arithmetic follow
    // the SPIR-V spec's own definition of `OpGroupNonUniformRotateKHR`
    // verbatim (see the op's own summary in SPIRVNonUniformOps.td):
    //   RotationGroupSize = ClusterSize, if present, else SubgroupSize
    //   InvocationId = ((LocalId + Delta) & (RotationGroupSize - 1)) +
    //                  (LocalId & ~(RotationGroupSize - 1))
    mlir::Value LocalId = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.subgroup.local.invocation.id", I32, {});
    mlir::Value GroupSize =
        Adaptor.getClusterSize()
            ? Adaptor.getClusterSize()
            : createIntrinsicCall(Rewriter, Loc, "llvm.spv.subgroup.size",
                                  I32, {});
    mlir::Value One =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, 1);
    mlir::Value Mask =
        mlir::LLVM::SubOp::create(Rewriter, Loc, GroupSize, One);
    mlir::Value AllOnes =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, -1);
    mlir::Value NotMask =
        mlir::LLVM::XOrOp::create(Rewriter, Loc, Mask, AllOnes);
    mlir::Value Rotated =
        mlir::LLVM::AddOp::create(Rewriter, Loc, LocalId, Adaptor.getDelta());
    mlir::Value RotatedMasked =
        mlir::LLVM::AndOp::create(Rewriter, Loc, Rotated, Mask);
    mlir::Value BaseMasked =
        mlir::LLVM::AndOp::create(Rewriter, Loc, LocalId, NotMask);
    mlir::Value InvocationId = mlir::LLVM::AddOp::create(
        Rewriter, Loc, RotatedMasked, BaseMasked);

    mlir::Type ResultType =
        getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.wave.readlane",
                                ResultType,
                                {Adaptor.getValue(), InvocationId}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformElect` (roadmap L7e) directly to
/// `llvm.spv.wave.is.first.lane`: both are defined identically ("true only
/// in the invocation with the lowest id active in the group"), and this
/// project's DXIL-origin frontend already lowers HLSL's `WaveIsFirstLane()`
/// to exactly this same intrinsic (`feme/lib/Transforms/DXIL/OpRaising.cpp`),
/// which `feme::cpu::WaveUniformity`/`SIMDizePass` already fully support --
/// this pattern simply gives the SPIR-V-origin frontend (glslang/dxc's own
/// `OpGroupNonUniformElect` encoding of the same HLSL builtin) the identical
/// entry point, rather than needing its own separate CPU-side lowering.
/// Only `Subgroup` execution scope is implemented, mirroring
/// `RotateConversionPattern` above: `Workgroup`-scope elect has no real
/// HLSL/GLSL source in this ICD's frontend surface today (`WaveIsFirstLane`
/// is subgroup-only).
class ElectConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GroupNonUniformElectOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformElectOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformElectOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope elect is not supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Op.getLoc(),
                                "llvm.spv.wave.is.first.lane", ResultType,
                                {}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformAll`/`GroupNonUniformAny` (roadmap L7i,
/// split out of L7e's own closing session) directly to
/// `llvm.spv.wave.all`/`llvm.spv.wave.any`: both pairs are defined
/// identically ("true if Predicate is true for all/any active
/// invocations"), operating on a plain scalar `i1` (per the dialect's own
/// `SPIRV_Bool:$predicate`/`SPIRV_Bool:$result` -- unlike
/// `GroupNonUniformAllEqualOp`, neither op has a vector-operand shape to
/// worry about at all), and `spirv.GroupNonUniformAll`/`AnyOp` are already
/// constrained to `Subgroup` scope by their own `SPIRV_ExecutionScopeAttrIs`
/// (see `SPIRVNonUniformOps.td`), matching `llvm.spv.wave.all`/`any`'s own
/// only supported scope, so no scope check is needed here (mirroring
/// `AllEqualConversionPattern` below's own reasoning for the same dialect
/// constraint). HLSL's `WaveActiveAllTrue`/`AnyTrue` already lower to these
/// same intrinsics from the DXIL-origin frontend
/// (`feme/lib/Transforms/DXIL/OpRaising.cpp`), which
/// `feme::cpu::WaveUniformity`/`SIMDizePass` already fully support -- like
/// `Elect`/`AllEqual`/`Shuffle` before it, this needed no new CPU-side
/// codegen surface at all.
template <typename GroupOp>
constexpr llvm::StringLiteral getVoteIntrinsicName();
template <>
constexpr llvm::StringLiteral
getVoteIntrinsicName<mlir::spirv::GroupNonUniformAllOp>() {
  return "llvm.spv.wave.all";
}
template <>
constexpr llvm::StringLiteral
getVoteIntrinsicName<mlir::spirv::GroupNonUniformAnyOp>() {
  return "llvm.spv.wave.any";
}

template <typename GroupOp>
class VoteConversionPattern : public mlir::SPIRVToLLVMConversion<GroupOp> {
public:
  using mlir::SPIRVToLLVMConversion<GroupOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(GroupOp Op, typename GroupOp::Adaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType =
        this->getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Op.getLoc(),
                                getVoteIntrinsicName<GroupOp>(), ResultType,
                                {Adaptor.getPredicate()}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformAllEqual` (roadmap L7e/L7i) directly to
/// `llvm.spv.wave.all.equal`: both take the identical operand ("true if
/// Value is equal for all active invocations"), and
/// `spirv.GroupNonUniformAllEqualOp` is already constrained to `Subgroup`
/// scope by its own `SPIRV_ExecutionScopeAttrIs` (see
/// `SPIRVNonUniformOps.td`), matching `llvm.spv.wave.all.equal`'s own only
/// supported scope, so unlike `ElectConversionPattern`/
/// `RotateConversionPattern` above there is no narrower scope to check here.
/// HLSL's `WaveActiveAllEqual` already lowers to this same intrinsic from
/// the DXIL-origin frontend (`feme/lib/Transforms/DXIL/OpRaising.cpp`),
/// which `feme::cpu::WaveUniformity`/`SIMDizePass` already fully support.
///
/// A *vector* `Value` operand needs one extra step beyond a straight
/// forward: unlike `llvm.spv.wave.all.equal`'s own vector shape (a
/// per-component `<W x i1>` result, matching HLSL's own `WaveActiveAllEqual
/// (bool2/bool3/bool4)` semantics, which the DXIL-origin frontend already
/// relies on -- see `OpRaising.cpp`'s own "overloaded on the operand, not
/// the i1 result" note), `spirv.GroupNonUniformAllEqualOp`'s result is
/// always a single scalar `SPIRV_Bool` even when `Value` is a vector (per
/// the SPIR-V spec's own "Result Type must be a Boolean type" text, mirrored
/// verbatim by the dialect's `results = (outs SPIRV_Bool:$result)`),
/// collapsing the whole vector into one true/false. Per the SPIR-V spec's
/// own wording ("the result is true if Value is equal for all ... "), a
/// vector `Value` is compared as a whole -- equivalent to *every* component
/// being equal across every active invocation, i.e. exactly the AND of
/// `llvm.spv.wave.all.equal`'s own per-component result -- so this pattern
/// calls the intrinsic to get that per-component `<W x i1>` first, then
/// folds it down with `llvm.vector.reduce.and` to produce the single
/// scalar `i1` this op's result type actually requires. (roadmap L7i,
/// closing the vector-operand gap `AllEqualConversionPattern` originally
/// declined in L7e: no known dxc-compiled shape needs it -- dxc's own
/// SPIR-V backend scalarizes a vector `WaveActiveAllEqual` into one call
/// per component instead -- but `dEQP-VK.subgroups.vote.*`'s own
/// `bvec2`-`bvec4`/`vec8` type matrix genuinely does, via glslang's
/// `subgroupAllEqual`.)
class AllEqualConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformAllEqualOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformAllEqualOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformAllEqualOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Operand = Adaptor.getValue();
    if (auto VecTy = mlir::dyn_cast<mlir::VectorType>(Operand.getType())) {
      mlir::Type ComponentResultType =
          mlir::VectorType::get(VecTy.getShape(), Rewriter.getI1Type());
      mlir::Value ComponentEqual = createIntrinsicCall(
          Rewriter, Loc, "llvm.spv.wave.all.equal", ComponentResultType,
          {Operand});
      Rewriter.replaceOpWithNewOp<mlir::LLVM::vector_reduce_and>(
          Op, ResultType, ComponentEqual);
      return mlir::success();
    }

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.wave.all.equal",
                                ResultType, {Operand}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformShuffle` (roadmap L7e) directly to
/// `llvm.spv.wave.readlane`: both are defined identically ("Result is the
/// Value of the invocation identified by the id Id"), an exact match for
/// this intrinsic's own semantics already relied on by
/// `RotateConversionPattern` above (which builds its own target invocation
/// id then hands it to this same intrinsic). HLSL's `WaveReadLaneAt`
/// already lowers to this same intrinsic from the DXIL-origin frontend, and
/// unlike `dx_wave_readlane` (whose lane-index operand HLSL's language rule
/// requires to be dynamically uniform, letting `feme::cpu::WaveUniformity`
/// classify a `dx_wave_readlane` call's own result as uniform whenever its
/// value operand is), `spv_wave_readlane`'s result is conservatively always
/// treated as divergent (see `WaveUniformity.cpp`'s own note on this),
/// matching `OpGroupNonUniformShuffle`'s SPIR-V spec text, which -- unlike
/// `WaveReadLaneAt`'s HLSL-level uniformity requirement on its index
/// argument -- places no such restriction on Id at all. Only `Subgroup`
/// execution scope is implemented, mirroring `RotateConversionPattern`/
/// `ElectConversionPattern` above.
class ShuffleConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GroupNonUniformShuffleOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformShuffleOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformShuffleOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope shuffle is not supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Op.getLoc(),
                                "llvm.spv.wave.readlane", ResultType,
                                {Adaptor.getValue(), Adaptor.getId()}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformShuffleXor` (roadmap L7i, split out of
/// L7e's own closing session) into the equivalent target-invocation-id
/// arithmetic the SPIR-V spec itself defines for this op ("the current
/// invocation's id within the group xor'ed with Mask") followed by an
/// `llvm.spv.wave.readlane` shuffle to that invocation -- the identical
/// "compute an id, then shuffle to it" shape `RotateConversionPattern`
/// (roadmap F2) already established, just with XOR instead of that
/// pattern's own masked rotate arithmetic. Unlike
/// `GroupNonUniformShuffleUp`/`DownOp` (a separate,
/// `GroupNonUniformShuffleRelative`-gated capability this pattern
/// deliberately does not touch -- see this row's own roadmap text),
/// `GroupNonUniformShuffleXorOp` shares `ShuffleOp`'s own
/// `GroupNonUniformShuffle` capability, so closing it (alongside plain
/// `Shuffle`) is what actually completes `VK_SUBGROUP_FEATURE_SHUFFLE_BIT`'s
/// own backing op set. Only `Subgroup` execution scope is implemented,
/// mirroring `ShuffleConversionPattern`/`ElectConversionPattern` above.
class ShuffleXorConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformShuffleXorOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformShuffleXorOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformShuffleXorOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope shuffle-xor is not supported");

    mlir::Type I32 = Rewriter.getI32Type();
    if (Adaptor.getMask().getType() != I32)
      return Rewriter.notifyMatchFailure(
          Op, "mask must be 32-bit (as every known producer of this op "
              "emits)");

    mlir::Location Loc = Op.getLoc();
    mlir::Value LocalId = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.subgroup.local.invocation.id", I32, {});
    mlir::Value TargetId =
        mlir::LLVM::XOrOp::create(Rewriter, Loc, LocalId, Adaptor.getMask());

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.wave.readlane",
                                ResultType, {Adaptor.getValue(), TargetId}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBroadcast` (roadmap L89e) directly to
/// `llvm.spv.wave.readlane`, exactly as `ShuffleConversionPattern` above
/// does: "Result is the Value of the invocation identified by the id Id"
/// is the same sentence both ops' spec text uses, and this intrinsic
/// already implements it for every scalar, vector and `i1` operand shape
/// the `dEQP-VK.subgroups.ballot_broadcast` group exercises.
///
/// The only difference from `Shuffle` is a *restriction*: `Broadcast`
/// additionally requires Id to be dynamically uniform (a constant before
/// SPIR-V 1.5). Nothing needs to be done with that guarantee here --
/// `lowerReadLane`'s per-lane gather computes the right answer for a
/// uniform index as the special case where every lane happens to read the
/// same source lane -- so honouring it would only be an optimization, and
/// deliberately is not one this pattern tries to make: `spv_wave_readlane`
/// is conservatively treated as divergent by `WaveUniformity.cpp` (see
/// `ShuffleConversionPattern`'s own note), and narrowing that for this op
/// alone would need its own uniformity evidence rather than a promise the
/// SPIR-V producer made.
class BroadcastConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformBroadcastOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBroadcastOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBroadcastOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope broadcast is not supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op,
        createIntrinsicCall(Rewriter, Op.getLoc(), "llvm.spv.wave.readlane",
                            ResultType, {Adaptor.getValue(), Adaptor.getId()}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBallot` (roadmap L85) directly to
/// `llvm.spv.subgroup.ballot`: both are defined identically ("a bitfield
/// value combining the Predicate value from all invocations in the
/// group"), an exact match for `int_spv_subgroup_ballot`'s own shape
/// (`i1` predicate -> `<4 x i32>` bitmask, see `llvm/include/llvm/IR/
/// IntrinsicsSPIRV.td`). Unlike `Elect`/`Vote`/`Shuffle`'s existing
/// intrinsics (all already fully wired through
/// `feme::cpu::SIMDizePass`/`WaveLowering.cpp` for the DXIL-origin
/// `WaveIsFirstLane`/`WaveActiveAllTrue`/`WaveReadLaneAt` equivalents),
/// this SPIR-V-origin ballot needs `SIMDize.cpp`'s own
/// `classifyWaveCall` extended with a new `Intrinsic::spv_subgroup_ballot`
/// case (see that file), reusing the exact same
/// `WaveCallKind::Ballot`/`lowerBallot` DXIL-origin machinery
/// `WaveActiveBallot` already relies on: both encode the identical
/// 128-bit-wide bitmask, just packaged differently at the two frontends'
/// own ABI boundary (DXIL's `{i32,i32,i32,i32}` struct vs. SPIR-V's
/// `<4 x i32>` vector), a difference `SIMDize.cpp`'s own widening step now
/// bridges (see `widenWaveCall`'s `Ballot`-specific repackaging there).
/// Only `Subgroup` execution scope is implemented, mirroring
/// `ElectConversionPattern`/`ShuffleConversionPattern` above: no known
/// dxc/glslang-compiled shape needs `Workgroup`-scope ballot.
class BallotConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GroupNonUniformBallotOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBallotOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBallotOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope ballot is not supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Op.getLoc(),
                                "llvm.spv.subgroup.ballot", ResultType,
                                {Adaptor.getPredicate()}));
    return mlir::success();
  }
};

/// Bitcasts a `<4 x i32>` ballot bitmask (every `spirv.GroupNonUniform
/// Ballot*` consumer's own fixed operand shape, `SPIRV_IOrUIVec4`) to a
/// single `i128` integer, the natural shape for the uniform
/// bit-manipulation `BallotFindLSBConversionPattern`/
/// `BallotFindMSBConversionPattern`/`BallotBitCountConversionPattern`
/// below need (`llvm.cttz`/`ctlz`/`ctpop`, shifts, masks): word 0 (the
/// lowest four bytes, containing the first 32 invocations' bits per the
/// SPIR-V spec's own "first invocation is represented in the lowest bit of
/// the first vector component" text) becomes the integer's own low 32
/// bits, matching a little-endian `bitcast` of the vector's component
/// order -- exactly the layout `feme::cpu::WaveLowering.cpp`'s own
/// `lowerBallot`/`lowerActiveCountBits` already rely on for the
/// DXIL-origin ballot ABI's identical shape. Safe to use here specifically
/// *because* a ballot bitmask's own value is always uniform across the
/// group by construction (it is the result of a group-wide reduction), so
/// this single 128-bit-wide operation never needs the per-lane widening
/// `extractBallotBit` below's dynamic (divergent-index) lookup requires.
mlir::Value ballotVectorToI128(mlir::ConversionPatternRewriter &Rewriter,
                               mlir::Location Loc, mlir::Value Vector) {
  mlir::Type I128 = Rewriter.getIntegerType(128);
  return mlir::LLVM::BitcastOp::create(Rewriter, Loc, I128, Vector);
}

/// Masks \p Bits (a `ballotVectorToI128`-shaped 128-bit ballot bitmask)
/// down to only the low `llvm.spv.subgroup.size` bits: `(1 << SubgroupSize)
/// - 1`, clamped to "every bit" once `SubgroupSize` reaches the full
/// 128-bit width (mirroring `BallotBitCountConversionPattern`'s own
/// `>= 128`-guarded `select`, for the identical reason -- a plain
/// `1 << 128` would otherwise be a poison out-of-range shift). Needed
/// because the SPIR-V spec defines `OpGroupNonUniformBallotFindLSB`/
/// `FindMSB`/`BallotBitCount`'s `Reduce` variant to only consider the
/// bits actually representing invocations in the group -- any set bit at
/// or beyond the group's own size is not part of the group and must not
/// affect the result -- confirmed necessary by a real `deqp-vk`
/// `dEQP-VK.subgroups.ballot_other.compute.subgroupballotfindlsb`-shaped
/// failure this row's own verification pass hit: `vktSubgroupsBallotOther
/// Tests.cpp`'s own test source deliberately passes a hand-built `uvec4`
/// with high bits set beyond `gl_SubgroupSize` (`allOnes`/`MAKE_HIGH_
/// BALLOT_RESULT`) and still expects a result clipped to the actual
/// subgroup size, not the full 128-bit vector width. `BallotBitCount`'s
/// own `InclusiveScan`/`ExclusiveScan` variants need no equivalent call
/// to this helper: their own per-invocation prefix mask is already always
/// bounded by the invocation's own id, itself always less than
/// `SubgroupSize`, so high garbage bits beyond `SubgroupSize` are already
/// excluded by that narrower mask on their own.
mlir::Value clipBallotBitsToSubgroupSize(
    mlir::ConversionPatternRewriter &Rewriter, mlir::Location Loc,
    mlir::Value Bits) {
  mlir::Type I128 = Rewriter.getIntegerType(128);
  mlir::Value SubgroupSize = createIntrinsicCall(
      Rewriter, Loc, "llvm.spv.subgroup.size", Rewriter.getI32Type(), {});
  mlir::Value SubgroupSize128 =
      mlir::LLVM::ZExtOp::create(Rewriter, Loc, I128, SubgroupSize);
  mlir::Value One = mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128, 1);
  mlir::Value AllOnes = mlir::LLVM::ConstantOp::create(
      Rewriter, Loc, I128, llvm::APInt::getAllOnes(128));
  mlir::Value Shifted =
      mlir::LLVM::ShlOp::create(Rewriter, Loc, One, SubgroupSize128);
  mlir::Value LowMask =
      mlir::LLVM::SubOp::create(Rewriter, Loc, Shifted, One);
  mlir::Value FullWidth =
      mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128, 128);
  mlir::Value IsFull = mlir::LLVM::ICmpOp::create(
      Rewriter, Loc, mlir::LLVM::ICmpPredicate::uge, SubgroupSize128,
      FullWidth);
  mlir::Value Mask =
      mlir::LLVM::SelectOp::create(Rewriter, Loc, IsFull, AllOnes, LowMask);
  return mlir::LLVM::AndOp::create(Rewriter, Loc, Bits, Mask);
}

/// Builds one of the five subgroup mask builtin variables' `<4 x i32>`
/// values (roadmap L89g) from the current invocation's own id.
///
/// Each is defined by the SPIR-V spec against the same 128-bit ballot
/// bitmask layout `ballotVectorToI128` documents, so all five are computed
/// as `i128` arithmetic over `llvm.spv.subgroup.local.invocation.id` and
/// bitcast back to the `uvec4` the ballot ABI spells them as:
///
///   EqMask = 1 << id           GeMask = ~LtMask
///   LtMask = (1 << id) - 1     GtMask = ~LeMask
///   LeMask = (1 << (id + 1)) - 1
///
/// `1 << id` is always in range (`id` is always less than `SubgroupSize`,
/// itself at most 128), so none of these needs
/// `clipBallotBitsToSubgroupSize`'s own out-of-range-shift guard on the
/// shift itself. `Le`/`Gt` are deliberately built from `EqMask << 1`
/// rather than the equivalent-looking `1 << (id + 1)`, which would be an
/// out-of-range shift for the largest valid `id` (127, in a full 128-wide
/// subgroup); shifting the already-computed bit instead simply drops it
/// off the top, leaving the all-ones prefix that case wants. The two
/// complemented masks do need that helper's clip on their *result*, though: the
/// spec defines every bit at or beyond `SubgroupSize` as zero, and
/// complementing a prefix mask sets all of them. The three prefix masks are
/// already bounded by `id + 1 <= SubgroupSize` and so need no clip, exactly as
/// `clipBallotBitsToSubgroupSize`'s own comment says of `BallotBitCount`'s
/// scan variants. `vktSubgroupsBuiltinMaskVarTests.cpp` checks this
/// directly by requiring a plain 128-bit `bitCount` of the mask to equal
/// `subgroupBallotBitCount`, which only counts bits below `SubgroupSize`.
mlir::Value buildSubgroupMask(mlir::ConversionPatternRewriter &Rewriter,
                              mlir::Location Loc, SubgroupMaskKind Kind,
                              mlir::Type ResultType) {
  mlir::Type I128 = Rewriter.getIntegerType(128);
  mlir::Value Id = createIntrinsicCall(Rewriter, Loc,
                                       "llvm.spv.subgroup.local.invocation.id",
                                       Rewriter.getI32Type(), {});
  mlir::Value Id128 = mlir::LLVM::ZExtOp::create(Rewriter, Loc, I128, Id);
  mlir::Value One = mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128, 1);

  // `Eq` is the single bit; `Lt`/`Le` are the prefixes below and through it.
  mlir::Value Eq = mlir::LLVM::ShlOp::create(Rewriter, Loc, One, Id128);
  mlir::Value Bits;
  switch (Kind) {
  case SubgroupMaskKind::Eq:
    Bits = Eq;
    break;
  case SubgroupMaskKind::Lt:
    Bits = mlir::LLVM::SubOp::create(Rewriter, Loc, Eq, One);
    break;
  case SubgroupMaskKind::Le:
    Bits = mlir::LLVM::SubOp::create(
        Rewriter, Loc, mlir::LLVM::ShlOp::create(Rewriter, Loc, Eq, One), One);
    break;
  case SubgroupMaskKind::Ge:
    Bits = clipBallotBitsToSubgroupSize(
        Rewriter, Loc,
        mlir::LLVM::XOrOp::create(
            Rewriter, Loc, mlir::LLVM::SubOp::create(Rewriter, Loc, Eq, One),
            mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128,
                                           llvm::APInt::getAllOnes(128))));
    break;
  case SubgroupMaskKind::Gt:
    Bits = clipBallotBitsToSubgroupSize(
        Rewriter, Loc,
        mlir::LLVM::XOrOp::create(
            Rewriter, Loc,
            mlir::LLVM::SubOp::create(
                Rewriter, Loc,
                mlir::LLVM::ShlOp::create(Rewriter, Loc, Eq, One), One),
            mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128,
                                           llvm::APInt::getAllOnes(128))));
    break;
  }
  return mlir::LLVM::BitcastOp::create(Rewriter, Loc, ResultType, Bits);
}

/// Extracts bit \p Index (an `i32`, not required to be a compile-time
/// constant, and not required to be uniform across the group -- see
/// `InverseBallotConversionPattern`'s own "current invocation" index and
/// `BallotBitExtractConversionPattern`'s own explicit `Index` operand,
/// both genuinely per-invocation-varying values) from a `<4 x i32>` ballot
/// bitmask \p Value as an `i1`: `(Value[Index / 32] >> (Index % 32)) & 1`.
/// Deliberately an ordinary dynamic `llvm.extractelement` lookup into the
/// fixed four-word vector, rather than `ballotVectorToI128`'s own single
/// 128-bit-wide shift (which only ever needs a uniform shift amount, see
/// its own comment): a per-invocation `Index` needs the dynamic word
/// selection `llvm.extractelement` already handles generically for a
/// divergent index (`feme::cpu::FunctionWidener::widenExtractElement`,
/// the same generic widening `ShuffleXorConversionPattern`'s own
/// divergent-index shuffle above already relies on transitively through
/// `llvm.spv.wave.readlane`), letting the ordinary SIMDize divergence
/// analysis -- not a bespoke wave-op lowering -- carry this arithmetic
/// per-lane once `Index` is genuinely divergent (as `gl_SubgroupInvocation
/// ID`, the only known source of `InverseBallot`'s own "current
/// invocation" concept and a common `BallotBitExtract` index operand, is).
mlir::Value extractBallotBit(mlir::ConversionPatternRewriter &Rewriter,
                             mlir::Location Loc, mlir::Value Value,
                             mlir::Value Index) {
  mlir::Type I32 = Rewriter.getI32Type();
  mlir::Value Five = mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, 5);
  mlir::Value ThirtyOne =
      mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, 31);
  mlir::Value WordIndex =
      mlir::LLVM::LShrOp::create(Rewriter, Loc, Index, Five);
  mlir::Value BitIndex =
      mlir::LLVM::AndOp::create(Rewriter, Loc, Index, ThirtyOne);
  mlir::Value WordIndex64 = mlir::LLVM::ZExtOp::create(
      Rewriter, Loc, Rewriter.getI64Type(), WordIndex);
  mlir::Value Word = mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Value,
                                                          WordIndex64);
  mlir::Value Shifted =
      mlir::LLVM::LShrOp::create(Rewriter, Loc, Word, BitIndex);
  mlir::Value One = mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, 1);
  mlir::Value Bit = mlir::LLVM::AndOp::create(Rewriter, Loc, Shifted, One);
  return mlir::LLVM::TruncOp::create(Rewriter, Loc, Rewriter.getI1Type(), Bit);
}

/// Converts `spirv.GroupNonUniformInverseBallot` (roadmap L85) into
/// `extractBallotBit`'s own dynamic bit lookup, using
/// `llvm.spv.subgroup.local.invocation.id` (the same intrinsic
/// `ShuffleXorConversionPattern` above already relies on) as the "current
/// invocation" index the SPIR-V spec's own "Result is true if the current
/// invocation's bit is set to 1 in Value" text refers to.
class InverseBallotConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformInverseBallotOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformInverseBallotOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformInverseBallotOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::Value LocalId = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.subgroup.local.invocation.id",
        Rewriter.getI32Type(), {});
    Rewriter.replaceOp(
        Op, extractBallotBit(Rewriter, Loc, Adaptor.getValue(), LocalId));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBallotBitExtract` (roadmap L85) into
/// `extractBallotBit`'s own dynamic bit lookup, using this op's own
/// explicit `Index` operand directly -- the identical arithmetic
/// `InverseBallotConversionPattern` above uses, just against a caller-
/// supplied index instead of the current invocation's own id. Only a
/// 32-bit `Index` is supported, mirroring `ShuffleXorConversionPattern`'s
/// own "mask must be 32-bit" restriction above: every known dxc/glslang-
/// compiled `subgroupBallotBitExtract` call site's own `uint` index
/// operand is already 32-bit.
class BallotBitExtractConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformBallotBitExtractOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBallotBitExtractOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBallotBitExtractOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type I32 = Rewriter.getI32Type();
    if (Adaptor.getIndex().getType() != I32)
      return Rewriter.notifyMatchFailure(
          Op, "index must be 32-bit (as every known producer of this op "
              "emits)");

    Rewriter.replaceOp(Op,
                       extractBallotBit(Rewriter, Op.getLoc(),
                                        Adaptor.getValue(), Adaptor.getIndex()));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBallotFindLSB` (roadmap L85) into
/// `ballotVectorToI128`'s own 128-bit bitcast, clipped to the actual
/// `gl_SubgroupSize` via `clipBallotBitsToSubgroupSize` (see that
/// helper's own comment for why -- the SPIR-V spec only considers bits
/// actually representing invocations in the group), followed by
/// `llvm.cttz` (`is_zero_poison=true`, matching the SPIR-V spec's own "if
/// none of the considered bits is set to 1, the resulting value is
/// undefined" text) and a truncation down to the op's own result width.
/// Only a 32-bit result is supported, mirroring
/// `BallotBitExtractConversionPattern` above: every known dxc/glslang-
/// compiled `subgroupBallotFindLSB` call site's own `uint` result is
/// already 32-bit.
class BallotFindLSBConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformBallotFindLSBOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBallotFindLSBOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBallotFindLSBOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType || !mlir::isa<mlir::IntegerType>(ResultType) ||
        mlir::cast<mlir::IntegerType>(ResultType).getWidth() != 32)
      return Rewriter.notifyMatchFailure(
          Op, "only a 32-bit result is supported (as every known producer "
              "of this op emits)");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Bits = ballotVectorToI128(Rewriter, Loc, Adaptor.getValue());
    Bits = clipBallotBitsToSubgroupSize(Rewriter, Loc, Bits);
    mlir::Value Lsb = mlir::LLVM::CountTrailingZerosOp::create(
        Rewriter, Loc, Bits.getType(), Bits, /*isZeroPoison=*/true);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::TruncOp>(Op, ResultType, Lsb);
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBroadcastFirst` (roadmap L89e) into a
/// `llvm.spv.wave.readlane` from the first *active* invocation, computed
/// as the lowest set bit of this subgroup's own active-invocation ballot:
/// `readlane(Value, cttz(ballot(true)))`.
///
/// `ballot(true)` is by definition the mask of invocations active at this
/// point, so its lowest set bit is precisely the "active invocation with
/// the lowest id in the subgroup" this op broadcasts from. That makes the
/// whole lowering a composition of two already-CTS-verified pieces (the
/// `dEQP-VK.subgroups.ballot*` groups cover both) rather than a new wave
/// primitive: notably it needs no new `feme::cpu::WaveCallKind`, and so no
/// change to `SIMDize.cpp` or `WaveLowering.cpp` at all.
///
/// The ballot is clipped to `gl_SubgroupSize` for the same reason
/// `BallotFindLSBConversionPattern` clips: bits at or beyond the subgroup
/// size do not represent invocations in the group. `is_zero_poison` is
/// deliberately *false* here, unlike that pattern -- an all-zero ballot
/// means no invocation is active, which cannot happen at a point this op
/// actually executes, but `cttz` of zero would otherwise yield 128 and
/// feed `lowerReadLane` an out-of-range lane index. Returning 0 instead
/// keeps the gather in range, matching how
/// `getClampedFirstActiveLaneIndex` guards the identical hazard on the
/// DXIL-origin side.
class BroadcastFirstConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformBroadcastFirstOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBroadcastFirstOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBroadcastFirstOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Op.getExecutionScope() != mlir::spirv::Scope::Subgroup)
      return Rewriter.notifyMatchFailure(
          Op, "workgroup-scope broadcast-first is not supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I32 = Rewriter.getI32Type();
    mlir::Value True =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI1Type(), 1);
    mlir::Value Ballot =
        createIntrinsicCall(Rewriter, Loc, "llvm.spv.subgroup.ballot",
                            mlir::VectorType::get({4}, I32), {True});
    mlir::Value Bits = clipBallotBitsToSubgroupSize(
        Rewriter, Loc, ballotVectorToI128(Rewriter, Loc, Ballot));
    mlir::Value Lsb = mlir::LLVM::CountTrailingZerosOp::create(
        Rewriter, Loc, Bits.getType(), Bits, /*isZeroPoison=*/false);
    mlir::Value FirstLane =
        mlir::LLVM::TruncOp::create(Rewriter, Loc, I32, Lsb);

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.wave.readlane",
                                ResultType, {Adaptor.getValue(), FirstLane}));
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBallotFindMSB` (roadmap L85) into
/// `ballotVectorToI128`'s own 128-bit bitcast, clipped to the actual
/// `gl_SubgroupSize` exactly like `BallotFindLSBConversionPattern` above,
/// followed by `127 - llvm.ctlz(..., is_zero_poison=true)` (the
/// most-significant set bit's own index, counting from the low bit,
/// matching the SPIR-V spec's "if none of the considered bits is set to
/// 1, the resulting value is undefined" text the same way
/// `BallotFindLSBConversionPattern` above does) and a truncation down to
/// the op's own result width. Only a 32-bit result is supported,
/// mirroring `BallotFindLSBConversionPattern` above.
class BallotFindMSBConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformBallotFindMSBOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBallotFindMSBOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBallotFindMSBOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType || !mlir::isa<mlir::IntegerType>(ResultType) ||
        mlir::cast<mlir::IntegerType>(ResultType).getWidth() != 32)
      return Rewriter.notifyMatchFailure(
          Op, "only a 32-bit result is supported (as every known producer "
              "of this op emits)");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I128 = Rewriter.getIntegerType(128);
    mlir::Value Bits = ballotVectorToI128(Rewriter, Loc, Adaptor.getValue());
    Bits = clipBallotBitsToSubgroupSize(Rewriter, Loc, Bits);
    mlir::Value Clz = mlir::LLVM::CountLeadingZerosOp::create(
        Rewriter, Loc, I128, Bits, /*isZeroPoison=*/true);
    mlir::Value BitWidthMinusOne =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128, 127);
    mlir::Value Msb = mlir::LLVM::SubOp::create(Rewriter, Loc, I128,
                                               BitWidthMinusOne, Clz);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::TruncOp>(Op, ResultType, Msb);
    return mlir::success();
  }
};

/// Converts `spirv.GroupNonUniformBallotBitCount` (roadmap L85, all three
/// `GroupOperation`s GLSL's own `subgroupBallotBitCount`/
/// `InclusiveBitCount`/`ExclusiveBitCount` lower to, distinguished only by
/// this op's own `group_operation` attribute) into
/// `ballotVectorToI128`'s own 128-bit bitcast followed by `llvm.ctpop`
/// (`Reduce`, clipped to the actual `gl_SubgroupSize` first via
/// `clipBallotBitsToSubgroupSize` -- see that helper's own comment for
/// why) or, for `InclusiveScan`/`ExclusiveScan`, a mask over every
/// bit at-or-before (inclusive) / strictly before (exclusive) the current
/// invocation's own position first: `(1 << N) - 1` where `N` is the
/// invocation's own id (plus one for the inclusive variant), clamped to
/// "every bit" once `N` reaches the full 128-bit width (a plain `1 << 128`
/// would otherwise be a poison shift-by-out-of-range-amount) via a
/// `select` over an explicit `>= 128` check -- a value never actually
/// selected in practice (real subgroup sizes are always far below 128
/// invocations) but kept for correctness regardless; this scan-variant
/// mask needs no equivalent call to `clipBallotBitsToSubgroupSize` of its
/// own (see that helper's own comment). Only a 32-bit result
/// is supported, mirroring `BallotFindLSBConversionPattern` above.
class BallotBitCountConversionPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::GroupNonUniformBallotBitCountOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GroupNonUniformBallotBitCountOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GroupNonUniformBallotBitCountOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType || !mlir::isa<mlir::IntegerType>(ResultType) ||
        mlir::cast<mlir::IntegerType>(ResultType).getWidth() != 32)
      return Rewriter.notifyMatchFailure(
          Op, "only a 32-bit result is supported (as every known producer "
              "of this op emits)");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I128 = Rewriter.getIntegerType(128);
    mlir::Value Bits = ballotVectorToI128(Rewriter, Loc, Adaptor.getValue());

    if (Op.getGroupOperation() == mlir::spirv::GroupOperation::Reduce) {
      Bits = clipBallotBitsToSubgroupSize(Rewriter, Loc, Bits);
    } else {
      bool Inclusive =
          Op.getGroupOperation() == mlir::spirv::GroupOperation::InclusiveScan;
      mlir::Value LocalId = createIntrinsicCall(
          Rewriter, Loc, "llvm.spv.subgroup.local.invocation.id",
          Rewriter.getI32Type(), {});
      mlir::Value LocalId128 =
          mlir::LLVM::ZExtOp::create(Rewriter, Loc, I128, LocalId);
      mlir::Value PrefixBitCount = mlir::LLVM::AddOp::create(
          Rewriter, Loc, LocalId128,
          mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128,
                                         Inclusive ? 1 : 0));
      mlir::Value One = mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128, 1);
      // `Builder::getIntegerAttr(Type, int64_t)` only zero-extends a
      // signless integer's value beyond 64 bits (it does not sign-extend
      // one, unlike a *signed* integer type) -- passing a plain `-1`
      // here would silently produce only the low 64 bits set, not a
      // genuine 128-bit all-ones mask, so this needs the explicit-`APInt`
      // constant overload instead.
      mlir::Value AllOnes = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, I128, llvm::APInt::getAllOnes(128));
      mlir::Value Shifted = mlir::LLVM::ShlOp::create(Rewriter, Loc, One,
                                                      PrefixBitCount);
      mlir::Value LowMask =
          mlir::LLVM::SubOp::create(Rewriter, Loc, Shifted, One);
      mlir::Value FullWidth =
          mlir::LLVM::ConstantOp::create(Rewriter, Loc, I128, 128);
      mlir::Value IsFull = mlir::LLVM::ICmpOp::create(
          Rewriter, Loc, mlir::LLVM::ICmpPredicate::uge, PrefixBitCount,
          FullWidth);
      LowMask = mlir::LLVM::SelectOp::create(Rewriter, Loc, IsFull, AllOnes,
                                             LowMask);
      Bits = mlir::LLVM::AndOp::create(Rewriter, Loc, Bits, LowMask);
    }

    mlir::Value Popcount =
        mlir::LLVM::CtPopOp::create(Rewriter, Loc, I128, Bits);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::TruncOp>(Op, ResultType, Popcount);
    return mlir::success();
  }
};

/// `mlir::GroupReducePattern` (`mlir/lib/Conversion/SPIRVToLLVM/
/// SPIRVToLLVM.cpp`), the upstream pattern for `spirv.GroupNonUniform*`
/// arithmetic reductions (`WaveActiveSum`/`Product`/`Min`/`Max`/`BitAnd`/
/// `Or`/`Xor`'s own SPIR-V shape), lowers every one of them to a raw
/// mangled-name `llvm.call` (e.g. `_Z27__spirv_GroupNonUniformIAddii`)
/// rather than a real LLVM intrinsic. `feme::cpu::WaveUniformity`/
/// `SIMDizePass` (`feme/lib/Transforms/CPU/WaveUniformity.cpp`/
/// `SIMDize.cpp`) only ever classify and widen a *real* `llvm::
/// IntrinsicInst` (matched via `dyn_cast<IntrinsicInst>`) -- never a raw
/// mangled `CallInst` -- so every HLSL `WaveActiveSum`/`Product`/`Min`/
/// `Max`/`BitAnd`/`Or`/`Xor` call compiled through SPIR-V import silently
/// hit `feme-cpu-simdize`'s "unsupported divergent call" diagnostic,
/// scalar or vector, all along (roadmap H124a, reduced from
/// `offload-test-suite`'s own `WaveOps/WaveActiveMax.test`, a scalar-only
/// control case that reproduces this identically). This also let the
/// pre-existing `IntegerGroupNonUniformReducePattern` (which only fixed
/// up the `si32`/`ui32`-vs-`i32` signedness mismatch described below, not
/// this deeper issue) go unnoticed.
///
/// This pattern instead converts every `Reduce`-group-operation
/// arithmetic reduction directly to the matching `llvm.spv.wave.*`
/// intrinsic (mirroring `AllEqualConversionPattern`/
/// `RotateConversionPattern` above's own established convention), which
/// MLIR translates to a real `IntrinsicInst` `feme-cpu-simdize` already
/// knows how to classify (`WaveUniformity.cpp`) and widen
/// (`SIMDize.cpp`'s `widenWaveCall`, extended this same roadmap entry to
/// decompose a vector operand into its per-component reduce calls, see
/// `isVectorOperandReduceKind` there). Every `llvm.spv.wave.reduce.*`/
/// `llvm.spv.wave.product` intrinsic (`llvm/include/llvm/IR/
/// IntrinsicsSPIRV.td`) is `llvm_any_ty`-overloaded, so a *vector*
/// operand needs no scalarization at all at this MLIR level -- unlike
/// `AllEqualConversionPattern`'s own vector case, which does need one
/// extra step because its *result* type does not vary with the operand.
///
/// Running the op's result through the dialect conversion's own
/// `TypeConverter` (rather than passing `op.getResult()`'s raw SPIR-V
/// type straight through, as upstream's own pattern does) additionally
/// fixes the pre-existing `IntegerGroupNonUniformReducePattern`'s own
/// signedness bug: HLSL's `int`/`uint` distinction survives into MLIR's
/// SPIR-V dialect as `si32`/`ui32` (a signless `i32`'s two
/// signedness-carrying siblings, used only to preserve `OpSConvert`/
/// `OpUConvert`-style signedness information for as long as possible),
/// and `si32`/`ui32` are not themselves valid LLVM dialect types at all
/// -- only signless `i32` is (roadmap L10, reduced from a real
/// `offload-test-suite` `WaveOps/WaveActiveSum.convergence.test` case).
///
/// Only the `Reduce` group operation has a matching intrinsic here:
/// `ClusteredReduce` (unreachable from any HLSL `Wave*` intrinsic today)
/// is left to upstream's own `GroupReducePattern` -- correctly, since no
/// HLSL intrinsic ever produces it. (Roadmap H126) `ExclusiveScan` --
/// HLSL's `WavePrefixSum`/`WavePrefixProduct` -- is a different story:
/// upstream's own `GroupReducePattern` was *not* actually a fix for that
/// case, contrary to this comment's own original (roadmap L10) claim --
/// it has exactly the same `retTy = op.getResult().getType()` bug
/// `IntegerGroupNonUniformReducePattern` had for `Reduce`, just never
/// converting the SPIR-V dialect's own `si32`/`ui32` result type to a
/// valid LLVM dialect `i32` before handing it to `createSPIRVBuiltinCall`
/// -- so *every* `WavePrefixSum`/`WavePrefixProduct` call compiled
/// through SPIR-V import hit dialect conversion's own "'llvm.call' op
/// result #0 must be LLVM dialect-compatible type, but got 'si32'"
/// diagnostic at pipeline-creation time, unconditionally (reduced from a
/// real `offload-test-suite` `WaveOps/WavePrefixSum.convergence.test`
/// case). See `GroupNonUniformScanPattern` below, the identical fix for
/// this group operation instead of `Reduce`'s.
template <typename ReduceOp>
constexpr llvm::StringLiteral getGroupNonUniformReduceIntrinsicName();
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformIAddOp>() {
  return "llvm.spv.wave.reduce.sum";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformFAddOp>() {
  return "llvm.spv.wave.reduce.sum";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformIMulOp>() {
  return "llvm.spv.wave.product";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformFMulOp>() {
  return "llvm.spv.wave.product";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformSMinOp>() {
  return "llvm.spv.wave.reduce.min";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformFMinOp>() {
  return "llvm.spv.wave.reduce.min";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformUMinOp>() {
  return "llvm.spv.wave.reduce.umin";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformSMaxOp>() {
  return "llvm.spv.wave.reduce.max";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformFMaxOp>() {
  return "llvm.spv.wave.reduce.max";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformReduceIntrinsicName<mlir::spirv::GroupNonUniformUMaxOp>() {
  return "llvm.spv.wave.reduce.umax";
}
template <>
constexpr llvm::StringLiteral getGroupNonUniformReduceIntrinsicName<
    mlir::spirv::GroupNonUniformBitwiseAndOp>() {
  return "llvm.spv.wave.reduce.and";
}
template <>
constexpr llvm::StringLiteral getGroupNonUniformReduceIntrinsicName<
    mlir::spirv::GroupNonUniformBitwiseOrOp>() {
  return "llvm.spv.wave.reduce.or";
}
template <>
constexpr llvm::StringLiteral getGroupNonUniformReduceIntrinsicName<
    mlir::spirv::GroupNonUniformBitwiseXorOp>() {
  return "llvm.spv.wave.reduce.xor";
}

template <typename ReduceOp>
class GroupNonUniformReducePattern
    : public mlir::SPIRVToLLVMConversion<ReduceOp> {
public:
  using mlir::SPIRVToLLVMConversion<ReduceOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(ReduceOp Op, typename ReduceOp::Adaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Adaptor.getGroupOperation() != mlir::spirv::GroupOperation::Reduce)
      return Rewriter.notifyMatchFailure(
          Op, "only the Reduce group operation has a matching intrinsic; "
              "InclusiveScan/ExclusiveScan/ClusteredReduce fall back to "
              "upstream's own GroupReducePattern");

    mlir::Type ResultType =
        this->getTypeConverter()->convertType(Op.getResult().getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    Rewriter.replaceOp(
        Op, createIntrinsicCall(
                Rewriter, Op.getLoc(),
                getGroupNonUniformReduceIntrinsicName<ReduceOp>(), ResultType,
                {Adaptor.getValue()}));
    return mlir::success();
  }
};

/// (Roadmap H126) The `ExclusiveScan` counterpart to
/// `GroupNonUniformReducePattern` above -- see that pattern's own comment
/// and `getGroupNonUniformReduceIntrinsicName`'s for the shared
/// background (the `si32`/`ui32`-vs-`i32` signedness bug this fixes by
/// running the result through the real `TypeConverter`, and why using a
/// real `llvm.spv.wave.prefix.*` intrinsic instead of a raw mangled-name
/// call matters for `feme-cpu-simdize`/`WaveUniformity` to ever recognize
/// the call at all). Only registered for the two op families HLSL can
/// actually produce an `ExclusiveScan` for (`WavePrefixSum`/
/// `WavePrefixProduct`); `getGroupNonUniformScanIntrinsicName`'s primary
/// template is deliberately left undefined, so instantiating this
/// pattern for any other `ReduceOp` is a compile error rather than a
/// runtime one.
template <typename ReduceOp>
constexpr llvm::StringLiteral getGroupNonUniformScanIntrinsicName();
template <>
constexpr llvm::StringLiteral
getGroupNonUniformScanIntrinsicName<mlir::spirv::GroupNonUniformIAddOp>() {
  return "llvm.spv.wave.prefix.sum";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformScanIntrinsicName<mlir::spirv::GroupNonUniformFAddOp>() {
  return "llvm.spv.wave.prefix.sum";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformScanIntrinsicName<mlir::spirv::GroupNonUniformIMulOp>() {
  return "llvm.spv.wave.prefix.product";
}
template <>
constexpr llvm::StringLiteral
getGroupNonUniformScanIntrinsicName<mlir::spirv::GroupNonUniformFMulOp>() {
  return "llvm.spv.wave.prefix.product";
}

template <typename ReduceOp>
class GroupNonUniformScanPattern
    : public mlir::SPIRVToLLVMConversion<ReduceOp> {
public:
  using mlir::SPIRVToLLVMConversion<ReduceOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(ReduceOp Op, typename ReduceOp::Adaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (Adaptor.getGroupOperation() != mlir::spirv::GroupOperation::ExclusiveScan)
      return Rewriter.notifyMatchFailure(
          Op, "only the ExclusiveScan group operation has a matching "
              "intrinsic here; Reduce/InclusiveScan/ClusteredReduce fall "
              "back to GroupNonUniformReducePattern/upstream's own "
              "GroupReducePattern");

    mlir::Type ResultType =
        this->getTypeConverter()->convertType(Op.getResult().getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    Rewriter.replaceOp(
        Op, createIntrinsicCall(
                Rewriter, Op.getLoc(),
                getGroupNonUniformScanIntrinsicName<ReduceOp>(), ResultType,
                {Adaptor.getValue()}));
    return mlir::success();
  }
};

/// `mlir::BranchConditionalConversionPattern` (`mlir/lib/Conversion/
/// SPIRVToLLVM/SPIRVToLLVM.cpp`), the upstream pattern for
/// `spirv.BranchConditional`, builds its `llvm.cond_br`'s true/false
/// successor operands directly from `op.getTrueBlockArguments()`/
/// `op.getFalseBlockArguments()` -- the *original* op's own raw operand
/// accessors -- rather than `adaptor.getTrueTargetOperands()`/
/// `adaptor.getFalseTargetOperands()`, the dialect conversion's own
/// remapped (type-converted) operands. This is the identical class of bug
/// roadmap L10's `GroupNonUniformReducePattern` above fixed for
/// `spirv.GroupNonUniform*`'s reduce operand/result: whenever a successor
/// block argument being passed along a conditional branch is itself an
/// `si32`/`ui32` value (HLSL's `int`/`uint` distinction, preserved in
/// MLIR's SPIR-V dialect but not a valid LLVM dialect type on its own),
/// the *original*, un-remapped SSA value still carries that
/// dialect-conversion-illegal type, producing the dialect conversion
/// legalizer's own "operand #1 must be variadic of LLVM dialect-compatible
/// type, but got 'si32'" diagnostic on the freshly built `llvm.cond_br`
/// (roadmap L18, reduced from the real
/// `Feature/StructuredBuffer/packed.test`: its `if (Fido.TailState == 0)`
/// merges an `si32`-typed `TailState` value back into `^bb1` via exactly
/// this shape). Registered at `FeMeBenefit` so it wins over the upstream
/// pattern for every `spirv.BranchConditional`; unlike
/// `GroupNonUniformReducePattern` (which only needs to special-case
/// nine specific integer-typed ops), this fix applies uniformly to every
/// `spirv.BranchConditional` regardless of its successor operands' types,
/// since simply using the adaptor's own already-correctly-remapped
/// operands in place of the op's raw ones is a strict improvement with no
/// downside for any other case.
class BranchConditionalPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::BranchConditionalOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::BranchConditionalOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::BranchConditionalOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    // Mirrors upstream's own branch-weights handling verbatim; only the
    // successor operands below differ.
    mlir::DenseI32ArrayAttr BranchWeights = nullptr;
    if (auto Weights = Op.getBranchWeights()) {
      llvm::SmallVector<int32_t> WeightValues;
      for (auto Weight : Weights->getAsRange<mlir::IntegerAttr>())
        WeightValues.push_back(Weight.getInt());
      BranchWeights = mlir::DenseI32ArrayAttr::get(getContext(), WeightValues);
    }

    Rewriter.replaceOpWithNewOp<mlir::LLVM::CondBrOp>(
        Op, Adaptor.getCondition(), Adaptor.getTrueTargetOperands(),
        Adaptor.getFalseTargetOperands(), BranchWeights, Op.getTrueBlock(),
        Op.getFalseBlock());
    return mlir::success();
  }
};

/// Returns the bit width of `Type`'s element, where `Type` is always the
/// *already-converted* LLVM dialect type of a `BitField*` operand (a
/// signless integer, or a fixed vector thereof) -- never the original,
/// still-SPIR-V-dialect-tagged (`si32`/`ui32`) type of the op's own operand.
static unsigned getBitFieldElementBitWidth(mlir::Type Type) {
  if (auto VecTy = mlir::dyn_cast<mlir::VectorType>(Type))
    return mlir::cast<mlir::IntegerType>(VecTy.getElementType()).getWidth();
  return mlir::cast<mlir::IntegerType>(Type).getWidth();
}

/// Builds an `llvm.mlir.constant` of `Value`, splatted across every lane if
/// `DstType` is a vector -- shared by the `Offset`+`Count`-mask constants in
/// `BitFieldInsertPattern`/`BitFieldUExtractPattern` and the base-bit-width
/// constant in `BitFieldSExtractPattern`.
static mlir::Value createBitFieldConstant(mlir::ConversionPatternRewriter &Rewriter,
                                          mlir::Location Loc, mlir::Type DstType,
                                          int64_t Value) {
  if (auto VecTy = mlir::dyn_cast<mlir::VectorType>(DstType)) {
    auto ElemTy = mlir::cast<mlir::IntegerType>(VecTy.getElementType());
    auto Attr = Rewriter.getIntegerAttr(ElemTy, Value);
    return mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, DstType, mlir::SplatElementsAttr::get(VecTy, Attr));
  }
  auto ElemTy = mlir::cast<mlir::IntegerType>(DstType);
  return mlir::LLVM::ConstantOp::create(Rewriter, Loc, DstType,
                                        Rewriter.getIntegerAttr(ElemTy, Value));
}

/// Broadcasts an already-converted scalar `Value` to a `NumElements`-lane
/// vector of its own type -- the vector-`Base` counterpart of
/// `broadcastScalar` above (which broadcasts into a caller-chosen
/// `VectorType` for matrix arithmetic); this one derives the vector type
/// from `Value` itself, matching upstream's own `broadcast` helper in
/// `mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`.
static mlir::Value broadcastBitFieldOperand(mlir::ConversionPatternRewriter &Rewriter,
                                            mlir::Location Loc, mlir::Value Value,
                                            unsigned NumElements) {
  auto VecTy = mlir::VectorType::get(NumElements, Value.getType());
  mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, VecTy);
  for (unsigned I = 0; I != NumElements; ++I) {
    mlir::Value Index = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI32Type(), Rewriter.getI32IntegerAttr(I));
    Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, VecTy, Result,
                                                 Value, Index);
  }
  return Result;
}

/// Prepares a `BitField*` op's `Offset` or `Count` operand for use as an
/// `llvm.shl`/`llvm.lshr` shift amount: broadcasts it to match `DstType`'s
/// lane count if `Base` is a vector, then zero-extends or truncates it to
/// `DstType`'s own element width (`Offset`/`Count` are always "consumed as
/// an unsigned value" per the SPIR-V spec, so this never sign-extends,
/// matching upstream's own `processCountOrOffset`/`optionallyTruncateOrExtend`).
///
/// Unlike upstream's version, `AdaptorValue` here is always the pattern's
/// own `OpAdaptor`-supplied, already-type-converted value (a signless
/// integer, never a stale `si32`/`ui32`) -- this is the actual fix for
/// H10h: upstream's `BitFieldInsertPattern`/`BitFieldSExtractPattern`/
/// `BitFieldUExtractPattern` instead call this helper (`processCountOrOffset`)
/// with the *op's own raw* `getOffset()`/`getCount()`, which -- whenever the
/// operand's width already matches `Base`'s width, the common case, since
/// SPIR-V's `BitField*` ops require same-width `Offset`/`Count` in every
/// real shader -- both this broadcast step and the truncate/extend step
/// below become no-ops, silently returning that stale, dialect-conversion-
/// illegal value straight through into the final `llvm.shl`/`llvm.lshr`.
static mlir::Value processBitFieldCountOrOffset(
    mlir::ConversionPatternRewriter &Rewriter, mlir::Location Loc,
    mlir::Value AdaptorValue, mlir::Type DstType) {
  mlir::Value Value = AdaptorValue;
  if (auto VecTy = mlir::dyn_cast<mlir::VectorType>(DstType))
    if (!mlir::isa<mlir::VectorType>(Value.getType()))
      Value = broadcastBitFieldOperand(Rewriter, Loc, Value,
                                       VecTy.getNumElements());

  unsigned TargetWidth = getBitFieldElementBitWidth(DstType);
  unsigned ValueWidth = getBitFieldElementBitWidth(Value.getType());
  if (ValueWidth < TargetWidth)
    return mlir::LLVM::ZExtOp::create(Rewriter, Loc, DstType, Value);
  if (ValueWidth > TargetWidth)
    return mlir::LLVM::TruncOp::create(Rewriter, Loc, DstType, Value);
  return Value;
}

/// Converts `spirv.BitFieldInsert` -- overriding MLIR's own pattern (see
/// `processBitFieldCountOrOffset` above for why: it feeds `Base`/`Insert`/
/// `Offset`/`Count` from the op's own raw accessors into the final
/// `llvm.shl`/`llvm.and`/`llvm.xor` ops instead of the adaptor's
/// type-converted ones, producing an ill-typed `llvm.shl` whenever the
/// SPIR-V module's integers are signed/unsigned rather than already
/// signless). Otherwise mirrors upstream's own bit-mask construction
/// verbatim: build a mask covering `[Offset, Offset + Count - 1]`, clear
/// those bits in `Base`, and `or` in `Insert`'s low `Count` bits shifted up
/// by `Offset`.
class BitFieldInsertPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::BitFieldInsertOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::BitFieldInsertOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::BitFieldInsertOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    mlir::Location Loc = Op.getLoc();

    mlir::Value Offset = processBitFieldCountOrOffset(
        Rewriter, Loc, Adaptor.getOffset(), DstType);
    mlir::Value Count = processBitFieldCountOrOffset(Rewriter, Loc,
                                                     Adaptor.getCount(), DstType);

    mlir::Value MinusOne = createBitFieldConstant(Rewriter, Loc, DstType, -1);
    mlir::Value MaskShiftedByCount =
        mlir::LLVM::ShlOp::create(Rewriter, Loc, DstType, MinusOne, Count);
    mlir::Value Negated = mlir::LLVM::XOrOp::create(
        Rewriter, Loc, DstType, MaskShiftedByCount, MinusOne);
    mlir::Value MaskShiftedByCountAndOffset =
        mlir::LLVM::ShlOp::create(Rewriter, Loc, DstType, Negated, Offset);
    mlir::Value Mask = mlir::LLVM::XOrOp::create(
        Rewriter, Loc, DstType, MaskShiftedByCountAndOffset, MinusOne);

    mlir::Value BaseAndMask = mlir::LLVM::AndOp::create(
        Rewriter, Loc, DstType, Adaptor.getBase(), Mask);
    mlir::Value InsertShiftedByOffset = mlir::LLVM::ShlOp::create(
        Rewriter, Loc, DstType, Adaptor.getInsert(), Offset);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::OrOp>(Op, DstType, BaseAndMask,
                                                  InsertShiftedByOffset);
    return mlir::success();
  }
};

/// Converts `spirv.BitFieldSExtract` -- see `BitFieldInsertPattern` above for
/// why this overrides MLIR's own pattern. Mirrors upstream's own two-shift
/// sign-extension construction verbatim: shift `Base` left so the field's
/// most-significant bit lands in `Base`'s own sign position, then shift
/// right by the same amount plus `Offset`, letting `llvm.ashr` replicate
/// that sign bit across the vacated high bits.
class BitFieldSExtractPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::BitFieldSExtractOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::BitFieldSExtractOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::BitFieldSExtractOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    mlir::Location Loc = Op.getLoc();

    mlir::Value Offset = processBitFieldCountOrOffset(
        Rewriter, Loc, Adaptor.getOffset(), DstType);
    mlir::Value Count = processBitFieldCountOrOffset(Rewriter, Loc,
                                                     Adaptor.getCount(), DstType);

    mlir::Value Size = createBitFieldConstant(
        Rewriter, Loc, DstType, getBitFieldElementBitWidth(DstType));

    mlir::Value CountPlusOffset =
        mlir::LLVM::AddOp::create(Rewriter, Loc, DstType, Count, Offset);
    mlir::Value AmountToShiftLeft = mlir::LLVM::SubOp::create(
        Rewriter, Loc, DstType, Size, CountPlusOffset);
    mlir::Value BaseShiftedLeft = mlir::LLVM::ShlOp::create(
        Rewriter, Loc, DstType, Adaptor.getBase(), AmountToShiftLeft);

    mlir::Value AmountToShiftRight = mlir::LLVM::AddOp::create(
        Rewriter, Loc, DstType, Offset, AmountToShiftLeft);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::AShrOp>(
        Op, DstType, BaseShiftedLeft, AmountToShiftRight);
    return mlir::success();
  }
};

/// Converts `spirv.BitFieldUExtract` -- see `BitFieldInsertPattern` above for
/// why this overrides MLIR's own pattern. Mirrors upstream's own
/// mask-and-shift construction verbatim: shift `Base` right by `Offset`,
/// then mask off everything above bit `Count - 1`.
class BitFieldUExtractPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::BitFieldUExtractOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::BitFieldUExtractOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::BitFieldUExtractOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    mlir::Location Loc = Op.getLoc();

    mlir::Value Offset = processBitFieldCountOrOffset(
        Rewriter, Loc, Adaptor.getOffset(), DstType);
    mlir::Value Count = processBitFieldCountOrOffset(Rewriter, Loc,
                                                     Adaptor.getCount(), DstType);

    mlir::Value MinusOne = createBitFieldConstant(Rewriter, Loc, DstType, -1);
    mlir::Value MaskShiftedByCount =
        mlir::LLVM::ShlOp::create(Rewriter, Loc, DstType, MinusOne, Count);
    mlir::Value Mask = mlir::LLVM::XOrOp::create(
        Rewriter, Loc, DstType, MaskShiftedByCount, MinusOne);

    mlir::Value ShiftedBase = mlir::LLVM::LShrOp::create(
        Rewriter, Loc, DstType, Adaptor.getBase(), Offset);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::AndOp>(Op, DstType, ShiftedBase,
                                                   Mask);
    return mlir::success();
  }
};

/// Converts `spirv.CopyObject` -- a plain type-preserving copy with no
/// dereferences (roadmap L86: MLIR's own `spirv.CopyObject`, added
/// upstream-style as this row's own prerequisite) -- into nothing at all:
/// LLVM IR's own SSA values already have this exact "make another handle
/// to the same value" semantics implicitly for any value that isn't a
/// pointer/memory reference, so the op's own result is simply replaced
/// with its already-converted operand directly, with no new IR emitted.
/// dxc's own codegen convention pairs this op with a `NonUniform`
/// decoration on its result (see roadmap L7f's own decoration-handling
/// fix) whenever `NonUniformResourceIndex()` wraps a dynamic resource-array
/// index -- `feme`'s own legalization has no separate notion of
/// "non-uniform-ness" to preserve past this point (every access already
/// gets whatever divergent/uniform handling its own address's actual
/// uniformity dictates, downstream in `feme::cpu::SIMDizePass`), so this
/// decoration itself needs no equivalent handling here either.
class CopyObjectConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::CopyObjectOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::CopyObjectOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::CopyObjectOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    Rewriter.replaceOp(Op, Adaptor.getOperand());
    return mlir::success();
  }
};

/// Converts `spirv.Dot` -- which, like `spirv.Switch` above, MLIR has no
/// pattern for at all -- into a per-lane `llvm.intr.fmuladd` chain, mirroring
/// `feme::dxil::expandFDot`'s expansion of the analogous (post-raising)
/// `llvm.dx.fdot` intrinsic on the DXIL side (see
/// feme/lib/Transforms/DXIL/IntrinsicExpansion.cpp): both take two same-width
/// float vectors and reduce them to a single scalar with the same
/// fused-multiply-add semantics, just at different points in the pipeline.
class DotConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::DotOp> {
public:

  using mlir::SPIRVToLLVMConversion<mlir::spirv::DotOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::DotOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::Value Vector1 = Adaptor.getVector1();
    mlir::Value Vector2 = Adaptor.getVector2();
    auto VectorType = mlir::cast<mlir::VectorType>(Vector1.getType());
    int64_t NumElements = VectorType.getNumElements();

    auto ExtractElement = [&](mlir::Value Vector, int64_t Index) {
      mlir::Value IndexValue =
          mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI64Type(),
                                         Rewriter.getI64IntegerAttr(Index));
      return mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Vector,
                                                  IndexValue);
    };

    mlir::Value Result = mlir::LLVM::FMulOp::create(
        Rewriter, Loc, ExtractElement(Vector1, 0), ExtractElement(Vector2, 0));
    for (int64_t I = 1; I != NumElements; ++I)
      Result = mlir::LLVM::FMulAddOp::create(
          Rewriter, Loc, ExtractElement(Vector1, I), ExtractElement(Vector2, I),
          Result);

    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Extracts the lanes `spirv.SDot`/`spirv.UDot`/`spirv.SUDot` and their
/// `*AccSat` counterparts (roadmap E8, `VK_KHR_shader_integer_dot_product`)
/// reduce over, each sign- or zero-extended to \p ResultType per \p
/// Vector1Signed/\p Vector2Signed. A real vector operand's elements are used
/// directly; a scalar 32-bit operand (legal only together with the
/// `PackedVectorFormat4x8Bit` format -- the only format value SPIR-V defines
/// today, per `verifyIntegerDotProduct` in MLIR's `DotProductOps.cpp`) is
/// unpacked into its four constituent bytes first, byte 0 occupying the
/// low-order bits, matching the packing HLSL's analogous `dot4add_*8packed`
/// intrinsics already use.
void extractIntegerDotProductLanes(
    mlir::ConversionPatternRewriter &Rewriter, mlir::Location Loc,
    mlir::Value Vector1, mlir::Value Vector2, mlir::Type ResultType,
    bool Vector1Signed, bool Vector2Signed,
    llvm::SmallVectorImpl<mlir::Value> &Lanes1,
    llvm::SmallVectorImpl<mlir::Value> &Lanes2) {
  auto ExtendTo = [&](mlir::Value V, bool Signed) -> mlir::Value {
    if (V.getType() == ResultType)
      return V;
    if (Signed)
      return mlir::LLVM::SExtOp::create(Rewriter, Loc, ResultType, V);
    return mlir::LLVM::ZExtOp::create(Rewriter, Loc, ResultType, V);
  };

  if (auto VectorType = mlir::dyn_cast<mlir::VectorType>(Vector1.getType())) {
    int64_t NumElements = VectorType.getNumElements();
    for (int64_t I = 0; I != NumElements; ++I) {
      mlir::Value Index = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI64Type(), Rewriter.getI64IntegerAttr(I));
      Lanes1.push_back(ExtendTo(
          mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Vector1, Index),
          Vector1Signed));
      Lanes2.push_back(ExtendTo(
          mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Vector2, Index),
          Vector2Signed));
    }
    return;
  }

  mlir::Type ScalarType = Vector1.getType();
  mlir::Type ByteType = Rewriter.getI8Type();
  constexpr unsigned NumPackedBytes = 4;
  auto UnpackByte = [&](mlir::Value Scalar, unsigned ByteIndex,
                        bool Signed) -> mlir::Value {
    mlir::Value Shift = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, ScalarType,
        Rewriter.getIntegerAttr(ScalarType, ByteIndex * 8));
    mlir::Value Shifted =
        mlir::LLVM::LShrOp::create(Rewriter, Loc, Scalar, Shift);
    mlir::Value Byte =
        mlir::LLVM::TruncOp::create(Rewriter, Loc, ByteType, Shifted);
    return ExtendTo(Byte, Signed);
  };
  for (unsigned I = 0; I != NumPackedBytes; ++I) {
    Lanes1.push_back(UnpackByte(Vector1, I, Vector1Signed));
    Lanes2.push_back(UnpackByte(Vector2, I, Vector2Signed));
  }
}

/// Multiplies each corresponding pair of already-extended `Lanes1[i]`/
/// `Lanes2[i]` and sums the products: the integer analogue of
/// `DotConversionPattern`'s per-lane `llvm.intr.fmuladd` chain above, using
/// plain multiply/add since an integer dot product has no intermediate-
/// rounding concern a fused op would need to address.
mlir::Value reduceIntegerDotProductLanes(
    mlir::ConversionPatternRewriter &Rewriter, mlir::Location Loc,
    llvm::ArrayRef<mlir::Value> Lanes1, llvm::ArrayRef<mlir::Value> Lanes2) {
  mlir::Value Result =
      mlir::LLVM::MulOp::create(Rewriter, Loc, Lanes1[0], Lanes2[0]);
  for (size_t I = 1, E = Lanes1.size(); I != E; ++I) {
    mlir::Value Product =
        mlir::LLVM::MulOp::create(Rewriter, Loc, Lanes1[I], Lanes2[I]);
    Result = mlir::LLVM::AddOp::create(Rewriter, Loc, Result, Product);
  }
  return Result;
}

/// Converts `spirv.SDot`/`spirv.UDot`/`spirv.SUDot` (roadmap E8): none of
/// the three has an upstream MLIR conversion pattern, exactly like
/// `spirv.Dot` above. \p Vector1Signed/\p Vector2Signed select which of the
/// three this instantiates: both signed (`SDot`), both unsigned (`UDot`), or
/// mixed -- vector 1 signed, vector 2 unsigned (`SUDot`), per each op's own
/// spec-defined extension semantics.
template <typename OpTy, bool Vector1Signed, bool Vector2Signed>
class IntegerDotProductConversionPattern
    : public mlir::SPIRVToLLVMConversion<OpTy> {
public:
  using mlir::SPIRVToLLVMConversion<OpTy>::SPIRVToLLVMConversion;
  using OpAdaptor = typename mlir::SPIRVToLLVMConversion<OpTy>::OpAdaptor;

  mlir::LogicalResult
  matchAndRewrite(OpTy Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType =
        this->getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    llvm::SmallVector<mlir::Value> Lanes1, Lanes2;
    extractIntegerDotProductLanes(Rewriter, Loc, Adaptor.getVector1(),
                                  Adaptor.getVector2(), ResultType,
                                  Vector1Signed, Vector2Signed, Lanes1,
                                  Lanes2);
    Rewriter.replaceOp(Op, reduceIntegerDotProductLanes(Rewriter, Loc, Lanes1,
                                                        Lanes2));
    return mlir::success();
  }
};
using SDotConversionPattern =
    IntegerDotProductConversionPattern<mlir::spirv::SDotOp,
                                       /*Vector1Signed=*/true,
                                       /*Vector2Signed=*/true>;
using UDotConversionPattern =
    IntegerDotProductConversionPattern<mlir::spirv::UDotOp,
                                       /*Vector1Signed=*/false,
                                       /*Vector2Signed=*/false>;
using SUDotConversionPattern =
    IntegerDotProductConversionPattern<mlir::spirv::SUDotOp,
                                       /*Vector1Signed=*/true,
                                       /*Vector2Signed=*/false>;

/// Converts `spirv.SDotAccSat`/`spirv.SUDotAccSat`/`spirv.UDotAccSat`
/// (roadmap E8): the same lane extraction and reduction as
/// `IntegerDotProductConversionPattern` above, followed by a saturating
/// addition of the accumulator -- signed for `SDotAccSat`/`SUDotAccSat`
/// (the accumulator is always `Result Type`-signed for these two, per the
/// spec's "signed saturating addition" wording for both), unsigned only for
/// `UDotAccSat`.
template <typename OpTy, bool Vector1Signed, bool Vector2Signed,
         bool SaturateSigned>
class IntegerDotProductAccSatConversionPattern
    : public mlir::SPIRVToLLVMConversion<OpTy> {
public:
  using mlir::SPIRVToLLVMConversion<OpTy>::SPIRVToLLVMConversion;
  using OpAdaptor = typename mlir::SPIRVToLLVMConversion<OpTy>::OpAdaptor;

  mlir::LogicalResult
  matchAndRewrite(OpTy Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType =
        this->getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    llvm::SmallVector<mlir::Value> Lanes1, Lanes2;
    extractIntegerDotProductLanes(Rewriter, Loc, Adaptor.getVector1(),
                                  Adaptor.getVector2(), ResultType,
                                  Vector1Signed, Vector2Signed, Lanes1,
                                  Lanes2);
    mlir::Value Sum =
        reduceIntegerDotProductLanes(Rewriter, Loc, Lanes1, Lanes2);
    mlir::Value Saturated =
        SaturateSigned
            ? mlir::Value(mlir::LLVM::SAddSat::create(
                  Rewriter, Loc, Sum, Adaptor.getAccumulator()))
            : mlir::Value(mlir::LLVM::UAddSat::create(
                  Rewriter, Loc, Sum, Adaptor.getAccumulator()));
    Rewriter.replaceOp(Op, Saturated);
    return mlir::success();
  }
};
using SDotAccSatConversionPattern = IntegerDotProductAccSatConversionPattern<
    mlir::spirv::SDotAccSatOp, /*Vector1Signed=*/true, /*Vector2Signed=*/true,
    /*SaturateSigned=*/true>;
using UDotAccSatConversionPattern = IntegerDotProductAccSatConversionPattern<
    mlir::spirv::UDotAccSatOp, /*Vector1Signed=*/false,
    /*Vector2Signed=*/false, /*SaturateSigned=*/false>;
using SUDotAccSatConversionPattern = IntegerDotProductAccSatConversionPattern<
    mlir::spirv::SUDotAccSatOp, /*Vector1Signed=*/true,
    /*Vector2Signed=*/false, /*SaturateSigned=*/true>;

/// Replaces `spirv.mlir.addressof` of a builtin input variable with the
/// `llvm.spv.*` intrinsic reading it. There is no LLVM global to take the
/// address of: LLVM's SPIRV backend synthesizes the `OpVariable`, and its
/// `BuiltIn` decoration, from the intrinsic itself, so the "address" a
/// builtin variable has in SPIR-V is modeled as the value it holds, and this
/// converts its pointee type directly (rather than going through the pointer
/// type conversion, which non-builtin `Input`/`Output` variables need to
/// convert to an ordinary pointer instead -- see
/// feme::spirv::populateSPIRVToLLVMTargetTypeConversions).
class BuiltInAddressOfPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AddressOfOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::AddressOfOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AddressOfOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::spirv::GlobalVariableOp Global = getReferencedGlobal(Op);
    if (!Global)
      return Rewriter.notifyMatchFailure(Op, "no such global variable");
    const BuiltInMapping *Mapping = getBuiltInMapping(Global);
    std::optional<SubgroupMaskKind> MaskKind = getSubgroupMaskBuiltIn(Global);
    if (!Mapping && !MaskKind)
      return Rewriter.notifyMatchFailure(
          Op, "not a builtin variable with an LLVM equivalent");

    auto PointerTy = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    mlir::Type ResultType =
        getTypeConverter()->convertType(PointerTy.getPointeeType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    if (MaskKind) {
      Rewriter.replaceOp(
          Op, buildSubgroupMask(Rewriter, Loc, *MaskKind, ResultType));
      return mlir::success();
    }

    auto VectorTy = mlir::dyn_cast<mlir::VectorType>(ResultType);
    if (!VectorTy) {
      llvm::SmallVector<mlir::Value, 1> Args;
      if (Mapping->PerComponent)
        Args.push_back(mlir::LLVM::ConstantOp::create(
            Rewriter, Loc, Rewriter.getI32Type(), 0));
      Rewriter.replaceOp(Op,
                         createIntrinsicCall(Rewriter, Loc, Mapping->Intrinsic,
                                             ResultType, Args));
      return mlir::success();
    }

    if (!Mapping->PerComponent || VectorTy.getRank() != 1)
      return Rewriter.notifyMatchFailure(
          Op, "builtin variable has no vector-valued LLVM equivalent");

    mlir::Type ElementTy = VectorTy.getElementType();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, VectorTy);
    for (int64_t I = 0, E = VectorTy.getNumElements(); I != E; ++I) {
      mlir::Value Index = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI32Type(), I);
      mlir::Value Component = createIntrinsicCall(
          Rewriter, Loc, Mapping->Intrinsic, ElementTy, Index);
      Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                   Component, Index);
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Drops a builtin input variable's declaration; the intrinsics
/// BuiltInAddressOfPattern emits carry the whole declaration with them.
class BuiltInGlobalVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GlobalVariableOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GlobalVariableOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!isValueModeledBuiltIn(Op))
      return Rewriter.notifyMatchFailure(
          Op, "not a builtin variable with an LLVM equivalent");
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// A SPIR-V decoration this pattern preserves for a stage-IO variable, and
/// the `!spirv.Decorations`-shaped (see `getStageIODecorationsAttrName`)
/// code it maps to. Values match the SPIR-V spec's `Decoration` enumerators;
/// see `mlir::spirv::Decoration` (`SPIRVBase.td`) for the same numbering.
struct StageIODecoration {
  llvm::StringLiteral AttrName;
  uint32_t Code;
};

/// The boolean (no-argument) decorations a non-builtin `Input`/`Output`
/// variable may carry: the fragment-stage interpolation qualifiers, and
/// `Patch`/`PerPrimitiveEXT` for tessellation/mesh per-patch and
/// per-primitive variables. `Component`/`Index` are separate since they
/// carry an integer argument (handled directly in buildStageIODecorations),
/// as does `Location` (already an ODS attribute on `GlobalVariableOp`).
constexpr StageIODecoration StageIOFlagDecorations[] = {
    {"no_perspective", 13}, {"flat", 14},   {"patch", 15},
    {"centroid", 16},       {"sample", 17}, {"per_primitive_ext", 5271},
};

/// Builds the getStageIODecorationsAttrName() attribute for \p Op -- an
/// `Input`/`Output` variable that is not one of the compute builtins
/// `BuiltInMappings` legalizes to an `llvm.spv.*` intrinsic -- from its
/// `BuiltIn`/`Location`/`Component`/`Index`/`Offset`/`XfbBuffer`/
/// `XfbStride` and boolean interpolation/per-primitive/per-patch
/// attributes (`StageIOFlagDecorations`), or a null attribute if it
/// carries none of them.
///
/// A *graphics* builtin (`Position`, `VertexIndex`, `FragCoord`, ...) is
/// ordinary interface memory here, exactly like a user varying: it has no
/// `llvm.spv.*` intrinsic to read, and the stage ABI sources it from the
/// invocation record instead (see `feme::SignatureSystemValue`). Preserving
/// its `BuiltIn` decoration (code 11) is what lets
/// `feme::graphics::CanonicalizeStagePass` recover that system-value
/// identity when it builds the entry's `feme::EntrySignature`.
mlir::ArrayAttr buildStageIODecorationsAttr(mlir::spirv::GlobalVariableOp Op) {
  mlir::Builder Builder(Op.getContext());
  llvm::SmallVector<mlir::Attribute> Decorations;

  auto addIntDecoration = [&](uint32_t Code, mlir::Attribute ValueAttr) {
    auto Value = mlir::dyn_cast_or_null<mlir::IntegerAttr>(ValueAttr);
    if (!Value)
      return;
    Decorations.push_back(Builder.getArrayAttr(
        {Builder.getI32IntegerAttr(Code),
         Builder.getI32IntegerAttr(static_cast<int32_t>(Value.getInt()))}));
  };

  // `location` is a strongly typed `GlobalVariableOp` attribute; `component`/
  // `index` are not (MLIR's SPIR-V dialect only special-cases `location`,
  // `binding`, `descriptor_set` and `built_in` -- see `SPIRV_GlobalVariableOp`
  // in `SPIRVStructureOps.td`), so those are read as plain attributes by the
  // name MLIR's deserializer would give them
  // (`llvm::convertToSnakeFromCamelCase`).
  if (std::optional<llvm::StringRef> BuiltInName = Op.getBuiltIn())
    if (std::optional<mlir::spirv::BuiltIn> BuiltIn =
            mlir::spirv::symbolizeBuiltIn(*BuiltInName))
      Decorations.push_back(Builder.getArrayAttr(
          {Builder.getI32IntegerAttr(11),
           Builder.getI32IntegerAttr(static_cast<int32_t>(*BuiltIn))}));
  addIntDecoration(30, Op.getLocationAttr());
  addIntDecoration(31, Op->getAttr("component"));
  addIntDecoration(32, Op->getAttr("index"));
  // (Roadmap H21a) `VK_EXT_transform_feedback`'s own three per-variable
  // decorations -- read the same "plain attribute, MLIR's own deserializer
  // naming" way as `component`/`index` above, since MLIR's SPIR-V dialect
  // does not special-case these either (`Deserializer.cpp`'s own
  // `Offset`/`XfbBuffer`/`XfbStride` handling stores each as a plain
  // `IntegerAttr`, exactly like `component`/`index`). Forwarded here
  // unconditionally, with no feature-bit/extension gate: this pattern
  // lowers whatever decorations a module already carries, and a module
  // that was never allowed to declare these in the first place (the
  // extension not yet being advertised, roadmap H21) simply never has
  // them to forward.
  addIntDecoration(35, Op->getAttr("offset"));
  addIntDecoration(36, Op->getAttr("xfb_buffer"));
  addIntDecoration(37, Op->getAttr("xfb_stride"));

  for (const StageIODecoration &Flag : StageIOFlagDecorations)
    if (Op->hasAttr(Flag.AttrName))
      Decorations.push_back(
          Builder.getArrayAttr({Builder.getI32IntegerAttr(Flag.Code)}));

  if (Decorations.empty())
    return nullptr;
  return Builder.getArrayAttr(Decorations);
}

/// Builds one `(code, arg...)` tuple for a single struct member's decoration
/// (see mlir::spirv::StructType::MemberDecorationInfo), in the same shape
/// buildStageIODecorationsAttr uses for a whole-variable decoration, or a
/// null attribute if \p Info's decoration is not one of the ones a stage-IO
/// interface block's own member can carry (i.e. not `MatrixStride`/
/// `ColMajor`/`RowMajor`, an ordinary UBO/SSBO struct's own layout
/// decorations, which a stage-IO struct never carries in practice, but
/// filtered defensively all the same). `Offset` is handled separately by
/// `buildMemberDecorationsAttr` below, not here -- see its own comment for
/// why (mlir::spirv::StructType models a member's byte offset as a
/// first-class field, `getMemberOffset`, never as one of the
/// `MemberDecorationInfo` entries this function's caller iterates).
mlir::Attribute buildMemberDecorationTuple(
    mlir::Builder &Builder,
    const mlir::spirv::StructType::MemberDecorationInfo &Info) {
  switch (Info.decoration) {
  case mlir::spirv::Decoration::BuiltIn:
  case mlir::spirv::Decoration::Location:
  case mlir::spirv::Decoration::Component:
  case mlir::spirv::Decoration::Index: {
    auto Value =
        mlir::dyn_cast_or_null<mlir::IntegerAttr>(Info.decorationValue);
    if (!Value)
      return nullptr;
    return Builder.getArrayAttr(
        {Builder.getI32IntegerAttr(static_cast<int32_t>(Info.decoration)),
         Builder.getI32IntegerAttr(static_cast<int32_t>(Value.getInt()))});
  }
  case mlir::spirv::Decoration::NoPerspective:
  case mlir::spirv::Decoration::Flat:
  case mlir::spirv::Decoration::Patch:
  case mlir::spirv::Decoration::Centroid:
  case mlir::spirv::Decoration::Sample:
  // (Roadmap H93) A mesh entry's own `perprimitiveEXT` interface block
  // (e.g. `gl_MeshPerPrimitiveEXT`, holding builtins like `gl_PrimitiveID`
  // as well as any user-defined per-primitive varying) gets this
  // decoration as an `OpMemberDecorate`, exactly like `Patch` on a
  // tessellation entry's own per-patch block -- not as a whole-variable
  // `OpDecorate` the way `buildStageIODecorationsAttr` reads it for an
  // ordinary (non-block) per-primitive varying. Omitting it here left
  // every member of such a block (including builtins) silently
  // classified `SignatureFrequency::PerVertex` by
  // `CanonicalizeStage.cpp`'s `classifySPIRVElement`, so a mesh shader's
  // `gl_PrimitiveID` store got lowered through the wrong (per-vertex)
  // output storage, clamped to the single declared vertex slot -- found
  // via `dEQP-VK.mesh_shader.ext.properties.max_mesh_output_
  // primitives_256`, whose 128 odd-numbered primitive IDs came back
  // unset because every primitive's `gl_PrimitiveID` aliased onto the
  // same one-slot vertex-output location instead of its own 256-slot
  // primitive-output one.
  case mlir::spirv::Decoration::PerPrimitiveEXT:
    return Builder.getArrayAttr(
        {Builder.getI32IntegerAttr(static_cast<int32_t>(Info.decoration))});
  default:
    return nullptr;
  }
}

/// Builds the getStageIOMemberDecorationsAttrName() attribute for \p Struct
/// -- a builtin interface block's own field struct (e.g. `gl_PerVertex`'s
/// `{Position, PointSize, ClipDistance, CullDistance}`), or a plain
/// user-defined multi-member interface block's own field struct (e.g.
/// `out BlockB { vec2 a; ivec2 b[3]; } blockB;`) -- from its members' own
/// `OpMemberDecorate`d decorations
/// (mlir::spirv::StructType::getMemberDecorations, already used by
/// isBufferBlockWritable above for a storage-buffer block's `NonWritable`
/// member decoration) plus each member's own byte `Offset`
/// (`StructType::getMemberOffset`, tracked separately from the generic
/// decoration list -- see `buildMemberDecorationTuple`'s own comment), or a
/// null attribute if the struct has no members at all.
///
/// (Roadmap H101b) A plain user-defined block like `BlockB` above carries
/// no per-member `BuiltIn`/`Location` at all -- only `Offset` (each
/// member's byte position within the block) and `RelaxedPrecision` (which
/// FeMe's model has no representation for) -- so before this fix, every
/// member of such a block produced an empty `Tuples` list and was skipped
/// below entirely, same as a struct with literally no decorated members.
/// `Members` then came back empty too, so no `feme.spirv.MemberDecorations`
/// metadata was ever attached, and `CanonicalizeStage.cpp`'s `addElements`
/// treated the whole multi-member struct as a single opaque stage-IO
/// element instead of decomposing it per member. `resolveRowComponent`
/// (CanonicalizeStage.cpp) has no case for a genuine multi-member
/// `StructType` (only single-member-struct-wrapped scalars/vectors/
/// arrays), so every store's byte offset silently resolved to the same
/// `(Row=0, Component=0)`, all four of this shader's stores collapsing
/// onto the same two shadow allocas and tripping `PromoteMem2Reg`'s
/// `isAllocaPromotable` assertion on the resulting mixed-type stores --
/// found via `dEQP-VK.transform_feedback.fuzz.random_geometry.
/// all_instance_array.75`. Emitting each member's own `Offset` (decoration
/// code 35, the same code `parseSPIRVDecorations` in CanonicalizeStage.cpp
/// already reads as `XfbOffset` when it appears on a whole variable rather
/// than a member) unconditionally -- not gated behind
/// `buildMemberDecorationTuple` recognizing some *other* decoration first
/// -- ensures `Members` is never empty for any multi-member struct, however
/// plainly it's decorated.
/// Each entry is `(memberIndex, tuples)`, where `tuples` is an `ArrayAttr`
/// of buildMemberDecorationTuple's own per-decoration shape, plus one
/// synthesized `(35, byteOffset)` tuple in the same shape for the member's
/// own `Offset`.
mlir::ArrayAttr buildMemberDecorationsAttr(mlir::spirv::StructType Struct) {
  mlir::Builder Builder(Struct.getContext());
  llvm::SmallVector<mlir::Attribute> Members;
  unsigned NumMembers = Struct.getNumElements();
  for (unsigned Index = 0; Index != NumMembers; ++Index) {
    llvm::SmallVector<mlir::spirv::StructType::MemberDecorationInfo, 2>
        Decorations;
    Struct.getMemberDecorations(Index, Decorations);

    llvm::SmallVector<mlir::Attribute> Tuples;
    for (const auto &Decoration : Decorations)
      if (mlir::Attribute Tuple =
              buildMemberDecorationTuple(Builder, Decoration))
        Tuples.push_back(Tuple);
    if (Struct.hasOffset())
      Tuples.push_back(Builder.getArrayAttr(
          {Builder.getI32IntegerAttr(
               static_cast<int32_t>(mlir::spirv::Decoration::Offset)),
           Builder.getI32IntegerAttr(
               static_cast<int32_t>(Struct.getMemberOffset(Index)))}));
    // (Roadmap H114) A plain (non-`Block`) multi-member struct used
    // directly as a tessellation-control/tessellation-evaluation
    // `patch`/per-vertex stage-IO variable's type (e.g. `patch out S {
    // int x; vec4 y; } s;`) carries *no* decoration at all on any member
    // -- neither `Offset` (that's only ever emitted for a `Block`-
    // decorated interface block, `Struct.hasOffset()`) nor `Location`
    // (SPIR-V leaves every member's own location to be derived
    // sequentially from the whole variable's single `Location`, exactly
    // like `CanonicalizeStage.cpp`'s own `TakeBlockPath` fallback already
    // computes for a `Block`'s own undecorated members). Skipping every
    // such entry (the pre-existing `Tuples.empty()` behavior) left
    // `Members` empty for the whole struct, so no
    // `feme.spirv.MemberDecorations` metadata was ever attached and
    // `CanonicalizeStage.cpp`'s `addElements` fell through to the plain
    // (single-`SignatureElement`) path -- silently merging every member
    // into one `(Row=0, Component=0)` shadow slot regardless of type,
    // tripping `PromoteMem2Reg`'s `isAllocaPromotable` assertion on a
    // struct whose members' types differ (e.g. `int`/`vec4`) -- found via
    // `dEQP-VK.tessellation.user_defined_io.per_patch.
    // vertex_io_array_size_implicit.isolines`. A genuinely single-member
    // struct is deliberately left alone here (matches
    // `CanonicalizeStage.cpp`'s own single-member exclusion, roadmap
    // H101b): there is nothing to decompose when there's only one
    // member, and always emitting an entry for it would wrongly divert a
    // real array-of-block-instances shape onto the per-member path
    // instead of staying on the array-peeling one below it.
    if (Tuples.empty() && NumMembers <= 1)
      continue;
    Members.push_back(Builder.getArrayAttr(
        {Builder.getI32IntegerAttr(static_cast<int32_t>(Index)),
         Builder.getArrayAttr(Tuples)}));
  }
  if (Members.empty())
    return nullptr;
  return Builder.getArrayAttr(Members);
}

/// Converts a non-builtin `Input`/`Output` `spirv.GlobalVariable` -- an
/// ordinary vertex/fragment/etc. stage-IO variable, as opposed to a `BuiltIn`
/// one (BuiltInGlobalVariablePattern) -- to an `llvm.mlir.global` in the
/// address space LLVM's SPIRV backend expects that storage class to use (7
/// for `Input`, 8 for `Output`; see `storageClassToAddressSpace` in
/// `llvm/lib/Target/SPIRV/SPIRVUtils.h`), matching how
/// PushConstantGlobalVariablePattern handles the one other storage class
/// MLIR's own `GlobalVariablePattern` does not support -- unlike that one,
/// though, MLIR's `GlobalVariablePattern` *does* claim `Input`/`Output`
/// already, just at address space 0 (its `storageClassToAddressSpace`
/// overload is Vulkan-unaware), so this needs a higher benefit to win over
/// it here too. `BuiltIn`/`Location`/`Component`/`Index`/interpolation/
/// per-primitive/
/// per-patch decorations are preserved as a getStageIODecorationsAttrName()
/// attribute (see buildStageIODecorationsAttr), which
/// feme::spirv::attachStageIODecorations later turns into real
/// `!spirv.Decorations` metadata once a genuine `llvm::Module` exists.
/// Returns the address space a stage-IO variable's storage class converts
/// to (7 for `Input`, 8 for `Output` -- see
/// `storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`), or
/// `std::nullopt` if \p Op is not one (a different storage class, or a
/// *compute* builtin variable, which converts through
/// BuiltInAddressOfPattern/BuiltInGlobalVariablePattern instead -- a
/// graphics builtin such as `Position` has no `llvm.spv.*` intrinsic and is
/// ordinary interface memory here, see buildStageIODecorationsAttr).
std::optional<unsigned>
getStageIOAddressSpace(mlir::spirv::GlobalVariableOp Op) {
  auto SrcType = mlir::cast<mlir::spirv::PointerType>(Op.getType());
  mlir::spirv::StorageClass SC = SrcType.getStorageClass();
  if (SC != mlir::spirv::StorageClass::Input &&
      SC != mlir::spirv::StorageClass::Output)
    return std::nullopt;
  if (isValueModeledBuiltIn(Op))
    return std::nullopt;
  return SC == mlir::spirv::StorageClass::Input ? 7 : 8;
}

class StageIOGlobalVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GlobalVariableOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GlobalVariableOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<unsigned> AddrSpace = getStageIOAddressSpace(Op);
    if (!AddrSpace)
      return Rewriter.notifyMatchFailure(Op,
                                         "not a non-builtin stage-IO variable");

    auto SrcType = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    mlir::Type DstType =
        getTypeConverter()->convertType(SrcType.getPointeeType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    // `Input` is read-only from the shader's point of view (nothing lowers a
    // store into one); `Output` is written but never read back, matching how
    // MLIR's own `GlobalVariablePattern` treats these two storage classes.
    bool IsConstant =
        SrcType.getStorageClass() == mlir::spirv::StorageClass::Input;
    auto NewGlobal = Rewriter.replaceOpWithNewOp<mlir::LLVM::GlobalOp>(
        Op, DstType, IsConstant, mlir::LLVM::Linkage::External, Op.getSymName(),
        mlir::Attribute(), /*alignment=*/0, *AddrSpace);

    if (mlir::ArrayAttr Decorations = buildStageIODecorationsAttr(Op))
      NewGlobal->setAttr(feme::spirv::getStageIODecorationsAttrName(),
                         Decorations);

    // A builtin interface block (e.g. `gl_PerVertex`) has no whole-variable
    // `BuiltIn` attribute of its own -- SPIR-V decorates its members
    // individually -- but those per-member decorations are recovered here
    // the same way (roadmap H2c). A geometry entry's own per-vertex block
    // (`gl_in[]`) takes this same shape one array dimension further out --
    // an `ArrayType` of the block struct rather than the bare struct --
    // since SPIR-V still decorates the inner struct's own members, not the
    // array wrapping it; `CanonicalizeStage.cpp`'s own `addElements` already
    // peels that outer array dimension back off when reading this same
    // metadata (roadmap H5b), so it is attached here unconditionally,
    // keyed off the inner struct regardless of which of these two shapes
    // wraps it (roadmap H5g).
    mlir::Type PointeeType = SrcType.getPointeeType();
    if (auto ArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(PointeeType))
      PointeeType = ArrayTy.getElementType();
    if (auto Struct = mlir::dyn_cast<mlir::spirv::StructType>(PointeeType))
      if (mlir::ArrayAttr MemberDecorations =
              buildMemberDecorationsAttr(Struct))
        NewGlobal->setAttr(feme::spirv::getStageIOMemberDecorationsAttrName(),
                           MemberDecorations);
    return mlir::success();
  }
};

/// Replaces `spirv.mlir.addressof` of a non-builtin stage-IO variable.
///
/// For an `Output` variable this produces `llvm.mlir.addressof` of the
/// `llvm.mlir.global` StageIOGlobalVariablePattern converts its declaration
/// to, in the matching address space (8) -- see
/// feme::spirv::populateSPIRVToLLVMTargetTypeConversions.
///
/// For a scalar/vector-typed `Input` variable, whose pointer converts to
/// its pointee type instead (the same conversion a builtin `Input`
/// variable's pointer uses, there being no way to tell the two apart by
/// type alone), this instead loads the global eagerly right here, producing
/// that pointee-typed value directly: LoadValuePattern then collapses the
/// real `spirv.Load` reading it into the identity, exactly as it already
/// does for a builtin's `llvm.spv.*` intrinsic result.
///
/// (Roadmap H7y) An *array*-typed `Input` variable -- a geometry or
/// tessellation entry's own per-vertex-arrayed `gl_in[]` (block or plain),
/// or a fragment-stage's own standalone `gl_ClipDistance`/`gl_CullDistance`
/// read (roadmap H7x) -- is deliberately left as a real pointer instead,
/// exactly like the `Output` case above: a real shader may index such an
/// array with a genuinely dynamic, loop-carried value (`gl_in[i]`), which
/// has no representation as an `llvm.extractvalue` index (constant only)
/// at all -- eagerly loading it into a value, as H7x's own since-removed
/// `StageIOArrayAccessChainPattern` first tried to patch around for the
/// constant-index case only, cannot support that shape by construction, no
/// matter how many more patterns are added on top. Left as a pointer, an
/// `spirv.AccessChain` into it -- with a constant or dynamic leading index,
/// and any number of further constant ones selecting a builtin interface
/// block's own member or a matrix row -- is legalized by this file's own
/// `StageIOArrayAccessChainPattern` (below), not MLIR's own generic
/// `AccessChainPattern`: that generic pattern computes its own result type
/// by re-converting the access chain's *leaf* SPIR-V pointer type through
/// this same type converter, which -- for a scalar/vector leaf -- still
/// answers with the eagerly-loaded-value convention described just above
/// (there being no way to tell a true standalone scalar `Input` variable's
/// own pointer apart from an access-chain leaf pointer by type alone), so
/// it would build an ill-typed `getelementptr` whose result is a value, not
/// the real pointer this array's own base actually is;
/// `feme::graphics::CanonicalizeStagePass`'s own
/// `getDynamicVertexIndexedAccess`/`getDynamicRowIndexedAccess` are written
/// to expect exactly this real-pointer shape (see their own comments).
///
/// (Roadmap H87/H82) Returns true if \p PointeeType is a `spirv.array`, or
/// any `spirv.struct` (regardless of member count) -- both shapes a real
/// shader may subsequently index with a `spirv.AccessChain` rather than
/// read as a single whole value. This originally only recognized a plain
/// array, or a single-member struct wrapping one (the shape a mesh
/// shader's own per-primitive/per-vertex flat-array `Input` interface
/// block takes, SPIR-V's `Block` decoration requiring an interface block
/// to be a struct even when it logically holds nothing but one array), but
/// a genuine multi-member `Input` interface block with no array at all --
/// e.g. a fragment stage's own `in PerPrimitiveEXT { float a; vec3 b;
/// float c; }`-shaped read of a mesh shader's per-primitive output, each
/// member selected by its own constant-indexed `spirv.AccessChain` -- needs
/// exactly the same real-pointer treatment: `spirv.AccessChain`'s own
/// member-selecting indices are always compile-time constant, so this
/// shape does not strictly need it to support a *dynamic* index the way
/// the array shapes do, but it still needs a real pointer base because
/// MLIR's own generic `AccessChainPattern` (see `StageIOArrayAccessChainPattern`'s
/// own comment below) unconditionally builds a `getelementptr`, which
/// requires a pointer operand, not the previously-eagerly-loaded struct
/// value this predicate answering `false` for a multi-member struct used
/// to produce.
bool isCompositeStageIOType(mlir::Type PointeeType) {
  return mlir::isa<mlir::spirv::ArrayType, mlir::spirv::StructType>(
      PointeeType);
}

/// LLVM-dialect counterpart of isCompositeStageIOType, applied to an
/// already-converted type (see StageIOAddressOfPattern's own use, below).
bool isCompositeLLVMType(mlir::Type Type) {
  return mlir::isa<mlir::LLVM::LLVMArrayType, mlir::LLVM::LLVMStructType>(
      Type);
}

/// \p StageIOVariables must have been collected by
/// feme::spirv::prepareStageIOVariables, before the conversion ran: by the
/// time an `Input`/`Output` variable's own use is legalized, an earlier
/// sibling `spirv.GlobalVariable` in the same block (this one included) may
/// already have converted, so looking its address space back up through
/// the (possibly by-then-replaced) declaration -- the way
/// BuiltInAddressOfPattern/ResourceAddressOfPattern look up their own
/// declarations -- is not reliable here (see prepareStageIOVariables).
class StageIOAddressOfPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AddressOfOp> {
public:
  StageIOAddressOfPattern(mlir::MLIRContext *Context,
                          const mlir::LLVMTypeConverter &TypeConverter,
                          mlir::PatternBenefit Benefit,
                          const feme::spirv::StageIOInfoMap &StageIOVariables)
      : mlir::SPIRVToLLVMConversion<mlir::spirv::AddressOfOp>(
            Context, TypeConverter, Benefit),
        StageIOVariables(StageIOVariables) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AddressOfOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto It = StageIOVariables.find(Op.getVariable());
    if (It == StageIOVariables.end())
      return Rewriter.notifyMatchFailure(Op,
                                         "not a non-builtin stage-IO variable");
    unsigned AddrSpace = It->second;

    mlir::Location Loc = Op.getLoc();
    mlir::Type PtrType =
        mlir::LLVM::LLVMPointerType::get(Rewriter.getContext(), AddrSpace);
    mlir::Value Address = mlir::LLVM::AddressOfOp::create(
        Rewriter, Loc, PtrType, Op.getVariable());

    if (AddrSpace == 8) {
      Rewriter.replaceOp(Op, Address);
      return mlir::success();
    }

    auto PointerTy = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    mlir::Type ValueType =
        getTypeConverter()->convertType(PointerTy.getPointeeType());
    if (!ValueType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    // (Roadmap H7y/H87/H82) An array- or struct-typed `Input` variable --
    // any shape that a `spirv.AccessChain` may subsequently index into --
    // stays a real pointer instead of an eagerly-loaded value -- see this
    // class's own comment and isCompositeStageIOType/isCompositeLLVMType
    // above.
    if (isCompositeLLVMType(ValueType)) {
      Rewriter.replaceOp(Op, Address);
      return mlir::success();
    }

    Rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(Op, ValueType, Address);
    return mlir::success();
  }

private:
  const feme::spirv::StageIOInfoMap &StageIOVariables;
};

/// Returns true if \p Op's base operand is an `Input`-storage-class
/// pointer to a composite (array or struct) type (see
/// isCompositeStageIOType, and StageIOAddressOfPattern's own comment for
/// why such a variable stays a real pointer, and
/// StageIOArrayAccessChainPattern below for what this identifies it for).
bool isInputArrayAccessChain(mlir::spirv::AccessChainOp Op) {
  auto BaseType =
      mlir::dyn_cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());
  return BaseType &&
         BaseType.getStorageClass() == mlir::spirv::StorageClass::Input &&
         isCompositeStageIOType(BaseType.getPointeeType());
}

/// (Roadmap H7y) Converts a `spirv.AccessChain` whose base operand is a
/// real pointer into an array-typed `Input` variable (see
/// StageIOAddressOfPattern's own comment for why such a base stays a real
/// pointer rather than an eagerly-loaded value) into an ordinary,
/// pointer-result `getelementptr` -- with a constant or a genuinely dynamic
/// leading (per-vertex) index, and any number of further indices selecting
/// a builtin interface block's own member or a matrix row, uniformly.
///
/// This exists as its own pattern, rather than deferring to MLIR's own
/// generic `AccessChainPattern`, only because that pattern computes its
/// own *result* type by re-converting the access chain's leaf SPIR-V
/// pointer type through the same type converter -- which, for a
/// scalar/vector leaf, still answers with the "eagerly-loaded value"
/// conversion a genuinely standalone scalar `Input` variable's own address
/// needs (there being no way to tell the two apart by type alone once
/// nested this deeply). Since this array's own base is a real pointer, not
/// a value, that would build an ill-typed `getelementptr` whose declared
/// result is a value type instead of the real pointer it must be. Building
/// the `getelementptr` directly here, with an explicit real-pointer result
/// type in the array's own address space, sidesteps that ambiguity for the
/// `getelementptr` itself; the `spirv.Load` that always follows it still
/// sees this same ambiguity (the leaf pointer type's declared/"legalized"
/// conversion says a value, this pattern's own real result says a
/// pointer), which is what the target materialization registered in
/// feme::spirv::populateSPIRVToLLVMTargetTypeConversions resolves, by
/// reading through the real pointer with an ordinary `llvm.load` whenever
/// the dialect conversion framework needs a value of the "expected"
/// (eagerly-loaded-value) type but only has this real pointer on hand.
class StageIOArrayAccessChainPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::AccessChainOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AccessChainOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!isInputArrayAccessChain(Op))
      return Rewriter.notifyMatchFailure(
          Op, "not an access chain into an array-typed Input variable");
    auto BaseType =
        mlir::cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());

    if (!mlir::isa<mlir::LLVM::LLVMPointerType>(
            Adaptor.getBasePtr().getType()))
      return Rewriter.notifyMatchFailure(Op,
                                         "base did not convert to a pointer");

    mlir::Type ElementType =
        getTypeConverter()->convertType(BaseType.getPointeeType());
    if (!ElementType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    llvm::SmallVector<mlir::Value, 4> Indices;
    mlir::Type IndexType =
        getTypeConverter()->convertType(Op.getIndices().front().getType());
    if (!IndexType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    Indices.push_back(mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, IndexType, Rewriter.getIntegerAttr(IndexType, 0)));
    llvm::append_range(Indices, Adaptor.getIndices());

    mlir::Type ResultType = mlir::LLVM::LLVMPointerType::get(
        Rewriter.getContext(),
        mlir::cast<mlir::LLVM::LLVMPointerType>(
            Adaptor.getBasePtr().getType())
            .getAddressSpace());
    Rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
        Op, ResultType, ElementType, Adaptor.getBasePtr(), Indices);
    return mlir::success();
  }
};

/// Converts a load whose "pointer" operand already converted to the loaded
/// value itself, which is how the SPIR-V constructs LLVM models as values
/// rather than as memory (builtin input variables) reach their uses: the load
/// is then nothing but the identity.
class LoadValuePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::LoadOp> {
public:
  using mlir::SPIRVToLLVMConversion<mlir::spirv::LoadOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::LoadOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Value Pointer = Adaptor.getPtr();
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(Pointer.getType()))
      return Rewriter.notifyMatchFailure(Op, "an ordinary memory load");
    if (Pointer.getType() != getTypeConverter()->convertType(Op.getType()))
      return Rewriter.notifyMatchFailure(Op, "loaded type does not match");
    Rewriter.replaceOp(Op, Pointer);
    return mlir::success();
  }
};

/// Converts `spirv.AccessChain` selecting a single lane of a builtin
/// `Input` vector variable (e.g. `gl_GlobalInvocationID.x`) -- the shape
/// glslang emits when only one component of such a variable is ever read,
/// distinct from the whole-vector load BuiltInAddressOfPattern/
/// LoadValuePattern already handle. That base operand converts to the
/// vector value itself rather than to a real pointer (see
/// BuiltInAddressOfPattern), so MLIR's own `AccessChainPattern` -- which
/// assumes any base operand converts to `!llvm.ptr` -- cannot handle it: it
/// would build a `getelementptr` treating that raw vector as if it were a
/// pointer instead. This rewrites the access chain directly to an
/// `llvm.extractelement`, mirroring how MatrixCompositeExtractPattern
/// selects one lane of a value-modeled matrix column rather than
/// navigating real memory. The `spirv.Load` that always follows such an
/// access chain then collapses to the identity via LoadValuePattern, the
/// same as a direct load of the whole builtin variable already does.
class BuiltInAccessChainPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::AccessChainOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AccessChainOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto VectorTy =
        mlir::dyn_cast<mlir::VectorType>(Adaptor.getBasePtr().getType());
    if (!VectorTy)
      return Rewriter.notifyMatchFailure(
          Op, "base is not a value-modeled builtin vector");

    mlir::ValueRange Indices = Adaptor.getIndices();
    if (Indices.size() != 1)
      return Rewriter.notifyMatchFailure(Op, "expected a single lane index");

    mlir::Type ResultType =
        getTypeConverter()->convertType(Op.getComponentPtr().getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    if (ResultType != VectorTy.getElementType())
      return Rewriter.notifyMatchFailure(Op, "not selecting a scalar lane");

    Rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractElementOp>(
        Op, ResultType, Adaptor.getBasePtr(), Indices[0]);
    return mlir::success();
  }
};

/// Replaces `spirv.mlir.addressof` of a resource variable with the
/// `llvm.spv.resource.handlefrombinding` call producing its handle. As for
/// builtin variables, there is no LLVM global to address: LLVM's SPIRV
/// backend emits the `OpVariable` and its `DescriptorSet`/`Binding`
/// decorations from the intrinsic, so `!spirv.ptr<image, UniformConstant>`
/// converts to the handle type itself.
///
/// An array-of-blocks or array-of-resources (roadmap L12a) variable's own
/// address is simply erased instead: its handle needs which descriptor to
/// bind, only known at its own access chain's leading (array) index -- see
/// ArrayedBlockAccessChainPattern/ResourceArrayAccessChainPattern, which
/// build that handle themselves and are the only legal use of such a
/// variable's address (a whole descriptor array is never itself loaded or
/// stored as a value). `Count == 0` (an *unbounded* array-of-resources,
/// `getArrayedResourceCount`'s own reserved sentinel) takes this same
/// path, not the single-handle one below: an unbounded array's handle
/// still needs a real access-chain index, exactly like a bounded one, just
/// with no compile-time-known upper bound on it.
class ResourceAddressOfPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AddressOfOp> {
public:
  ResourceAddressOfPattern(mlir::MLIRContext *Context,
                           const mlir::LLVMTypeConverter &TypeConverter,
                           mlir::PatternBenefit Benefit,
                           const feme::spirv::ResourceInfoMap &Resources)
      : mlir::SPIRVToLLVMConversion<mlir::spirv::AddressOfOp>(
            Context, TypeConverter, Benefit),
        Resources(Resources) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AddressOfOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto It = Resources.find(Op.getVariable());
    if (It == Resources.end())
      return Rewriter.notifyMatchFailure(Op, "not a resource variable");
    // (Roadmap H130) Whether this variable is arrayed at all is a
    // structural property of its own declared type -- an `ArrayType`/
    // `RuntimeArrayType` pointee -- not something `It->second.Count`'s
    // *value* can answer alone: a real single-element array (e.g. `T
    // blocks[1];`, one of `dEQP-VK.ubo.random.basic_instance_arrays`'s own
    // randomly-sized cases) still declares an array pointee, and
    // `getArrayedBlockCount`/`getArrayedResourceCount`
    // (populateSPIRVToLLVMTargetTypeConversions's own resource-collection
    // loop) correctly records its real length -- 1 -- as `Count`, exactly
    // like a genuinely non-arrayed variable's `Count` defaults to 1 too.
    // The old `It->second.Count != 1` check could not tell these two cases
    // apart and built a single, non-arrayed handle directly from this
    // op's own (array-typed) pointer type instead of erasing it for
    // ArrayedBlockAccessChainPattern/ResourceArrayAccessChainPattern to
    // handle -- `getTypeConverter()->convertType` below cannot produce a
    // real `spirv.VulkanBuffer` handle for an array-of-`StructType`/
    // array-of-resource pointee at all, and silently falls back to a raw
    // `ptr`, surfacing later as `UnsupportedOps.cpp`'s own generic "cannot
    // normalize" diagnostic on a handle that may not even be the one whose
    // actual access triggered the failure (see that diagnostic's own
    // comment).
    mlir::Type Pointee =
        mlir::cast<mlir::spirv::PointerType>(Op.getType()).getPointeeType();
    if (mlir::isa<mlir::spirv::ArrayType, mlir::spirv::RuntimeArrayType>(
            Pointee)) {
      Rewriter.eraseOp(Op);
      return mlir::success();
    }

    mlir::Type HandleType = getTypeConverter()->convertType(Op.getType());
    if (!HandleType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I32 = Rewriter.getI32Type();
    llvm::SmallVector<mlir::Value, 5> Args;
    Args.push_back(mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, I32, static_cast<int32_t>(It->second.DescriptorSet)));
    Args.push_back(mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, I32, static_cast<int32_t>(It->second.Binding)));
    // A non-arrayed `spirv.GlobalVariable` declares exactly one resource,
    // so the binding holds a single descriptor and the index into it is
    // always zero.
    Args.push_back(mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, 1));
    Args.push_back(mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, 0));
    Args.push_back(mlir::LLVM::AddressOfOp::create(
        Rewriter, Loc, mlir::LLVM::LLVMPointerType::get(Rewriter.getContext()),
        It->second.NameSymbol));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc,
                                "llvm.spv.resource.handlefrombinding",
                                HandleType, Args));
    return mlir::success();
  }

private:
  const feme::spirv::ResourceInfoMap &Resources;
};

/// Drops a resource variable's declaration; the handle intrinsic
/// ResourceAddressOfPattern emits carries the whole declaration with it.
class ResourceGlobalVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp> {
public:
  ResourceGlobalVariablePattern(mlir::MLIRContext *Context,
                                const mlir::LLVMTypeConverter &TypeConverter,
                                mlir::PatternBenefit Benefit,
                                const feme::spirv::ResourceInfoMap &Resources)
      : mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp>(
            Context, TypeConverter, Benefit),
        Resources(Resources) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GlobalVariableOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!Resources.count(Op.getSymName()))
      return Rewriter.notifyMatchFailure(Op, "not a resource variable");
    Rewriter.eraseOp(Op);
    return mlir::success();
  }

private:
  const feme::spirv::ResourceInfoMap &Resources;
};

/// Returns the constant integer value of \p Index, an `spirv.AccessChain`
/// index selecting a struct member -- always an `spirv.Constant` per the
/// SPIR-V spec, unlike an array/vector/matrix-selecting index, which may be
/// a genuine runtime value -- or `std::nullopt` if it is not one (a
/// malformed module this declines to convert rather than miscompiles).
std::optional<uint64_t> getConstantMemberIndex(mlir::Value Index) {
  auto Constant = Index.getDefiningOp<mlir::spirv::ConstantOp>();
  if (!Constant)
    return std::nullopt;
  auto IntAttr = mlir::dyn_cast<mlir::IntegerAttr>(Constant.getValue());
  if (!IntAttr)
    return std::nullopt;
  return IntAttr.getValue().getZExtValue();
}

/// Shared by BlockAccessChainPattern (a plain block) and
/// ArrayedBlockAccessChainPattern (one element of an array-of-blocks
/// binding) below: builds the `llvm.spv.resource.getpointer` call selecting
/// \p AllIndices[Selector] of \p Element's content from \p Handle, then an
/// ordinary GEP for any indices beyond it -- see BlockElement's own comment
/// for what that selector means in each shape.
/// Forward declaration: defined below, used by rewriteBlockAccess to
/// reject (rather than silently miscompile) a partial access into a
/// matrix member whose declared layout it does not represent naturally
/// -- see its own definition's comment.
bool isMatrixMemberLayoutRepresentable(mlir::spirv::StructType Struct,
                                       unsigned Index,
                                       mlir::Type ConvertedMember);

/// Forward declaration: defined below, alongside
/// isMatrixMemberLayoutRepresentable, whose own decoration-inspecting
/// body this factors out (roadmap H132) -- used by rewriteBlockAccess,
/// which (unlike isMatrixMemberLayoutRepresentable's own other callers)
/// already knows \p Index's own member is a matrix by the time it needs
/// this answer, however many array levels of a dxc wrapper (see
/// BlockElement's own comment) it took to reach it: \p Index always
/// names the matrix's own *declaring* struct member (decorations are
/// attached there regardless of how many array levels wrap the matrix
/// itself), even when \p ConvertedMember is the matrix's own natural
/// conversion recovered from further inside that member's own type, not
/// `Struct.getElementType(Index)` directly.
bool isMatrixLayoutRepresentable(mlir::spirv::StructType Struct,
                                 unsigned Index, mlir::Type ConvertedMember);

/// Forward declaration: defined below (alongside
/// convertOffsetStructTypeIgnoringDecorations, whose own struct-layout
/// decision this recovers), used by rewriteBlockAccess to remap a
/// `spirv.AccessChain`'s own declared struct-member selector into the
/// real physical LLVM field index a reordered-and/or-padded struct's
/// conversion actually placed it at -- see the definition's own comment
/// for why a member selector cannot just be forwarded unchanged the way
/// it can for a struct needing no such remapping.
unsigned getStructMemberPhysicalIndex(mlir::spirv::StructType Struct,
                                      unsigned DeclaredIndex,
                                      const mlir::TypeConverter &Converter);

/// Forward declaration: defined below (alongside
/// getStructMemberPhysicalIndex, which this calls once per struct level
/// reached) -- extends that single-level remap (roadmap H129) to as many
/// *further* levels of nested struct members as \p Op's own indices
/// actually reach (roadmap H133): a struct member that is itself a
/// reordered/padded struct, itself containing a member that is itself
/// such a struct, and so on, rather than just the first selector past
/// whichever selector the caller (OffsetStructMemberReorderAccessChain
/// Pattern, rewriteBlockAccess) already remapped on its own. Used by
/// both, each of which otherwise only remapped its own leading
/// member-selector and forwarded every further index unchanged --
/// exactly wrong once a member navigated by one of those further
/// indices is itself a reordered/padded struct.
bool remapNestedStructMemberIndices(
    mlir::Type CurrentType, mlir::spirv::AccessChainOp Op, unsigned StartIndex,
    const mlir::TypeConverter &Converter,
    mlir::ConversionPatternRewriter &Rewriter,
    llvm::SmallVectorImpl<mlir::Value> &Indices);

/// Forward declaration: defined below (alongside
/// getPhysicalMatrixMemberType, whose own \p IsRowMajor/\p Stride fields
/// this shares), used by rewriteBlockAccess (roadmap H129) to recover a
/// non-representable matrix member's own `RowMajor`/`MatrixStride`
/// decorations directly, to compute a column-select access's own address
/// (ColMajor) or decide to defer to MatrixColumnLoadPattern/
/// MatrixColumnStorePattern (RowMajor) -- see rewriteBlockAccess's own
/// comment for the column-select shape this recovers the decorations for.
/// The struct itself (not just its own declaration) has to be defined
/// this early, rather than merely forward-declared: rewriteBlockAccess
/// below actually calls getMatrixMemberLayout and inspects its own
/// `std::optional<MatrixMemberLayout>` result, which needs
/// `MatrixMemberLayout` complete at that call site.
struct MatrixMemberLayout {
  bool IsRowMajor = false;
  uint64_t Stride = 0;
};
std::optional<MatrixMemberLayout>
getMatrixMemberLayout(mlir::spirv::StructType Struct, unsigned Index);

/// \p BlockStruct is the struct Element was itself derived from (see
/// BlockElement's own comment) -- always a plain struct type, whether or
/// not `Element.HasWrapper`, since both getBufferBlockElement and
/// getUniformBlockElement only ever construct a BlockElement after first
/// confirming their own input pointer's pointee is one. It is threaded
/// through explicitly, rather than re-derived from `Op.getBasePtr()`'s own
/// type here, because BlockAccessChainPattern's and
/// ArrayedBlockAccessChainPattern's own base pointers point at different
/// things -- a struct directly for the former, but an *array* of structs
/// (one instance per array element) for the latter, whose own element
/// type (not `Op.getBasePtr()`'s own pointee) is what Element was actually
/// derived from.
mlir::LogicalResult rewriteBlockAccess(
    mlir::spirv::AccessChainOp Op, mlir::ConversionPatternRewriter &Rewriter,
    const mlir::TypeConverter &TypeConverter, const BlockElement &Element,
    mlir::spirv::StructType BlockStruct, mlir::Value Handle,
    mlir::ValueRange AllIndices, unsigned Selector) {
  mlir::Type ResultType =
      TypeConverter.convertType(Op.getComponentPtr().getType());
  if (!ResultType)
    return Rewriter.notifyMatchFailure(Op, "type conversion failed");

  mlir::Location Loc = Op.getLoc();
  // (Roadmap H129) `AllIndices[Selector]`'s own value -- whatever
  // `spirv.AccessChain`'s original, *declared* member selector converted
  // to -- only ever means the right thing to
  // `llvm.spv.resource.getpointer` when Element.Content is not itself a
  // struct needing reordering/padding (see
  // convertOffsetStructTypeIgnoringDecorations): a runtime/fixed array's
  // own element is the same regardless of which index selects it, so
  // there is nothing to remap in that shape, but a struct's declared
  // member index must be translated to its own real physical field index
  // first, exactly as an ordinary `spirv.AccessChain` into a
  // (non-handle) struct already has to be (see
  // OffsetStructMemberReorderAccessChainPattern) -- without this, a
  // getpointer whose target struct needed any reordering/padding at all
  // would silently address the wrong member instead of failing loudly,
  // since both an original and a remapped index are always in-bounds
  // integers `llvm.spv.resource.getpointer` accepts unquestioningly.
  mlir::Value GetPointerIndex = AllIndices[Selector];
  if (auto ContentStruct =
          mlir::dyn_cast<mlir::spirv::StructType>(Element.Content)) {
    std::optional<uint64_t> DeclaredIndex =
        getConstantMemberIndex(Op.getIndices()[Selector]);
    if (!DeclaredIndex)
      return Rewriter.notifyMatchFailure(Op,
                                         "member selector is not a constant");
    unsigned PhysicalIndex = getStructMemberPhysicalIndex(
        ContentStruct, static_cast<unsigned>(*DeclaredIndex), TypeConverter);
    if (PhysicalIndex != *DeclaredIndex) {
      mlir::Type LLVMIndexType = GetPointerIndex.getType();
      GetPointerIndex = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, LLVMIndexType,
          Rewriter.getIntegerAttr(LLVMIndexType, PhysicalIndex));
    }
  }
  mlir::Value ElementPtr =
      createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.getpointer",
                          ResultType, {Handle, GetPointerIndex});
  if (AllIndices.size() == Selector + 1) {
    Rewriter.replaceOp(Op, ElementPtr);
    return mlir::success();
  }

  // Further indices navigate what `llvm.spv.resource.getpointer` just
  // selected. Element.Content is either a homogeneous, dynamically-indexed
  // array (either a storage buffer's own runtime array, or a uniform
  // buffer's own fixed-size one -- see BlockElement's own comment -- in
  // either case reached through the wrapper shape or the direct one),
  // whose every element shares one type regardless of which one Selector
  // names; or a struct (a non-array uniform block's content, in either
  // shape), whose member Selector names varies per member and -- like any
  // struct-member-selecting SPIR-V index -- is always a compile-time
  // constant, so it has to be read to recover the right one. The leading 0
  // dereferences through the pointer `llvm.spv.resource.getpointer`
  // returned, exactly as an ordinary GEP into a pointer operand would.
  mlir::Type SelectedType;
  // (Roadmap H124b) The struct member whose own `RowMajor`/`MatrixStride`
  // decorations describe SelectedType's real layout, when SelectedType
  // turns out to be a matrix -- always member 0 of Op's own base struct
  // for either array-wrapped shape below (the wrapper's sole member,
  // whatever its element type), or the constant member index the
  // non-array (direct struct-content) branch already has to read anyway.
  unsigned MatrixDecorationMemberIndex = 0;
  if (auto Array =
          mlir::dyn_cast<mlir::spirv::RuntimeArrayType>(Element.Content)) {
    SelectedType = Array.getElementType();
  } else if (auto FixedArray =
                 mlir::dyn_cast<mlir::spirv::ArrayType>(Element.Content)) {
    SelectedType = FixedArray.getElementType();
  } else {
    std::optional<uint64_t> MemberIndex =
        getConstantMemberIndex(Op.getIndices()[Selector]);
    if (!MemberIndex)
      return Rewriter.notifyMatchFailure(Op,
                                         "member selector is not a constant");
    SelectedType = mlir::cast<mlir::spirv::StructType>(Element.Content)
                       .getElementType(*MemberIndex);
    MatrixDecorationMemberIndex = static_cast<unsigned>(*MemberIndex);
  }
  mlir::Type ElementType = TypeConverter.convertType(SelectedType);
  if (!ElementType)
    return Rewriter.notifyMatchFailure(Op, "type conversion failed");

  // (Roadmap H124b) A matrix reached through more than just the member
  // selector above -- e.g. one row/column or scalar element of it, rather
  // than the matrix in its own entirety (see getMatrixWholeAccess's own
  // comment for the one shape this file does support) -- whose own
  // declared layout isMatrixMemberLayoutRepresentable rejects (a
  // `RowMajor` matrix, physically transposed from ElementType's own
  // logical, always column-major shape, or a `MatrixStride` this
  // member's own physically substituted layout pads between
  // rows/columns) cannot be addressed by an ordinary GEP at all: it would
  // compute byte offsets from ElementType's own natural (unpadded,
  // logical) layout, not the member's real physical one.
  //
  // (Roadmap H129) SPIR-V's own matrix indexing is always column-first
  // regardless of physical RowMajor/ColMajor storage (a `spirv.Matrix`
  // is always modeled as an array of column vectors at the type-system
  // level -- see the SPIR-V spec's own `OpTypeMatrix`), so exactly one
  // further index past the member selector always selects one whole
  // logical column (`matrix[col]`, a `vector<NumRows x T>`), never a row
  // -- confirmed empirically too: every one of this roadmap entry's own
  // 236 real `dEQP-VK.ubo.*` failures had exactly this shape. The two
  // physical-major-axis cases need genuinely different fixes:
  //   - ColMajor: one logical column IS one physical major entry -- a
  //     single, contiguous, `Layout->Stride`-sized block -- so
  //     `member_base + col * Stride` bytes is exactly its address. This
  //     target's own vector ABI size for 2/3/4 lanes never exceeds a
  //     real `MatrixStride` reservation (verified against every real
  //     case this fix was developed against), so an ordinary
  //     `spirv.Load`/`spirv.Store` of that address as `vector<NumRows x
  //     T>` (ResultType, already this op's own correctly converted
  //     result type) reads/writes the right bytes directly -- no
  //     dedicated Load/Store pattern is needed for this half at all.
  //   - RowMajor: one logical column is scattered across `NumRows`
  //     separate physical row entries, each `Stride` bytes apart -- no
  //     single pointer can describe it, so this AccessChain instead
  //     just returns the member's own base address unconverted
  //     (`ElementPtr`, exactly like the whole-matrix case above), the
  //     same "deferred address" trick that case already relies on for
  //     RowMajorMatrixLoadPattern/StorePattern -- MatrixColumnLoadPattern/
  //     MatrixColumnStorePattern below do the actual gather/scatter.
  //
  // A further, *scalar-element* access (the member selector, a column
  // index, and a further row index -- `matrix[col][row]`, a scalar `T`)
  // is simpler than the column case for *both* majors: unlike a whole
  // column, one scalar element is always exactly one entry of one
  // physical major entry -- i.e. always directly, statically addressable
  // by a single GEP through the same `!llvm.array<MajorCount x
  // MajorEntryTy>` physical substitution getPhysicalMatrixMemberType's
  // own whole-matrix conversion already uses (see its own comment for
  // `MajorEntryTy`'s own shape) -- with `col`/`row` simply swapped into
  // major/minor position opposite each other between RowMajor and
  // ColMajor (`spirv.AccessChain`'s own index order is always [col, row]
  // regardless of physical majorness, since SPIR-V's own matrix indexing
  // is always column-first -- see this function's own comment above).
  //
  // A row-select access (without a further scalar index) is not this
  // shape and remains declined below (not observed in any real
  // `dEQP-VK.ubo.*` case: SPIR-V's own column-first indexing means an
  // access chain can only ever reach a row by first fully decomposing to
  // a scalar element, one row/column index at a time, not a row
  // directly).
  if (mlir::isa<mlir::spirv::MatrixType>(SelectedType)) {
    // (Roadmap H132) Call isMatrixLayoutRepresentable directly here, not
    // isMatrixMemberLayoutRepresentable: SelectedType is already
    // confirmed to be the matrix itself (the condition above), however
    // many array levels of a dxc wrapper it took to reach it (see
    // BlockElement's own comment) -- isMatrixMemberLayoutRepresentable's
    // own redundant re-check of `Struct.getElementType(Index)` directly
    // would incorrectly see the wrapper's own array type there instead
    // of the matrix, always answering "representable" for a wrapper-shape
    // member regardless of its real RowMajor/MatrixStride decorations,
    // silently miscompiling this partial access instead of correctly
    // handling or declining it.
    if (!isMatrixLayoutRepresentable(BlockStruct, MatrixDecorationMemberIndex,
                                     ElementType)) {
      std::optional<MatrixMemberLayout> Layout =
          getMatrixMemberLayout(BlockStruct, MatrixDecorationMemberIndex);
      if (Layout && AllIndices.size() == Selector + 2) {
        if (!Layout->IsRowMajor) {
          mlir::Type ByteTy = mlir::IntegerType::get(Rewriter.getContext(), 8);
          auto StrideBlockTy =
              mlir::LLVM::LLVMArrayType::get(ByteTy, Layout->Stride);
          mlir::Value ColumnPtr = mlir::LLVM::GEPOp::create(
              Rewriter, Loc, ResultType, StrideBlockTy, ElementPtr,
              llvm::ArrayRef<mlir::LLVM::GEPArg>{AllIndices[Selector + 1]},
              mlir::LLVM::GEPNoWrapFlags::inbounds);
          Rewriter.replaceOp(Op, ColumnPtr);
          return mlir::success();
        }
        // The RowMajor deferral below only works for the direct
        // (`cbuffer`/`ConstantBuffer<T>`, non-wrapper) shape:
        // MatrixColumnLoadPattern/StorePattern's own getMatrixColumnAccess
        // helper only recognizes that shape (see its own comment), so a
        // wrapper-shape RowMajor column-select falls through to the
        // decline below instead of deferring to a pattern that would
        // never actually match it -- silently leaving a wrong,
        // unresolved address is worse than declining.
        if (!Element.HasWrapper) {
          Rewriter.replaceOp(Op, ElementPtr);
          return mlir::success();
        }
      }
      if (Layout && AllIndices.size() == Selector + 3) {
        auto MatrixSpirvTy = mlir::cast<mlir::spirv::MatrixType>(SelectedType);
        mlir::Type ScalarTy =
            TypeConverter.convertType(MatrixSpirvTy.getElementType());
        if (!ScalarTy)
          return Rewriter.notifyMatchFailure(Op, "type conversion failed");
        mlir::DataLayout DL;
        int64_t MajorCount = Layout->IsRowMajor
                                 ? MatrixSpirvTy.getNumRows()
                                 : MatrixSpirvTy.getNumColumns();
        int64_t MinorCount = Layout->IsRowMajor
                                 ? MatrixSpirvTy.getNumColumns()
                                 : MatrixSpirvTy.getNumRows();
        uint64_t ElemSize = DL.getTypeSize(ScalarTy);
        uint64_t NaturalMinorBytes =
            static_cast<uint64_t>(MinorCount) * ElemSize;
        bool NeedsPad = Layout->Stride != NaturalMinorBytes;
        auto MinorArrTy = mlir::LLVM::LLVMArrayType::get(ScalarTy, MinorCount);
        mlir::Type MajorEntryTy = MinorArrTy;
        if (NeedsPad) {
          auto PadTy = mlir::LLVM::LLVMArrayType::get(
              mlir::IntegerType::get(Rewriter.getContext(), 8),
              Layout->Stride - NaturalMinorBytes);
          MajorEntryTy = mlir::LLVM::LLVMStructType::getLiteral(
              Rewriter.getContext(), {MinorArrTy, PadTy}, /*isPacked=*/true);
        }
        auto PhysicalArrTy =
            mlir::LLVM::LLVMArrayType::get(MajorEntryTy, MajorCount);
        mlir::Value ColIdx = AllIndices[Selector + 1];
        mlir::Value RowIdx = AllIndices[Selector + 2];
        mlir::Value MajorIdx = Layout->IsRowMajor ? RowIdx : ColIdx;
        mlir::Value MinorIdx = Layout->IsRowMajor ? ColIdx : RowIdx;
        llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
        GEPIndices.push_back(0);
        GEPIndices.push_back(MajorIdx);
        if (NeedsPad)
          GEPIndices.push_back(0);
        GEPIndices.push_back(MinorIdx);
        mlir::Value ScalarPtr = mlir::LLVM::GEPOp::create(
            Rewriter, Loc, ResultType, PhysicalArrTy, ElementPtr, GEPIndices,
            mlir::LLVM::GEPNoWrapFlags::inbounds);
        Rewriter.replaceOp(Op, ScalarPtr);
        return mlir::success();
      }
      return Rewriter.notifyMatchFailure(
          Op, "partial access (a row select) into a matrix member whose "
              "declared RowMajor/MatrixStride layout is not yet "
              "supported");
    }
  }

  // (Roadmap H133) SelectedType's own further indices may themselves
  // select into a member of a further reordered/padded struct, nested
  // more than one level below Element.Content -- remap every one of
  // those exactly as Selector's own selector already was above.
  llvm::SmallVector<mlir::Value, 4> RemappedIndices(AllIndices.begin(),
                                                    AllIndices.end());
  if (!remapNestedStructMemberIndices(SelectedType, Op, Selector + 1,
                                      TypeConverter, Rewriter, RemappedIndices))
    return Rewriter.notifyMatchFailure(
        Op, "nested struct member selector is not a constant");

  llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
  GEPIndices.push_back(0);
  llvm::append_range(GEPIndices,
                     llvm::ArrayRef(RemappedIndices).drop_front(Selector + 1));
  Rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
      Op, ResultType, ElementType, ElementPtr, GEPIndices,
      mlir::LLVM::GEPNoWrapFlags::inbounds);
  return mlir::success();
}

/// Converts `spirv.AccessChain` into a storage/uniform buffer block whose
/// base pointer converted to a `spirv.VulkanBuffer` handle rather than an
/// ordinary LLVM pointer (see feme::spirv::getBufferBlockElement/
/// getUniformBlockElement), which MLIR's own `AccessChainPattern` cannot
/// handle since it assumes its base pointer converts to `!llvm.ptr`.
///
/// The wrapper shape's leading index -- the member selector into FeMe's own
/// single-member wrapper, always the constant 0 -- is dropped; the shape
/// glslang emits directly has no such index to drop, and its own leading
/// index becomes the real selector instead (see BlockElement's comment for
/// both shapes). Either way, that selector becomes
/// `llvm.spv.resource.getpointer`'s index, and any further indices navigate
/// the selected content's own fields/elements with an ordinary
/// `llvm.getelementptr`, matching how real `dxc`-compiled SPIR-V is
/// expected to lower on LLVM's SPIRV backend (see
/// `llvm/test/CodeGen/SPIRV/pointers/structured-buffer-access.ll`). An
/// array-of-blocks pointer's own access chain is handled by
/// ArrayedBlockAccessChainPattern instead, since its handle needs a runtime
/// index this pattern's own base pointer -- already converted to a handle
/// with no such index -- cannot supply.
class BlockAccessChainPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::AccessChainOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AccessChainOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto HandleType = mlir::dyn_cast<mlir::LLVM::LLVMTargetExtType>(
        Adaptor.getBasePtr().getType());
    if (!HandleType || HandleType.getExtTypeName() != "spirv.VulkanBuffer")
      return Rewriter.notifyMatchFailure(Op, "not a block access");

    auto PointerType =
        mlir::cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());
    std::optional<BlockElement> Element = getBufferBlockElement(PointerType);
    if (!Element)
      Element = getUniformBlockElement(PointerType);
    if (!Element)
      return Rewriter.notifyMatchFailure(Op, "not a block pointer");

    mlir::ValueRange Indices = Adaptor.getIndices();
    unsigned Selector = Element->HasWrapper ? 1 : 0;
    if (Indices.size() <= Selector)
      return Rewriter.notifyMatchFailure(Op, "not enough indices");

    return rewriteBlockAccess(
        Op, Rewriter, *getTypeConverter(), *Element,
        mlir::cast<mlir::spirv::StructType>(PointerType.getPointeeType()),
        Adaptor.getBasePtr(), Indices, Selector);
  }
};

/// Converts `spirv.AccessChain` into an array-of-blocks pointer (`T
/// blocks[N]` in GLSL) -- a single binding covering `N` descriptors, each
/// its own storage/uniform buffer block instance -- building the
/// `spirv.VulkanBuffer` handle itself, unlike BlockAccessChainPattern
/// above: an arrayed block's handle needs *which* descriptor to bind, only
/// known once this access chain's own leading index (the array index) is
/// available, whereas a non-arrayed block's handle needs no runtime
/// information the type converter cannot already supply on its own.
/// `spirv.mlir.addressof`'s own conversion (ResourceAddressOfPattern)
/// erases the op instead of building a (necessarily incomplete) handle for
/// it, since its only use is always an access chain like this one.
class ArrayedBlockAccessChainPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp> {
public:
  ArrayedBlockAccessChainPattern(mlir::MLIRContext *Context,
                                 const mlir::LLVMTypeConverter &TypeConverter,
                                 mlir::PatternBenefit Benefit,
                                 const feme::spirv::ResourceInfoMap &Resources)
      : mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp>(
            Context, TypeConverter, Benefit),
        Resources(Resources) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AccessChainOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto AddrOf = Op.getBasePtr().getDefiningOp<mlir::spirv::AddressOfOp>();
    if (!AddrOf)
      return Rewriter.notifyMatchFailure(Op, "base is not a variable address");
    auto It = Resources.find(AddrOf.getVariable());
    if (It == Resources.end())
      return Rewriter.notifyMatchFailure(Op, "not a resource");

    // (Roadmap H130) Whether this variable is an array of block instances
    // at all is a structural property of its own declared type -- an
    // `spirv::ArrayType` pointee -- not something `It->second.Count`'s
    // *value* can answer on its own: a real `T blocks[1];` (a legal, if
    // unusual, single-element instance array -- e.g. one of
    // `dEQP-VK.ubo.random.basic_instance_arrays`'s own randomly-sized
    // cases) still declares an `OpTypeArray`, and `getArrayedBlockCount`
    // (populateSPIRVToLLVMTargetTypeConversions's own resource-collection
    // loop) correctly records its real length -- 1 -- as `Count`, exactly
    // like a genuinely non-arrayed block's `Count` defaults to 1 too. The
    // old `It->second.Count <= 1` guard could not tell these two cases
    // apart and silently declined the arrayed one, falling through to the
    // ordinary (non-arrayed) `spirv::PointerType` conversion further down
    // in this file, which cannot handle an `ArrayType`-of-`StructType`
    // pointee at all and silently produces a raw `ptr` handle instead of a
    // `spirv.VulkanBuffer` one -- surfacing many bindings/instructions
    // later as `UnsupportedOps.cpp`'s own generic "cannot normalize"
    // diagnostic, on a handle that may not even be the one whose actual
    // access triggered the failure (see that diagnostic's own comment).
    auto PointerType = mlir::cast<mlir::spirv::PointerType>(AddrOf.getType());
    auto Array =
        mlir::dyn_cast<mlir::spirv::ArrayType>(PointerType.getPointeeType());
    if (!Array)
      return Rewriter.notifyMatchFailure(Op, "not an arrayed block");

    auto ElementPointerType = mlir::spirv::PointerType::get(
        Array.getElementType(), PointerType.getStorageClass());

    std::optional<BlockElement> Element =
        getBufferBlockElement(ElementPointerType);
    if (!Element)
      Element = getUniformBlockElement(ElementPointerType);
    if (!Element)
      return Rewriter.notifyMatchFailure(Op, "not a block pointer");

    // The leading index selects which descriptor of the array to bind; the
    // rest apply to that descriptor's own block content exactly like
    // BlockAccessChainPattern's own selector, shifted over by that one
    // extra leading index.
    mlir::ValueRange Indices = Adaptor.getIndices();
    unsigned Selector = Element->HasWrapper ? 2 : 1;
    if (Indices.size() <= Selector)
      return Rewriter.notifyMatchFailure(Op, "not enough indices");

    mlir::Type HandleType = getTypeConverter()->convertType(ElementPointerType);
    if (!HandleType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I32 = Rewriter.getI32Type();
    mlir::Value Handle = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.resource.handlefrombinding", HandleType,
        {mlir::LLVM::ConstantOp::create(
             Rewriter, Loc, I32,
             static_cast<int32_t>(It->second.DescriptorSet)),
         mlir::LLVM::ConstantOp::create(
             Rewriter, Loc, I32, static_cast<int32_t>(It->second.Binding)),
         mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32,
                                        static_cast<int32_t>(It->second.Count)),
         Indices[0],
         mlir::LLVM::AddressOfOp::create(
             Rewriter, Loc,
             mlir::LLVM::LLVMPointerType::get(Rewriter.getContext()),
             It->second.NameSymbol)});

    return rewriteBlockAccess(
        Op, Rewriter, *getTypeConverter(), *Element,
        mlir::cast<mlir::spirv::StructType>(ElementPointerType.getPointeeType()),
        Handle, Indices, Selector);
  }

private:
  const feme::spirv::ResourceInfoMap &Resources;
};

/// (Roadmap L12a) Converts `spirv.AccessChain` into an array-of-resources
/// pointer (`RWBuffer<T> Buf[N]`, `Texture2D Tex[]`, or any other image/
/// sampled-image/sampler array, bounded or unbounded) directly into the
/// `llvm.spv.resource.handlefrombinding` call producing that one element's
/// own handle -- mirroring ArrayedBlockAccessChainPattern immediately
/// above, but simpler: an array-of-*resources* element is itself the whole
/// handle (an opaque value, never memory), not a block whose *content*
/// still needs its own further byte-offset indexing once the right
/// descriptor is selected, so this pattern replaces the access chain with
/// the handle value directly rather than delegating to
/// rewriteBlockAccess. Any subsequent `spirv.Load` of that value converts
/// to the identity via LoadValuePattern, exactly like a non-arrayed
/// resource's own `spirv.mlir.addressof`-then-`spirv.Load` pair already
/// does (see ResourceAddressOfPattern's own comment).
///
/// Before this pattern, indexing any array of resources -- bounded or
/// unbounded -- fell through to MLIR's own generic, lower-benefit
/// `AccessChainPattern`, which treats every `spirv.AccessChain` as an
/// ordinary in-memory offset computation: it converted the array-of-
/// resources global into an `!llvm.array<N x target<...>>`/
/// `!llvm.array<0 x target<...>>` (for a `spirv.rtarray`) and then emitted
/// an `llvm.getelementptr` indexing directly into it, immediately rejected
/// by the LLVM dialect's own GEP verifier ("result #0 must be LLVM pointer
/// type ..., but got '!llvm.target<...>'") since a resource handle is not
/// a pointer at all -- the exact failure roadmap milestone L12 names.
class ResourceArrayAccessChainPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp> {
public:
  ResourceArrayAccessChainPattern(
      mlir::MLIRContext *Context, const mlir::LLVMTypeConverter &TypeConverter,
      mlir::PatternBenefit Benefit,
      const feme::spirv::ResourceInfoMap &Resources)
      : mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp>(
            Context, TypeConverter, Benefit),
        Resources(Resources) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AccessChainOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto AddrOf = Op.getBasePtr().getDefiningOp<mlir::spirv::AddressOfOp>();
    if (!AddrOf)
      return Rewriter.notifyMatchFailure(Op, "base is not a variable address");
    auto It = Resources.find(AddrOf.getVariable());
    if (It == Resources.end())
      return Rewriter.notifyMatchFailure(Op, "not a resource");

    // (Roadmap H130) Do not gate on `It->second.Count == 1` here: a real
    // `Texture2D Tex[1];`/`RWBuffer<T> Buf[1];` (a legal, if unusual,
    // single-element resource array) is still an array structurally,
    // and `getArrayedResourceCount` immediately below already answers
    // that question correctly and structurally (an `ArrayType`/
    // `RuntimeArrayType` pointee) -- see
    // ArrayedBlockAccessChainPattern's own identical fix, immediately
    // above, for the analogous array-of-*blocks* bug this mirrors.
    auto PointerType = mlir::cast<mlir::spirv::PointerType>(AddrOf.getType());
    std::optional<uint32_t> ArrayedCount =
        getArrayedResourceCount(PointerType);
    if (!ArrayedCount)
      return Rewriter.notifyMatchFailure(Op, "not an array of resources");

    mlir::Type ElementType =
        mlir::isa<mlir::spirv::RuntimeArrayType>(PointerType.getPointeeType())
            ? mlir::cast<mlir::spirv::RuntimeArrayType>(
                  PointerType.getPointeeType())
                  .getElementType()
            : mlir::cast<mlir::spirv::ArrayType>(PointerType.getPointeeType())
                  .getElementType();
    auto ElementPointerType =
        mlir::spirv::PointerType::get(ElementType, PointerType.getStorageClass());

    mlir::ValueRange Indices = Adaptor.getIndices();
    if (Indices.size() != 1)
      return Rewriter.notifyMatchFailure(
          Op, "expected a single array-selecting index");

    mlir::Type HandleType = getTypeConverter()->convertType(ElementPointerType);
    if (!HandleType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I32 = Rewriter.getI32Type();
    Rewriter.replaceOp(
        Op, createIntrinsicCall(
                Rewriter, Loc, "llvm.spv.resource.handlefrombinding",
                HandleType,
                {mlir::LLVM::ConstantOp::create(
                     Rewriter, Loc, I32,
                     static_cast<int32_t>(It->second.DescriptorSet)),
                 mlir::LLVM::ConstantOp::create(
                     Rewriter, Loc, I32,
                     static_cast<int32_t>(It->second.Binding)),
                 mlir::LLVM::ConstantOp::create(
                     Rewriter, Loc, I32,
                     static_cast<int32_t>(It->second.Count)),
                 Indices[0],
                 mlir::LLVM::AddressOfOp::create(
                     Rewriter, Loc,
                     mlir::LLVM::LLVMPointerType::get(Rewriter.getContext()),
                     It->second.NameSymbol)}));
    return mlir::success();
  }

private:
  const feme::spirv::ResourceInfoMap &Resources;
};

/// Returns false if \p Struct's member \p Index -- already known to be a
/// matrix, at whatever array-wrapping depth its own decorations still
/// describe (see isMatrixLayoutRepresentable's own forward-declaration
/// comment for why this differs from isMatrixMemberLayoutRepresentable
/// below, its sole other caller) -- is decorated `RowMajor` -- a physical
/// layout transposed from the logical column-major type LLVM's own
/// natural array-of-column-vectors representation always uses (see the
/// `spirv.MatrixType` conversion in
/// populateSPIRVToLLVMTargetTypeConversions), which reinterpreting the
/// same bytes cannot reproduce -- or decorated `MatrixStride` with a
/// value other than \p ConvertedMember's own natural per-column stride
/// (the size of one column, since LLVM array elements pack with no
/// interior padding); true otherwise.
bool isMatrixLayoutRepresentable(mlir::spirv::StructType Struct,
                                 unsigned Index, mlir::Type ConvertedMember) {
  llvm::SmallVector<mlir::spirv::StructType::MemberDecorationInfo, 2>
      Decorations;
  Struct.getMemberDecorations(Index, Decorations);
  mlir::DataLayout DL;
  auto ArrayTy = mlir::cast<mlir::LLVM::LLVMArrayType>(ConvertedMember);
  uint64_t NaturalStride = DL.getTypeSize(ArrayTy.getElementType());
  for (const auto &Decoration : Decorations) {
    if (Decoration.decoration == mlir::spirv::Decoration::RowMajor)
      return false;
    if (Decoration.decoration != mlir::spirv::Decoration::MatrixStride)
      continue;
    auto StrideAttr =
        mlir::dyn_cast<mlir::IntegerAttr>(Decoration.decorationValue);
    if (!StrideAttr ||
        static_cast<uint64_t>(StrideAttr.getInt()) != NaturalStride)
      return false;
  }
  return true;
}

/// Returns false if \p Struct's member \p Index is *directly* a matrix
/// (not, e.g., a dxc wrapper's array of one -- see isMatrixLayoutRepresentable's
/// own comment for that shape, which rewriteBlockAccess checks instead)
/// decorated `RowMajor`/non-natural `MatrixStride` (see
/// isMatrixLayoutRepresentable, which this defers to once it has
/// confirmed \p Index is a matrix at all); true for every other member,
/// including one that is not a matrix, direct or wrapped, at all.
bool isMatrixMemberLayoutRepresentable(mlir::spirv::StructType Struct,
                                       unsigned Index,
                                       mlir::Type ConvertedMember) {
  if (!mlir::isa<mlir::spirv::MatrixType>(Struct.getElementType(Index)))
    return true;
  return isMatrixLayoutRepresentable(Struct, Index, ConvertedMember);
}

/// (MatrixMemberLayout's own struct definition now lives with its
/// forward declaration, above rewriteBlockAccess -- see that comment for
/// why -- rather than here.)

/// Returns \p Struct's member \p Index's own `RowMajor`/`MatrixStride`
/// decorations (see MatrixMemberLayout), or `std::nullopt` if it has no
/// `MatrixStride` decoration at all -- a matrix member always needs one
/// to be laid out in memory in the first place, so this indicates a
/// malformed module this declines to convert (matches
/// isMatrixMemberLayoutRepresentable's own "reject" behavior for the same
/// input, which every real caller of this function has already checked).
std::optional<MatrixMemberLayout>
getMatrixMemberLayout(mlir::spirv::StructType Struct, unsigned Index) {
  llvm::SmallVector<mlir::spirv::StructType::MemberDecorationInfo, 2>
      Decorations;
  Struct.getMemberDecorations(Index, Decorations);
  MatrixMemberLayout Layout;
  bool HasStride = false;
  for (const auto &Decoration : Decorations) {
    if (Decoration.decoration == mlir::spirv::Decoration::RowMajor) {
      Layout.IsRowMajor = true;
    } else if (Decoration.decoration ==
               mlir::spirv::Decoration::MatrixStride) {
      auto StrideAttr =
          mlir::dyn_cast<mlir::IntegerAttr>(Decoration.decorationValue);
      if (!StrideAttr)
        return std::nullopt;
      Layout.Stride = static_cast<uint64_t>(StrideAttr.getInt());
      HasStride = true;
    }
  }
  if (!HasStride)
    return std::nullopt;
  return Layout;
}

/// Returns the "physical" LLVM type substituting for a `spirv.MatrixType`
/// struct member whenever isMatrixMemberLayoutRepresentable rejects its
/// ordinary (logical, always column-major, tightly packed) conversion --
/// an `!llvm.array<MajorCount x MajorEntryTy>`, where MajorCount and
/// MajorEntryTy's own element count are chosen by \p Layout's own
/// `RowMajor`/`ColMajor`-ness (RowMajor: MajorCount = NumRows, one entry
/// holds NumColumns scalars; ColMajor: MajorCount = NumColumns, one entry
/// holds NumRows scalars -- the "major" axis is whichever one is
/// physically contiguous in memory), and MajorEntryTy is padded (a
/// trailing byte array, packed) up to \p Layout's own declared
/// `MatrixStride` whenever the tightly-packed natural size of one major
/// entry undershoots it -- real HLSL cbuffer packing reserves a whole
/// 16-byte register for every row/column regardless of its own true
/// element count (Roadmap H124b).
///
/// This substituted type does not attempt to reproduce the matrix's
/// *logical* (always column-major) shape at all -- unlike the ordinary
/// MatrixType conversion, whose `!llvm.array<NumColumns x
/// vector<NumRows>>` every other pattern in this file already assumes --
/// so nothing besides a pointer (GEP address) computation may use it
/// directly. getMatrixWholeAccess below (used by
/// RowMajorMatrixLoadPattern/StorePattern) recomputes this exact same
/// physical shape independently, from the same struct member
/// decorations, and transposes/depads to/from the ordinary logical
/// representation at the moment a real `spirv.Load`/`spirv.Store`
/// crosses this member's address boundary -- see their own comments.
///
/// Returns null if \p MatrixTy's own element type fails to convert, or if
/// \p Layout's declared `MatrixStride` is smaller than one major entry's
/// own natural (tightly packed) size -- a genuinely overlapping,
/// unrepresentable layout, not a mere padding gap.
mlir::Type getPhysicalMatrixMemberType(mlir::spirv::MatrixType MatrixTy,
                                       const MatrixMemberLayout &Layout,
                                       const mlir::TypeConverter &Converter,
                                       mlir::DataLayout &DL) {
  mlir::Type ElemTy = Converter.convertType(MatrixTy.getElementType());
  if (!ElemTy)
    return nullptr;
  uint64_t ElemSize = DL.getTypeSize(ElemTy);
  int64_t MajorCount =
      Layout.IsRowMajor ? MatrixTy.getNumRows() : MatrixTy.getNumColumns();
  int64_t MinorCount =
      Layout.IsRowMajor ? MatrixTy.getNumColumns() : MatrixTy.getNumRows();
  uint64_t NaturalMinorBytes = static_cast<uint64_t>(MinorCount) * ElemSize;
  if (Layout.Stride < NaturalMinorBytes)
    return nullptr; // Overlapping rows/columns: genuinely unrepresentable.

  auto MinorArrTy = mlir::LLVM::LLVMArrayType::get(ElemTy, MinorCount);
  mlir::Type MajorEntryTy = MinorArrTy;
  if (Layout.Stride != NaturalMinorBytes) {
    auto PadTy = mlir::LLVM::LLVMArrayType::get(
        mlir::IntegerType::get(MatrixTy.getContext(), 8),
        Layout.Stride - NaturalMinorBytes);
    MajorEntryTy = mlir::LLVM::LLVMStructType::getLiteral(
        MatrixTy.getContext(), {MinorArrTy, PadTy}, /*isPacked=*/true);
  }
  return mlir::LLVM::LLVMArrayType::get(MajorEntryTy, MajorCount);
}

/// (Roadmap H101j) Name prefix `getTightVectorArrayType`'s own marker
/// struct (see its comment) uses, mirrored by
/// `CanonicalizeStage.cpp`'s own `isTightVectorMarkerStruct` -- kept as a
/// single shared literal (duplicated, not included from a common header,
/// since these two files belong to different, independently-linked
/// components with no existing shared-header precedent for this small a
/// constant) so a rename of one side is caught by the other's own tests
/// failing to recognize the marker at all, rather than silently
/// mismatching.
constexpr llvm::StringLiteral kTightVectorMarkerName = "feme.tight_vector";

/// If \p Ty is one of `getTightVectorArrayType`'s own marker structs,
/// returns its one member's own (tight array) type; otherwise returns
/// null. Lets a consumer that needs to see *through* the marker (e.g.
/// `CompositeConstructPattern`'s own struct case below, reassembling a
/// real vector constituent into a tight-substituted member) recognize it
/// positively, rather than assuming any `LLVM::LLVMArrayType` struct
/// member must itself directly be the tight-substituted array (true
/// before this roadmap item, no longer true now that the marker wraps
/// it).
mlir::Type getTightVectorMarkerInnerType(mlir::Type Ty) {
  auto StructTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(Ty);
  if (!StructTy || !StructTy.isIdentified() ||
      !StructTy.getName().starts_with(kTightVectorMarkerName) ||
      StructTy.getBody().size() != 1)
    return nullptr;
  return StructTy.getBody()[0];
}

/// Returns the LLVM array type substituting for a SPIR-V vector-typed
/// struct member whenever that member's own natural (ABI-alignment-driven)
/// layout cannot reproduce a declared offset -- see the "tight-vector
/// retry" in convertOffsetStructTypeIgnoringDecorations below for why this
/// is needed at all. An LLVM `VectorType`'s own ABI size/alignment (see
/// `getDefaultTypeSizeInBits`/`getDefaultABIAlignment` in
/// `DataLayoutInterfaces.cpp`) rounds its innermost dimension up to the
/// next power of two purely for hardware SIMD-register purposes -- e.g. a
/// 3-lane vector is sized/aligned as if it were 4 lanes, and a 2-lane
/// vector's alignment as 8 bytes rather than 4 -- a rounding that
/// `-fvk-use-scalar-layout`/`-fvk-use-dx-layout`'s own tightly-packed
/// member offsets never leave room for (unlike GLSL's own std140/std430
/// base-alignment rules, which already match this rounding). An LLVM array
/// type's own size/alignment has no such rounding (its alignment is simply
/// its element's own alignment, and its size is exactly element count
/// times element size), so substituting one in reproduces the tightly
/// packed offsets `dxc` actually emits. This substitution is safe for
/// pointer-based access (a GEP into this member's address does not care
/// about its "nominal" element type, since this codebase uses opaque
/// pointers throughout) -- see CompositeConstructPattern's own struct case
/// below for the one place a *value* (not just a pointer) crosses this
/// member's boundary and needs a lane-by-lane reassembly to compensate.
/// Returns null if the vector's own element type fails to convert.
///
/// (Roadmap H101j) The substituted `array<M x Scalar>` is wrapped in a
/// uniquely-named, single-member identified struct (name prefix
/// `kTightVectorMarkerName`, mirrored in `CanonicalizeStage.cpp`'s own
/// `isTightVectorMarkerStruct`) rather than returned bare. Bit-for-bit, a
/// tight-substituted `array<M x Scalar>` is indistinguishable from a
/// genuinely-declared, directly-authored multi-dimensional scalar array
/// (e.g. `dEQP-VK.transform_feedback.fuzz.2_level_array.float`'s own
/// `float xs[2][2]`, which needs the *opposite* row/component
/// classification) -- `DataLayout` reports identical size/alignment for
/// both. `CanonicalizeStage.cpp`'s row/component-shape inference
/// (`getStageIORowShape`) and access-resolution (`resolveRowComponent`)
/// need a positive, unambiguous signal to tell the two apart; this marker
/// is that signal. `getNewIdentified` (rather than a fixed name via
/// `getIdentified`) lets each call site instantiate its own body without
/// colliding across differently-shaped substitutions in the same
/// `MLIRContext` -- the exact name does not matter beyond the shared
/// prefix, since nothing besides `isTightVectorMarkerStruct`'s own prefix
/// check ever looks at it.
mlir::Type getTightVectorArrayType(mlir::VectorType VectorTy,
                                   const mlir::TypeConverter &Converter) {
  mlir::Type ElementType = Converter.convertType(VectorTy.getElementType());
  if (!ElementType)
    return nullptr;
  mlir::Type ArrayTy =
      mlir::LLVM::LLVMArrayType::get(ElementType, VectorTy.getNumElements());
  return mlir::LLVM::LLVMStructType::getNewIdentified(
      VectorTy.getContext(), kTightVectorMarkerName, {ArrayTy});
}

/// Forward declaration: defined below. Needed by this file's own struct
/// member-conversion loop (roadmap H133), which must convert a
/// struct-typed member via this function directly rather than through
/// `Converter.convertType` -- see that loop's own comment for why:
/// MLIR's `TypeConverter` caches "context-free" conversions per raw
/// SPIR-V type, so any *other*, earlier caller reaching this exact
/// nested struct type first (transitively, for any reason) would
/// otherwise poison every later caller's own result, including this
/// loop's.
mlir::Type convertOffsetStructTypeIgnoringDecorations(
    mlir::spirv::StructType Type, const mlir::TypeConverter &Converter,
    llvm::SmallVectorImpl<unsigned> *PhysicalIndexOut);

/// Forward declaration: defined below. Needed by getTightNestedStructType
/// (roadmap H133) to reproduce an interior gap its own always-tightened
/// member list may still need between two declared offsets -- see that
/// function's own comment.
mlir::Type layOutStructIfOffsetsMatch(
    mlir::spirv::StructType Type, llvm::ArrayRef<mlir::Type> Members,
    llvm::SmallVectorImpl<unsigned> *PhysicalIndexOut, bool AllowInteriorPad);

/// Returns a "tight" (alignment-free) re-conversion of \p MatrixTy --
/// `!llvm.array<NumColumns x TightColumn>`, where `TightColumn` is
/// \p MatrixTy's own column vector re-converted via
/// `getTightVectorArrayType` -- the same substitution the array-or-matrix
/// retry tier in `convertOffsetStructTypeIgnoringDecorations` already
/// builds inline for a *direct* matrix member; factored out here so a
/// matrix nested one level inside an outer *array* member (roadmap H134,
/// e.g. `!spirv.array<5 x !spirv.matrix<4 x vector<3xf32>>>`, an array of
/// matrix instances -- as opposed to `spirv.matrix`'s own column-vector
/// array, which is a different axis) can reuse the exact same
/// substitution for its own element type. Returns null if the column
/// vector's own element type fails to convert.
mlir::Type getTightMatrixType(mlir::spirv::MatrixType MatrixTy,
                              const mlir::TypeConverter &Converter) {
  auto ColumnTy = mlir::cast<mlir::VectorType>(MatrixTy.getColumnType());
  mlir::Type TightColumnTy = getTightVectorArrayType(ColumnTy, Converter);
  if (!TightColumnTy)
    return nullptr;
  return mlir::LLVM::LLVMArrayType::get(TightColumnTy,
                                        MatrixTy.getNumColumns());
}

/// If \p ElementTy is a SPIR-V struct (any member count -- e.g.
/// `!spirv.struct<(vector<4xf32> [RelaxedPrecision])>`, or a two-member
/// `!spirv.struct<(!spirv.matrix<3 x vector<3xf32>> [RelaxedPrecision],
/// vector<4xsi32>)>`, both shapes `dEQP-VK.transform_feedback.fuzz.
/// all_unordered_and_instance_array`'s own fuzzer emits as one member of
/// an outer, explicitly byte-offset-laid-out block), returns a "tight"
/// (alignment-free) re-conversion of it, substituting every vector,
/// array-of-vector, and matrix member (recursing into any member that is
/// itself another nested struct) with the same tight form
/// `getTightVectorArrayType`'s own top-level retry already substitutes a
/// *bare* vector member with; otherwise returns null.
///
/// (Roadmap H101s) A struct like this converts to a perfectly good,
/// self-consistent LLVM struct all on its own (via this same file's
/// `spirv::StructType` conversion, invoked recursively by
/// `TypeConverter::convertType` for a nested identified struct) --
/// trivially so if it declares no per-member `Offset` at all (real SPIR-V
/// only requires one when the struct itself needs a layout, which a
/// struct nested *inside* another `Block`-decorated struct's own member
/// does not), in which case `layOutStructIfOffsetsMatch`'s own
/// `!Type.hasOffset()` early-out just accepts its natural, real-
/// vector/matrix layout unconditionally, with no chance to ever retry.
/// The mismatch only surfaces one level up: this nested struct's own
/// natural *size* and *alignment* are still driven by its real vector/
/// matrix members' ABI-rounded size/alignment (e.g. a 3-lane vector
/// rounds up to a 4-lane, 16-byte-aligned footprint), which the *outer*
/// struct's declared, tightly packed offset for this member (or the gap
/// to its following sibling) need not have reserved room for -- exactly
/// the same tight-vector problem `getTightVectorArrayType`'s own retry
/// already solves for a bare vector member, just one (or more) levels of
/// struct-wrapping away from where that retry looks by default.
///
/// Preserves \p ElementTy's own member count and order as a new literal
/// struct (rather than collapsing a single-member case into its own
/// substituted member directly): an access chain's existing "select
/// member I [of the outer struct], then select member J [of this nested
/// struct]" index pair must keep resolving to the same field it always
/// did, for every J, not just J == 0.
///
/// (Roadmap H133) Always tightens every member first (below), regardless
/// of whether \p NestedStruct declares any `Offset` at all: an *outer*
/// struct's own layout may need this nested struct's overall footprint
/// tight even when \p NestedStruct's own declared offsets (if any) are
/// already trivially satisfied by the natural, un-tightened layout (e.g.
/// its one and only member, at declared offset 0 -- the common case a
/// naive `convertOffsetStructTypeIgnoringDecorations(NestedStruct, ...)`
/// delegation would get wrong, since that function tries the natural
/// layout *first* and returns immediately once it already satisfies
/// every declared offset, with no way to know some *other*, outer
/// struct still needs the tighter footprint). Only once every member is
/// tightened does this then check whether \p NestedStruct's own
/// declared offsets need an *interior* gap the tightened member list
/// doesn't already reproduce on its own (e.g. two tightened, differently
/// -sized members declared out of natural placement order) -- inserting
/// one via `layOutStructIfOffsetsMatch`'s own interior-pad tier exactly
/// as `convertOffsetStructTypeIgnoringDecorations` would, but starting
/// from this function's own always-tight member list rather than that
/// function's natural-first one.
mlir::Type getTightNestedStructType(mlir::spirv::StructType NestedStruct,
                                    const mlir::TypeConverter &Converter) {
  llvm::SmallVector<mlir::Type, 4> TightMembers;
  for (unsigned I = 0, E = NestedStruct.getNumElements(); I != E; ++I) {
    mlir::Type ElementTy = NestedStruct.getElementType(I);
    mlir::Type MemberTy;
    if (auto VectorTy = mlir::dyn_cast<mlir::VectorType>(ElementTy)) {
      MemberTy = getTightVectorArrayType(VectorTy, Converter);
    } else if (auto MatrixTy =
                   mlir::dyn_cast<mlir::spirv::MatrixType>(ElementTy)) {
      auto ColumnTy = mlir::cast<mlir::VectorType>(MatrixTy.getColumnType());
      mlir::Type TightColumn = getTightVectorArrayType(ColumnTy, Converter);
      if (TightColumn)
        MemberTy = mlir::LLVM::LLVMArrayType::get(TightColumn,
                                                  MatrixTy.getNumColumns());
    } else if (auto ArrayTy =
                   mlir::dyn_cast<mlir::spirv::ArrayType>(ElementTy)) {
      if (auto InnerVectorTy =
              mlir::dyn_cast<mlir::VectorType>(ArrayTy.getElementType())) {
        mlir::Type TightElement =
            getTightVectorArrayType(InnerVectorTy, Converter);
        if (TightElement)
          MemberTy = mlir::LLVM::LLVMArrayType::get(
              TightElement, ArrayTy.getNumElements());
      } else {
        MemberTy = Converter.convertType(ElementTy);
      }
    } else if (auto InnerStructTy =
                   mlir::dyn_cast<mlir::spirv::StructType>(ElementTy)) {
      MemberTy = getTightNestedStructType(InnerStructTy, Converter);
    } else {
      MemberTy = Converter.convertType(ElementTy);
    }
    if (!MemberTy)
      return nullptr;
    TightMembers.push_back(MemberTy);
  }
  if (!NestedStruct.hasOffset())
    return mlir::LLVM::LLVMStructType::getLiteral(NestedStruct.getContext(),
                                                  TightMembers,
                                                  /*isPacked=*/false);
  // (Roadmap H133) Reproduce any interior gap between tightened members
  // \p NestedStruct's own declared offsets need -- the common case (no
  // gap at all) degrades to exactly the same literal struct the
  // no-offset path above would have returned.
  if (mlir::Type Result = layOutStructIfOffsetsMatch(
          NestedStruct, TightMembers, nullptr, /*AllowInteriorPad=*/false))
    return Result;
  return layOutStructIfOffsetsMatch(NestedStruct, TightMembers, nullptr,
                                    /*AllowInteriorPad=*/true);
}

/// Pads an already-converted \p Type (an array element, or a struct
/// member) up to \p TargetSize, by appending a trailing byte-array member
/// to its own body if it is itself an (already-bodied) `LLVM::LLVMStructType`
/// -- so every *existing* member index (and therefore every already-built
/// GEP navigating through it) keeps meaning exactly what it always did;
/// only a new, otherwise-unreferenced trailing member is introduced, unlike
/// wrapping \p Type in a brand-new one-member-plus-padding struct instead,
/// which would add a level of GEP nesting no real `spirv.AccessChain` index
/// sequence anticipates. This is the array-element analog of
/// convertOffsetStructTypeIgnoringDecorations's own "tight-vector retry",
/// but for the opposite direction: there, a member's *natural* layout is
/// too large relative to a declared (tighter) offset; here, it is too
/// *small* relative to a declared (larger, padded) stride or offset gap --
/// e.g. an identified struct used as a `-fvk-use-dx-layout` array element,
/// or an ordinary struct member, whose own natural size undershoots the
/// space HLSL's own per-variable/per-element "starts a fresh register"
/// packing rule actually reserves for it (roadmap L13a). See
/// convertArrayTypeIgnoringDecorations and the growth retry in
/// convertOffsetStructTypeIgnoringDecorations below for the two,
/// structurally identical, situations this is needed for.
///
/// Deliberately does *not* wrap a non-struct \p Type (a vector, array, or
/// scalar) this way: unlike a struct, it has no "own body" a new member
/// could be appended to without introducing a brand-new wrapping level no
/// real `spirv.AccessChain` index sequence anticipates, so this
/// conservatively fails (returns null) rather than risk producing a shape
/// whose GEP indices silently disagree with what a real `spirv.AccessChain`
/// expects. A scalar (or vector) array element that needs widening past
/// its own natural size instead gets a same-depth, differently-typed
/// stand-in of its own -- see `convertArrayTypeIgnoringDecorations`'s own
/// uniform byte-array substitution, and
/// `convertUndersizedScalarArrayMemberIgnoringDecorations`'s per-element
/// one (roadmap L17) -- neither of which goes through this function at
/// all. Returns \p Type unchanged if it already has exactly the right
/// size, or null if it is already too large (a genuinely unrepresentable
/// gap, e.g. the existing "member too big for its own slot" failure), if it
/// is not an already-bodied struct, or if the padded struct's own natural
/// layout does not reproduce \p TargetSize exactly (e.g. \p TargetSize is
/// not a multiple of the struct's own alignment).
mlir::Type padStructToSize(mlir::Type Type, uint64_t TargetSize,
                           mlir::DataLayout &DL) {
  uint64_t NaturalSize = DL.getTypeSize(Type);
  if (NaturalSize == TargetSize)
    return Type;
  if (NaturalSize > TargetSize)
    return nullptr;

  auto StructTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(Type);
  if (!StructTy || StructTy.isOpaque())
    return nullptr;
  // (Roadmap H101j) A `getTightVectorArrayType` marker struct wraps a
  // tight-substituted vector's own array stand-in, not a "real" struct
  // whose own body a caller may safely grow -- appending a trailing
  // byte-array member here would silently break
  // `getTightVectorMarkerInnerType`'s own "exactly one member" shape
  // check downstream, and CanonicalizeStage.cpp's own analogous
  // `isTightVectorMarkerStruct` (see either one's comment). Reject
  // exactly as this already would for a bare (unwrapped) tight array --
  // the "Deliberately does *not* wrap a non-struct Type" case above --
  // since a marker is conceptually one of those, just struct-shaped.
  if (getTightVectorMarkerInnerType(Type))
    return nullptr;

  llvm::SmallVector<mlir::Type, 4> Body(StructTy.getBody());
  Body.push_back(mlir::LLVM::LLVMArrayType::get(
      mlir::IntegerType::get(Type.getContext(), 8),
      TargetSize - NaturalSize));
  mlir::Type Padded = mlir::LLVM::LLVMStructType::getLiteral(
      Type.getContext(), Body, StructTy.isPacked());
  if (DL.getTypeSize(Padded) != TargetSize)
    return nullptr;
  return Padded;
}

/// Fills \p Out with a copy of \p Members with every member \p I (other
/// than the last, which has no declared next sibling to reserve space up
/// to) padded (see padStructToSize) up to \p Type's own declared gap to
/// member `I + 1` -- needed for a member whose natural size undershoots
/// that gap purely because HLSL's own packing rules reserve more space for
/// it than its real content occupies (e.g. an identified struct used as an
/// ordinary, non-array struct member: `X x1; X x2;` inside one `cbuffer`,
/// each `X` starting its own fresh 16-byte register regardless of its own,
/// smaller, real size). Returns false (leaving \p Out unspecified) if
/// \p Type has no offsets at all, if padding a member fails (see
/// padStructToSize), or if no member actually needed padding (a pure
/// fallback -- nothing to gain by retrying `layOutStructIfOffsetsMatch`
/// with an unchanged member list).
bool padUndersizedMembersIfNeeded(mlir::spirv::StructType Type,
                                 llvm::ArrayRef<mlir::Type> Members,
                                 llvm::SmallVectorImpl<mlir::Type> &Out) {
  if (!Type.hasOffset())
    return false;

  Out.assign(Members.begin(), Members.end());
  mlir::DataLayout DL;
  bool ChangedAny = false;
  for (unsigned I = 0, E = Out.size(); I + 1 < E; ++I) {
    uint64_t Gap = Type.getMemberOffset(I + 1) - Type.getMemberOffset(I);
    mlir::Type Padded = padStructToSize(Out[I], Gap, DL);
    if (!Padded)
      return false;
    ChangedAny |= Padded != Out[I];
    Out[I] = Padded;
  }
  return ChangedAny;
}

/// Returns \p Type's own declared member indices (`0 .. getNumElements()
/// - 1`), sorted by each member's ascending declared byte `Offset`
/// (`Type.getMemberOffset`). For a struct whose members are declared in
/// the same order as their own offsets (the overwhelmingly common case),
/// this is simply the identity permutation. (Roadmap H101p) GLSL's own
/// `all_unordered_and_instance_array` fuzz-test family deliberately emits
/// interface blocks whose members are declared *out* of ascending-offset
/// order (e.g. a `mat4x2` declared first but placed at the higher byte
/// offset, an `ivec3` declared second but placed at byte 0) -- this
/// permutation is what lets both `layOutStructIfOffsetsMatch` (struct-type
/// legalization) and `OffsetStructMemberReorderAccessChainPattern` (its
/// own member-selecting access-chain rewrite) lay out, and address, such a
/// struct's members in physical (ascending-offset) order regardless of
/// declaration order.
/// `llvm::stable_sort` (rather than plain `sort`) preserves declaration
/// order for any two members genuinely declared at the same offset (e.g.
/// a zero-sized array member), matching LLVM's own struct layout, which
/// likewise places same-offset members in declaration order.
///
/// Requires \p Type.hasOffset(): a struct without any member `Offset`
/// decorations (e.g. an ordinary, non-block aggregate) has no
/// `getMemberOffset` values to sort by, and `getMemberOffset` itself
/// dereferences a null offset-info array in that case (Roadmap H96 --
/// this was the null-pointer read that manifested as a `deqp-vk` crash
/// once a caselist happened to first exercise a non-block struct through
/// `OffsetStructMemberReorderAccessChainPattern`, which used to call this
/// unconditionally).
llvm::SmallVector<unsigned, 8>
getOffsetSortedMemberIndices(mlir::spirv::StructType Type) {
  assert(Type.hasOffset() &&
         "getOffsetSortedMemberIndices requires an offset-decorated struct");
  llvm::SmallVector<unsigned, 8> Order;
  Order.reserve(Type.getNumElements());
  for (unsigned I = 0, E = Type.getNumElements(); I != E; ++I)
    Order.push_back(I);
  llvm::stable_sort(Order, [&](unsigned A, unsigned B) {
    return Type.getMemberOffset(A) < Type.getMemberOffset(B);
  });
  return Order;
}

/// Builds the candidate LLVM struct for
/// convertOffsetStructTypeIgnoringDecorations below out of an
/// already-computed \p Members list (indexed by \p Type's own *declared*
/// member order), validating (when \p Type has explicit offsets) that
/// LLVM's own natural ABI-alignment-driven layout for those exact member
/// types, laid out in *physical* (ascending-offset) order, reproduces
/// every declared offset. Returns null if it doesn't. If \p
/// PhysicalIndexOut is non-null and this succeeds, it is filled with a
/// declared-index -> physical (LLVM struct field) index map -- see
/// getStructMemberPhysicalIndex, which recovers this same map for a
/// `spirv.AccessChain` member selector that must be remapped the same way.
///
/// (Roadmap H101p) Lays \p Members out in `getOffsetSortedMemberIndices`'s
/// own physical order rather than \p Type's declared order -- required for
/// a struct whose members are declared out of ascending-offset order (see
/// that function's own comment): the natural-ABI-layout cursor walk below
/// only ever increases, so a declared offset smaller than an
/// already-consumed cursor position could never re-match if members were
/// instead walked in raw declared order.
///
/// (Roadmap H129) Whenever the physical cursor undershoots a member's own
/// declared offset -- whether that's the struct's very first physical
/// member (a real shape: real `dxc`/glslang-compiled SPIR-V produces this
/// whenever a compiler or optimization pass drops one or more leading
/// members from an interface block while every surviving member keeps
/// its original byte offset, roadmap H6q) or any *interior* gap between
/// two physically-adjacent members
/// (a real shape too: `dEQP-VK.ubo.random.all_out_of_order_offsets.*`'s
/// own fuzz cases routinely declare a struct whose members are reordered
/// AND whose physical predecessor is smaller than the natural ABI
/// alignment gap to its successor requires) -- prepends a synthetic
/// `[Gap x i8]` member consuming exactly that gap before placing the next
/// real member, the same way a leading gap already did before this was
/// generalized to any interior one too. An ordinary (non-packed) LLVM
/// struct only ever advances its own layout cursor forward to the next
/// member's natural alignment, so without an explicit pad consuming a
/// gap no member's own natural size/alignment already accounts for, a
/// struct like this could never lay any later member out at its true
/// declared offset at all -- every consumer that builds a GEP or resource
/// getpointer index into a struct converted this way must remap through
/// the returned physical-index map rather than forwarding a declared
/// member index unchanged (see getStructMemberPhysicalIndex,
/// OffsetStructMemberReorderAccessChainPattern, rewriteBlockAccess).
///
/// \p AllowInteriorPad gates that generalization (default off, preserving
/// this function's exact pre-H129 behavior of only ever padding a
/// *leading* gap): convertOffsetStructTypeIgnoringDecorations's own
/// existing retry cascade already has a *different*, longstanding way to
/// paper over an interior gap for a struct declared in already-ascending
/// offset order -- padUndersizedMembersIfNeeded, which widens a member's
/// own body (e.g. `struct<(i32, array<12 x i8>)>`) rather than inserting a
/// separate sibling gap array -- and several already-passing tests pin
/// that exact shape. Enabling interior padding unconditionally here would
/// make THIS function's own (raw, unpadded) first attempt succeed before
/// that existing retry ever runs, silently changing those structs' own
/// converted shape. \p AllowInteriorPad is instead only ever passed true
/// once, as convertOffsetStructTypeIgnoringDecorations's own last resort,
/// once every other retry (including padUndersizedMembersIfNeeded, which
/// cannot help a struct whose members are declared out of physical order
/// in the first place: see its own comment) has already failed.

/// (Roadmap H135) Returns \p Ty's natural ABI alignment *as if* it (and
/// every aggregate nested inside it) were laid out non-packed, ignoring
/// whatever `isPacked` an already-built `LLVM::LLVMStructType` actually
/// carries. `layOutStructIfOffsetsMatch` below always builds its result
/// packed now (see its own comment), so a nested struct member's own
/// `mlir::DataLayout::getTypeABIAlignment` degrades to 1 -- LLVM's own
/// rule for a packed struct's alignment -- even though the *real*,
/// natural alignment its members would have imposed (had it been built
/// non-packed, as it always used to be before H135) is exactly what the
/// gap-detection cursor walk below still needs to decide whether a given
/// member gap is "natural" (needing no caller opt-in) or a true interior
/// gap. Without this, embedding any offset-decorated struct as a member
/// of an *outer* struct silently turns every natural trailing/interior
/// gap into an apparent "unnatural" one requiring `AllowInteriorPad`,
/// which this function's very first (unpadded) attempt never sets --
/// forcing every such case through this function's later retry tiers
/// instead, changing its members' own converted shapes for no reason.
uint64_t getNaturalAlignmentIgnoringPacking(mlir::Type Ty,
                                            mlir::DataLayout &DL) {
  if (auto StructTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(Ty)) {
    uint64_t Max = 1;
    for (mlir::Type Member : StructTy.getBody())
      Max = std::max(Max, getNaturalAlignmentIgnoringPacking(Member, DL));
    return Max;
  }
  if (auto ArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(Ty))
    return getNaturalAlignmentIgnoringPacking(ArrTy.getElementType(), DL);
  return DL.getTypeABIAlignment(Ty);
}

mlir::Type layOutStructIfOffsetsMatch(
    mlir::spirv::StructType Type, llvm::ArrayRef<mlir::Type> Members,
    llvm::SmallVectorImpl<unsigned> *PhysicalIndexOut = nullptr,
    bool AllowInteriorPad = false) {
  if (!Type.hasOffset()) {
    if (PhysicalIndexOut) {
      PhysicalIndexOut->clear();
      for (unsigned I = 0, E = Members.size(); I != E; ++I)
        PhysicalIndexOut->push_back(I);
    }
    return mlir::LLVM::LLVMStructType::getLiteral(Type.getContext(), Members,
                                                  /*isPacked=*/false);
  }

  llvm::SmallVector<unsigned, 8> Order = getOffsetSortedMemberIndices(Type);
  mlir::DataLayout DL;
  uint64_t Cursor = 0;
  llvm::SmallVector<mlir::Type, 9> Laid;
  llvm::SmallVector<unsigned, 8> PhysicalIndexOf(Type.getNumElements(), 0);

  for (unsigned OrderPos = 0, E = Order.size(); OrderPos != E; ++OrderPos) {
    unsigned Idx = Order[OrderPos];
    mlir::Type Member = Members[Idx];
    uint64_t DeclaredOffset = Type.getMemberOffset(Idx);
    uint64_t Alignment = getNaturalAlignmentIgnoringPacking(Member, DL);
    // Natural (unpadded) placement first -- matches this function's
    // pre-H129 behavior exactly, and lets a member whose own natural
    // alignment already reaches DeclaredOffset on its own (e.g. a real,
    // un-substituted SIMD-width vector after a smaller preceding scalar)
    // succeed with no pad at all, whether or not it's the physically
    // first member.
    uint64_t NaturalCursor = llvm::alignTo(Cursor, Alignment);
    if (NaturalCursor > DeclaredOffset)
      return nullptr; // Even natural alignment alone overshoots.
    if (NaturalCursor < DeclaredOffset) {
      // A gap before the struct's own physically-first member is always
      // padded (matches this function's pre-H129 behavior exactly); any
      // later (interior) one only if the caller opted in. This only
      // gates gaps *beyond* what Member's own natural alignment would
      // have reached on its own.
      if (OrderPos != 0 && !AllowInteriorPad)
        return nullptr;
    }
    if (Cursor < DeclaredOffset) {
      // (Roadmap H135) Materialize *every* needed gap explicitly, even
      // one that merely reaches Member's own natural ABI alignment (the
      // NaturalCursor == DeclaredOffset case) -- the resulting struct is
      // always built `isPacked=true` below, so LLVM no longer inserts
      // this alignment padding implicitly the way a non-packed struct
      // would; leaving it out here would silently shrink the struct and
      // misplace every member after this gap.
      uint64_t Gap = DeclaredOffset - Cursor;
      Laid.push_back(mlir::LLVM::LLVMArrayType::get(
          mlir::IntegerType::get(Type.getContext(), 8), Gap));
      // The byte-array pad's own (1-byte) alignment cannot itself force
      // Member any further forward -- Member must already land exactly on
      // DeclaredOffset once the pad reaches it, or no amount of padding
      // can reproduce this declared layout.
      if (llvm::alignTo(DeclaredOffset, Alignment) != DeclaredOffset)
        return nullptr;
    }
    Cursor = DeclaredOffset;
    PhysicalIndexOf[Idx] = Laid.size();
    Laid.push_back(Member);
    Cursor += DL.getTypeSize(Member);
  }
  if (PhysicalIndexOut)
    *PhysicalIndexOut = std::move(PhysicalIndexOf);
  // (Roadmap H135) Must be `isPacked=true`: every member above was placed
  // at its own exact declared byte offset (padding any gap explicitly
  // with a synthetic `[N x i8]` member), so `Cursor`'s final value is
  // already this struct's true intended size. A non-packed
  // `LLVMStructType` ignores that and instead appends its own *implicit*
  // trailing padding to round the whole struct's reported size up to its
  // largest member's own ABI alignment -- e.g. `{ vector<2xf32>, f32 }`
  // (8-byte-aligned vector, offsets 0/8, true size 12) silently becomes
  // 16 bytes. That extra, unwanted padding is invisible for a lone
  // struct value, but corrupts every *array of this struct*, since an
  // `!llvm.array<N x T>`'s per-element stride is always exactly
  // `sizeof(T)`: a `RWStructuredBuffer<B>`-shaped SSBO whose real
  // `ArrayStride` decoration says 12 would get elements placed 16 bytes
  // apart instead, reading/writing every element past the first at the
  // wrong offset.
  return mlir::LLVM::LLVMStructType::getLiteral(Type.getContext(), Laid,
                                                /*isPacked=*/true);
}

/// Returns the LLVM literal struct substituting for a SPIR-V fixed-size
/// array of a *scalar* (or vector) element -- as opposed to an identified
/// struct element, already handled via `padStructToSize` in
/// `convertArrayTypeIgnoringDecorations` -- whenever that array is an
/// outer struct's own member \p ArrayMemberIndex and is immediately
/// followed by another sibling member that (per real `-fvk-use-dx-layout`
/// packing rules) is expected to be packed into the *last* array
/// element's own otherwise-unused trailing padding, rather than after a
/// uniformly-padded array's own full `ArrayStride * NumElements` footprint
/// (roadmap L17; see `convertArrayTypeIgnoringDecorations`'s own comment
/// for why a single, uniform `LLVM::LLVMArrayType` cannot represent this
/// shape at all).
///
/// Builds one literal struct member per array element instead: every
/// element except the last uses a `Stride`-sized opaque byte-array
/// stand-in (mirroring `getTightVectorArrayType`'s own "safe for
/// pointer-based access" reasoning -- this codebase's pointers are all
/// opaque, so a differently-typed, same-size stand-in changes no GEP's
/// own meaning, since whatever `spirv.Load`/`spirv.Store` eventually reads
/// or writes through the resulting address specifies its own value type
/// independently); the *last* element keeps its own real (unpadded,
/// natural-size) converted type, since nothing declared after *it*
/// (within this array) needs its own space reclaimed -- only \p Type's
/// own subsequent sibling member does, and that member's placement is
/// governed entirely by this struct's own overall size, checked by the
/// caller (`layOutStructIfOffsetsMatch`), not by anything within this
/// function.
///
/// Every element access this way still needs a compile-time-constant
/// SPIR-V array index (an ordinary `spirv.Constant`, as real `dxc`-emitted
/// code always uses for a literal HLSL array index like `x[0]`/`x[1]`):
/// unlike an `LLVM::LLVMArrayType`, `LLVM::GEPOp` only allows indexing a
/// literal `LLVM::LLVMStructType` member this way with such a constant
/// (see `verifyStructIndices`, `LLVMDialect.cpp`) -- a genuinely *dynamic*
/// index into an array with this shape remains unrepresentable; this is a
/// deliberate, narrower scope than a fully general fix (see roadmap L17's
/// own text), matching every real case in this project's own test corpus
/// today (`Feature/CBuffer/array-of-structs.test`,
/// `Feature/CBuffer/dynamic-struct.test`), where a dynamic index always
/// selects an *array-of-struct* element (`v[GI]`), never an index into a
/// scalar array like `x` itself.
///
/// Returns null if \p Type's own element is not a scalar or vector (the
/// identified-struct case is handled elsewhere), if its own converted
/// element type's natural size is not smaller than the declared
/// `ArrayStride` (nothing to do, or an already-unrepresentable overshoot),
/// or if the element type fails to convert.
mlir::Type convertUndersizedScalarArrayMemberIgnoringDecorations(
    mlir::spirv::ArrayType Type, const mlir::TypeConverter &Converter,
    mlir::DataLayout &DL) {
  mlir::Type SpirvElementTy = Type.getElementType();
  if (!mlir::isa<mlir::spirv::ScalarType>(SpirvElementTy) &&
      !mlir::isa<mlir::VectorType>(SpirvElementTy))
    return nullptr; // Only scalars/vectors: an identified-struct element is
                     // handled by convertArrayTypeIgnoringDecorations instead.

  mlir::Type ElementType = Converter.convertType(SpirvElementTy);
  if (!ElementType)
    return nullptr;

  unsigned Stride = Type.getArrayStride();
  uint64_t NaturalElementSize = DL.getTypeSize(ElementType);
  if (Stride == 0 || NaturalElementSize >= Stride)
    return nullptr;

  unsigned NumElements = Type.getNumElements();
  if (NumElements == 0)
    return nullptr;

  mlir::Type Surrogate = mlir::LLVM::LLVMArrayType::get(
      mlir::IntegerType::get(Type.getContext(), 8), Stride);
  llvm::SmallVector<mlir::Type, 4> Body(NumElements - 1, Surrogate);
  Body.push_back(ElementType);
  return mlir::LLVM::LLVMStructType::getLiteral(Type.getContext(), Body,
                                                /*isPacked=*/true);
}

/// Converts a SPIR-V struct type to a non-packed LLVM struct with the same
/// member sequence (no inserted padding fields, so member index N still
/// means the same thing to whatever other conversion pattern GEPs into it),
/// after verifying LLVM's own natural (ABI-alignment-driven) layout for
/// those member types reproduces \p Type's declared offsets exactly.
///
/// Unlike MLIR's own `convertStructTypeWithOffset` (`SPIRVToLLVM.cpp`),
/// this does not reject a struct decorated `Block` (or any other
/// whole-struct decoration): that upstream helper's own sanity check
/// compares \p Type against `VulkanLayoutUtils::decorateType(Type)`, which
/// recomputes a struct's *canonical* layout from scratch and never
/// re-attaches any struct-level decoration the original had -- so any
/// `Block`-decorated struct with explicit member offsets (real SPIR-V's
/// actual shape for every uniform/push-constant block: `Offset` is a
/// mandatory per-member decoration whenever `Block` is present) always
/// compares unequal and is spuriously rejected, regardless of whether the
/// byte layout itself is representable. Checking each member's own natural
/// offset directly, as this does, is both correct (a mismatch really is
/// unrepresentable without packing, which would break index
/// correspondence) and immune to a struct decoration the comparison never
/// needed to care about. A push-constant block is always `Block`-decorated
/// in real (`dxc`-compiled or binary-round-tripped) SPIR-V, so
/// `PushConstantGlobalVariablePattern` below needs its own conversion
/// rather than the shared upstream one.
///
/// If the ordinary (real-vector-typed member) layout doesn't reproduce
/// \p Type's declared offsets, retries once with every vector-typed member
/// represented as a tightly-packed array instead (see
/// getTightVectorArrayType) -- needed for a real
/// `-fvk-use-scalar-layout`/`-fvk-use-dx-layout` struct with a vector
/// member whose declared offset assumes a tightly-packed (not
/// power-of-two-lane-rounded) size/alignment, e.g. HLSL's `int3`/`int2`
/// placed immediately after a preceding scalar with no padding. A member
/// whose type was substituted this way still holds the same bits as the
/// vector SPIR-V declared it as; whichever pattern assembles a *whole*
/// aggregate value containing it (rather than just GEP-ing to its address,
/// which this substitution does not affect at all, since this codebase's
/// pointers are all opaque) is responsible for reassembling between the
/// two -- see CompositeConstructPattern's own struct case below, currently
/// the only such pattern that needs to.
///
/// Also retries (independently of the vector retry, and combined with it
/// if both are needed) with any non-last member padded up to the declared
/// gap to its own next sibling (see padUndersizedMembersIfNeeded) --
/// needed for a member whose *natural* size undershoots that gap, e.g. an
/// identified struct used as an ordinary, non-array `cbuffer` member (`X
/// x1; X x2;`, each starting its own fresh HLSL register regardless of its
/// own smaller real size, roadmap L13a). Unlike the vector retry, this
/// substitution only ever appends a new, otherwise-unreferenced trailing
/// member to a struct-typed member's own body (see padStructToSize), so it
/// needs no analogous reassembly step: every existing member index -- and
/// so every already-built GEP through it -- still means exactly what it
/// always did.
///
/// Each retry is a pure fallback (only reached once every previous attempt
/// already failed), so none can change the representation of any struct
/// shape that already converts successfully today.
///
/// Returns null for a struct no attempt can lay out (a matrix member's
/// declared layout is not representable -- see
/// isMatrixMemberLayoutRepresentable -- an unconvertible member type, or a
/// declared offset gap even a padded, tight-vector member cannot
/// reproduce).
///
/// If \p PhysicalIndexOut is non-null and this succeeds, it is filled
/// with whichever retry's own declared-index -> physical-index map
/// (see layOutStructIfOffsetsMatch) actually won -- letting a caller
/// recover exactly how a reordered/padded struct's members really ended
/// up laid out without needing to know (or re-derive) which of the
/// several retry tiers above succeeded (see getStructMemberPhysicalIndex,
/// this function's own only caller needing that map).
mlir::Type convertOffsetStructTypeIgnoringDecorations(
    mlir::spirv::StructType Type, const mlir::TypeConverter &Converter,
    llvm::SmallVectorImpl<unsigned> *PhysicalIndexOut = nullptr) {
  llvm::SmallVector<mlir::Type, 8> Members;
  bool HasVectorMember = false;
  mlir::DataLayout DL;
  for (unsigned I = 0, E = Type.getNumElements(); I != E; ++I) {
    mlir::Type ElementTy = Type.getElementType(I);
    // (Roadmap H133) A nested-struct member must be converted via this
    // same function directly, not through Converter.convertType --
    // MLIR's TypeConverter caches "context-free" conversions per raw
    // SPIR-V type (see TypeConverter::convertTypeImpl), so once *any*
    // caller (anywhere, including one only checking whether some
    // *different* struct converts, transitively) has caused this exact
    // nested struct type to be converted once, every *other* caller
    // (including this struct's own member-list construction here) reuses
    // that one cached answer -- even though convertOffsetStructTypeIgnoring
    // Decorations's own several retry tiers are deterministic given only
    // \p Type and \p Converter, so recomputing here is always safe and
    // guarantees this struct's own Members list agrees with whatever
    // getStructMemberPhysicalIndex (which always calls this function
    // directly, bypassing the cache) computes for this same nested
    // struct, however it's reached.
    mlir::Type MemberTy;
    if (auto NestedStructTy =
            mlir::dyn_cast<mlir::spirv::StructType>(ElementTy))
      MemberTy = convertOffsetStructTypeIgnoringDecorations(NestedStructTy,
                                                            Converter, nullptr);
    else
      MemberTy = Converter.convertType(ElementTy);
    if (!MemberTy)
      return nullptr;
    if (!isMatrixMemberLayoutRepresentable(Type, I, MemberTy)) {
      // (Roadmap H124b) Rather than rejecting the whole struct's own
      // conversion outright, retry with this one member substituted for
      // its own physical (RowMajor/ColMajor- and MatrixStride-aware, but
      // no longer logical/column-major-shaped) layout -- see
      // getPhysicalMatrixMemberType's own comment. Only a pointer (GEP
      // address) computation into this member may use the substituted
      // type directly; getMatrixWholeAccess (used by
      // RowMajorMatrixLoadPattern/StorePattern below) is responsible for
      // interpreting a real Load/Store through it correctly.
      std::optional<MatrixMemberLayout> Layout =
          getMatrixMemberLayout(Type, I);
      MemberTy =
          Layout ? getPhysicalMatrixMemberType(
                       mlir::cast<mlir::spirv::MatrixType>(ElementTy),
                       *Layout, Converter, DL)
                 : nullptr;
      if (!MemberTy)
        return nullptr;
    }
    Members.push_back(MemberTy);
    HasVectorMember |= mlir::isa<mlir::VectorType>(ElementTy);
    if (auto ArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(ElementTy)) {
      HasVectorMember |=
          mlir::isa<mlir::VectorType>(ArrayTy.getElementType());
      // (Roadmap H134) An array-of-matrices member (e.g.
      // `!spirv.array<5 x !spirv.matrix<4 x vector<3xf32>>>`) needs the
      // same retry as a bare matrix member, just one array dimension
      // further in -- see the array-of-matrix case
      // `getTightMatrixType`'s own caller below adds to the
      // array-or-matrix retry tier.
      HasVectorMember |=
          mlir::isa<mlir::spirv::MatrixType>(ArrayTy.getElementType());
    }
    HasVectorMember |= mlir::isa<mlir::spirv::MatrixType>(ElementTy);
    // (Roadmap H101s) A nested struct member (any member count) whose
    // own body includes a vector/matrix/array-of-vector hits the same
    // tight-vector problem as a bare vector member, just one (or more)
    // levels of struct-wrapping away -- make sure this struct's own
    // retry loop below actually runs for a member shaped this way, even
    // if no *other* member in this outer struct is a bare vector/
    // array-of-vectors/matrix. Conservatively treats *any* nested struct
    // member as a reason to retry (rather than recursing just to check),
    // since the retry itself is a no-op if nothing inside actually needed
    // tightening.
    HasVectorMember |= mlir::isa<mlir::spirv::StructType>(ElementTy);
  }
  if (mlir::Type Result = layOutStructIfOffsetsMatch(Type, Members, PhysicalIndexOut))
    return Result;

  llvm::SmallVector<mlir::Type, 8> Padded;
  if (padUndersizedMembersIfNeeded(Type, Members, Padded))
    if (mlir::Type Result = layOutStructIfOffsetsMatch(Type, Padded, PhysicalIndexOut))
      return Result;

  // Retry with any undersized scalar/vector array member (roadmap L17,
  // see convertUndersizedScalarArrayMemberIgnoringDecorations) rebuilt as
  // a last-element-unpadded literal struct instead of a uniform
  // `LLVM::LLVMArrayType`, needed whenever such a member is immediately
  // followed by a sibling packed into its own last element's trailing
  // space -- independent of, and tried both alone and combined with, the
  // vector/padded-member retries below, since a struct can need any
  // combination of these substitutions at once.
  llvm::SmallVector<mlir::Type, 8> ScalarArrayAdjusted(Members.begin(),
                                                       Members.end());
  bool AdjustedScalarArray = false;
  {
    mlir::DataLayout DL;
    for (unsigned I = 0, E = ScalarArrayAdjusted.size(); I != E; ++I) {
      auto ArrayTy =
          mlir::dyn_cast<mlir::spirv::ArrayType>(Type.getElementType(I));
      if (!ArrayTy)
        continue;
      mlir::Type Adjusted = convertUndersizedScalarArrayMemberIgnoringDecorations(
          ArrayTy, Converter, DL);
      if (!Adjusted)
        continue;
      ScalarArrayAdjusted[I] = Adjusted;
      AdjustedScalarArray = true;
    }
  }
  if (AdjustedScalarArray) {
    if (mlir::Type Result =
            layOutStructIfOffsetsMatch(Type, ScalarArrayAdjusted, PhysicalIndexOut))
      return Result;
    if (padUndersizedMembersIfNeeded(Type, ScalarArrayAdjusted, Padded))
      if (mlir::Type Result = layOutStructIfOffsetsMatch(Type, Padded, PhysicalIndexOut))
        return Result;
  }

  if (!HasVectorMember) {
    // (Roadmap H129) Last resort for a struct with no vector/matrix/
    // nested-struct member to retry via the tiers below: an interior
    // gap between two purely-scalar/array members, declared out of
    // physical order, that no earlier tier's own substitution could ever
    // help with anyway (see this function's own investigation notes).
    if (mlir::Type Result = layOutStructIfOffsetsMatch(
            Type, AdjustedScalarArray ? ScalarArrayAdjusted : Members,
            PhysicalIndexOut, /*AllowInteriorPad=*/true))
      return Result;
    return nullptr;
  }

  // (Roadmap H101i) Two-tier retry: first substitute *only* bare vector
  // members (this tier now also covers any nested struct member whose
  // own body needs tightening, roadmap H101s -- see
  // getTightNestedStructType's own comment) with their tight,
  // alignment-free form -- this is exactly the retry this codebase
  // already had before this roadmap item, proven not to regress any
  // previously-working struct -- and only additionally substitute a
  // member that is itself an *array of vectors* or a `spirv.matrix`
  // (this roadmap item's own new capability) if that narrower retry
  // still doesn't reproduce every declared offset.
  //
  // Substituting a matrix/array-of-vectors member unconditionally
  // whenever *any* member in the struct needs a retry -- even one that
  // was already correctly placed on its own -- changes no offset, but
  // does change the LLVM *type* a downstream consumer (e.g.
  // `CanonicalizeStage.cpp`'s row/component-shape resolution, which
  // recognizes a matrix column or array-of-vectors element specifically
  // as a `VectorType`) sees for that member, and was observed (during
  // this same roadmap item's own investigation) to silently produce
  // wrong values, rather than a legalization failure, for
  // `dEQP-VK.transform_feedback.fuzz.random_geometry.all_instance_
  // array.74`, whose struct has a bare vector member that genuinely
  // needs tight substitution and a sibling matrix member that does not.
  // Trying the narrower, already-proven-safe retry first avoids ever
  // reaching for the new substitution unless it's actually needed.
  llvm::SmallVector<mlir::Type, 8> VectorOnly(Members.begin(), Members.end());
  for (unsigned I = 0, E = VectorOnly.size(); I != E; ++I) {
    if (auto VectorTy =
            mlir::dyn_cast<mlir::VectorType>(Type.getElementType(I))) {
      mlir::Type TightTy = getTightVectorArrayType(VectorTy, Converter);
      if (!TightTy)
        return nullptr;
      VectorOnly[I] = TightTy;
    } else if (auto NestedStructTy = mlir::dyn_cast<mlir::spirv::StructType>(
                   Type.getElementType(I))) {
      // (Roadmap H101s) Rebuild this nested struct member's own body
      // with every vector/matrix/array-of-vector inside it tightened,
      // preserving its own member count/order (see
      // getTightNestedStructType's own comment for why an
      // `spirv.AccessChain` into it still resolves correctly).
      mlir::Type TightTy = getTightNestedStructType(NestedStructTy, Converter);
      if (!TightTy)
        return nullptr;
      VectorOnly[I] = TightTy;
    }
  }
  if (mlir::Type Result = layOutStructIfOffsetsMatch(Type, VectorOnly, PhysicalIndexOut))
    return Result;
  if (padUndersizedMembersIfNeeded(Type, VectorOnly, Padded))
    if (mlir::Type Result = layOutStructIfOffsetsMatch(Type, Padded, PhysicalIndexOut))
      return Result;

  // The narrower retry didn't work either -- escalate to also
  // substituting any array-of-vectors or matrix member, starting from the
  // vector-only-substituted list above (a struct can need both kinds of
  // substitution at once).
  llvm::SmallVector<mlir::Type, 8> WithArraysAndMatrices(VectorOnly.begin(),
                                                         VectorOnly.end());
  bool SubstitutedArrayOrMatrix = false;
  for (unsigned I = 0, E = WithArraysAndMatrices.size(); I != E; ++I) {
    mlir::Type ElementTy = Type.getElementType(I);
    // A member that is itself an *array of vectors* (e.g. a
    // `!spirv.array<2 x vector<2xi32>>` member of a multi-member,
    // explicitly-offset block, the shape `dEQP-VK.transform_feedback.
    // fuzz.random_geometry.all_instance_array`'s own trailing member
    // takes) needs the same tight-vector substitution as a bare vector
    // member, just one array dimension further in: the *array's* own ABI
    // alignment is still driven by its vector element's ABI-padded
    // alignment (e.g. a 2-lane `i32` vector's own 8-byte alignment,
    // itself a SIMD-register-driven rounding no tightly-packed XFB/
    // `-fvk-use-scalar-layout` offset scheme reserves room for), so it
    // hits the exact same declared-offset mismatch a bare vector member
    // would, just discovered one level of nesting later.
    //
    // A `spirv.matrix` member (converted, per `MatrixTypeConverter`, to
    // an `!llvm.array` of column vectors -- structurally identical to an
    // ordinary array-of-vectors member for this purpose) hits the exact
    // same gap: its own ABI alignment is still its column vector's own
    // (possibly padded) alignment, which a tightly-packed offset scheme
    // need not leave room for -- e.g. `all_instance_array.11`'s own
    // `mat3x4`-typed member, declared at (whole-block-relative) offset
    // 44, not a multiple of a `vec4` column's own 16-byte alignment.
    // A member that is itself an *array of matrices* (roadmap H134,
    // e.g. `!spirv.array<5 x !spirv.matrix<4 x vector<3xf32>>>`, a
    // `float4x3` instance array member of a multi-member, explicitly-
    // offset block) hits the same gap one dimension further in still --
    // its own ABI alignment is driven by the matrix's own column
    // vector's (possibly padded) alignment, exactly like a bare matrix
    // member, just wrapped in one more array dimension the two cases
    // below do not look through. Handled separately (rather than falling
    // into the array-of-vectors/matrix-of-vectors case below, which only
    // ever looks one level past `ElementTy` for a `VectorType`) since
    // the substitution here needs *two* array dimensions preserved: the
    // declared array's own `InnerCount` around a tight matrix, itself an
    // array of tight column vectors.
    if (auto OuterArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(ElementTy)) {
      if (auto MatrixTy = mlir::dyn_cast<mlir::spirv::MatrixType>(
              OuterArrayTy.getElementType())) {
        mlir::Type TightMatrixTy = getTightMatrixType(MatrixTy, Converter);
        if (!TightMatrixTy)
          return nullptr;
        WithArraysAndMatrices[I] = mlir::LLVM::LLVMArrayType::get(
            TightMatrixTy, OuterArrayTy.getNumElements());
        SubstitutedArrayOrMatrix = true;
        continue;
      }
    }
    unsigned InnerCount;
    mlir::Type InnerElementTy;
    if (auto ArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(ElementTy)) {
      InnerElementTy = ArrayTy.getElementType();
      InnerCount = ArrayTy.getNumElements();
    } else if (auto MatrixTy =
                   mlir::dyn_cast<mlir::spirv::MatrixType>(ElementTy)) {
      InnerElementTy = MatrixTy.getColumnType();
      InnerCount = MatrixTy.getNumColumns();
    } else {
      continue;
    }
    auto InnerVectorTy = mlir::dyn_cast<mlir::VectorType>(InnerElementTy);
    if (!InnerVectorTy)
      continue;
    mlir::Type TightElementTy =
        getTightVectorArrayType(InnerVectorTy, Converter);
    if (!TightElementTy)
      return nullptr;
    WithArraysAndMatrices[I] =
        mlir::LLVM::LLVMArrayType::get(TightElementTy, InnerCount);
    SubstitutedArrayOrMatrix = true;
  }
  if (SubstitutedArrayOrMatrix) {
    if (mlir::Type Result = layOutStructIfOffsetsMatch(
            Type, WithArraysAndMatrices, PhysicalIndexOut))
      return Result;
    if (padUndersizedMembersIfNeeded(Type, WithArraysAndMatrices, Padded))
      if (mlir::Type Result =
              layOutStructIfOffsetsMatch(Type, Padded, PhysicalIndexOut))
        return Result;
  }
  // (Roadmap H129) Absolute last resort, tried only once every other
  // retry above (including padUndersizedMembersIfNeeded, itself unable
  // to help a struct whose members are declared out of physical order:
  // see layOutStructIfOffsetsMatch's own \p AllowInteriorPad comment) has
  // already failed: retry the furthest-substituted member list with
  // interior padding enabled, for a struct whose real problem is an
  // out-of-declaration-order interior gap rather than (or in addition
  // to) anything the tiers above already substitute for.
  if (mlir::Type Result = layOutStructIfOffsetsMatch(
          Type, WithArraysAndMatrices, PhysicalIndexOut,
          /*AllowInteriorPad=*/true))
    return Result;
  return nullptr;
}

/// Returns the physical (post-reordering/padding) LLVM struct field index
/// convertOffsetStructTypeIgnoringDecorations laid \p Struct's own
/// declared member \p DeclaredIndex out at, by re-running that same
/// conversion to recover its own physical-index map (roadmap H129) --
/// cheap and safe to redo here, since struct type conversion is a pure
/// function of \p Struct and \p Converter, and every real caller (an
/// `spirv.AccessChain` member-selector remap: see
/// OffsetStructMemberReorderAccessChainPattern, rewriteBlockAccess) is
/// already relying on \p Struct having converted successfully once
/// already (its own base pointer's pointee), so this never does
/// meaningfully more work than that already-necessary conversion did.
///
/// Falls back to returning \p DeclaredIndex unchanged if \p Struct
/// somehow fails to convert here despite an earlier, successful
/// conversion elsewhere -- should not happen in practice, but degrades to
/// the pre-H129 (no remapping) behavior rather than crashing if it ever
/// does.
unsigned getStructMemberPhysicalIndex(mlir::spirv::StructType Struct,
                                      unsigned DeclaredIndex,
                                      const mlir::TypeConverter &Converter) {
  llvm::SmallVector<unsigned, 8> PhysicalIndexOf;
  if (!convertOffsetStructTypeIgnoringDecorations(Struct, Converter,
                                                  &PhysicalIndexOf))
    return DeclaredIndex;
  if (DeclaredIndex >= PhysicalIndexOf.size())
    return DeclaredIndex;
  return PhysicalIndexOf[DeclaredIndex];
}

/// (Roadmap H133) See this function's own forward-declaration comment.
/// \p CurrentType is the SPIR-V type \p Op's own index at \p StartIndex
/// selects into (already resolved by the caller's own first-level remap
/// -- this only ever remaps indices *at or past* \p StartIndex). \p
/// Indices is the full, 1:1-aligned (already type-converted) index list
/// this mutates in place; only positions from \p StartIndex on are ever
/// touched.
///
/// Walks \p CurrentType exactly as the AccessChain's own declared indices
/// would navigate it: a struct needing no reordering (no `Offset`
/// decorations at all) or an array/runtime-array (whose every element
/// shares one physical layout regardless of which one is selected) needs
/// no remapping at that level, so its own index is left unchanged and
/// this simply advances into the selected element/member's own declared
/// type; a struct that *does* need reordering has its selector remapped
/// exactly as getStructMemberPhysicalIndex already does for a single
/// level. Stops (successfully, leaving every remaining index unchanged)
/// at the first matrix/vector/scalar leaf -- no further struct-member
/// selector is possible past that point -- or once \p Indices is
/// exhausted.
bool remapNestedStructMemberIndices(
    mlir::Type CurrentType, mlir::spirv::AccessChainOp Op, unsigned StartIndex,
    const mlir::TypeConverter &Converter,
    mlir::ConversionPatternRewriter &Rewriter,
    llvm::SmallVectorImpl<mlir::Value> &Indices) {
  unsigned Pos = StartIndex;
  while (Pos < Op.getIndices().size()) {
    mlir::Type ElementType;
    if (auto StructTy = mlir::dyn_cast<mlir::spirv::StructType>(CurrentType)) {
      std::optional<uint64_t> DeclaredIndex =
          getConstantMemberIndex(Op.getIndices()[Pos]);
      if (!DeclaredIndex)
        return false;
      unsigned Declared = static_cast<unsigned>(*DeclaredIndex);
      if (Declared >= StructTy.getNumElements())
        return false;
      if (StructTy.hasOffset()) {
        unsigned Physical =
            getStructMemberPhysicalIndex(StructTy, Declared, Converter);
        if (Physical != Declared) {
          mlir::Type LLVMIndexType = Indices[Pos].getType();
          Indices[Pos] = mlir::LLVM::ConstantOp::create(
              Rewriter, Op.getLoc(), LLVMIndexType,
              Rewriter.getIntegerAttr(LLVMIndexType, Physical));
        }
      }
      ElementType = StructTy.getElementType(Declared);
    } else if (auto ArrayTy =
                   mlir::dyn_cast<mlir::spirv::ArrayType>(CurrentType)) {
      ElementType = ArrayTy.getElementType();
    } else if (auto RTArrayTy =
                   mlir::dyn_cast<mlir::spirv::RuntimeArrayType>(CurrentType)) {
      ElementType = RTArrayTy.getElementType();
    } else {
      // A matrix/vector/scalar leaf: no further struct-member selector
      // is possible past this point.
      break;
    }
    CurrentType = ElementType;
    ++Pos;
  }
  return true;
}

/// Converts a SPIR-V (fixed-size) array type to an LLVM array with the
/// same element count, padding the element type up to the declared
/// `ArrayStride` (see padStructToSize) whenever its own natural size
/// undershoots it -- needed for a real `-fvk-use-dx-layout` array of an
/// identified struct, e.g. `X xs[2]`, whose own declared per-element
/// stride reserves more space than the struct's own real content occupies
/// (roadmap L13a). Supersedes MLIR's own `convertArrayType`
/// (`SPIRVToLLVM.cpp`), which instead validates the declared stride
/// against `VulkanLayoutUtils::getNaturalArrayStride`, a function that
/// unconditionally fails for *any* identified-struct element (see
/// `VulkanLayoutUtils::decorateType`'s own "Identified structs are uniqued
/// by identifier" comment, `LayoutUtils.cpp`) regardless of whether the
/// stride is actually reproducible -- so an array of an identified struct
/// never converts via that path at all, even in the already-working case
/// where the element's natural size already matches the stride exactly.
/// Basing this instead on the *converted* element's own real
/// `DataLayout`-computed size sidesteps that upstream limitation entirely,
/// while remaining exactly as strict about a genuinely unrepresentable
/// stride (one narrower than the element's own natural size, or one no
/// amount of trailing padding reproduces exactly) as the struct-member
/// padding retry above.
///
/// A *scalar* (or vector) element is padded differently (roadmap L17):
/// unlike a struct element, it has no "own body" to append a trailing
/// padding member to (see padStructToSize's own comment), so instead this
/// substitutes a `Stride`-sized opaque byte-array stand-in for every
/// element uniformly, exactly as `getTightVectorArrayType` already does
/// (safely, since this codebase's pointers are all opaque throughout) for
/// a struct member's own vector-to-array substitution -- this changes no
/// GEP's own addressing depth (still one array index reaches element
/// `i`'s own stand-in directly), only what nominal type that GEP's result
/// reports, which nothing but the pointer's own address depends on. This
/// keeps a scalar array like HLSL's own `uint x[2]` correctly addressable
/// (`x[1]` computes the right byte offset) whenever it either is not
/// itself immediately followed by a sibling struct member, or is followed
/// by one but has room to spare (the declared gap to that sibling is at
/// least `ArrayStride * NumElements`).
///
/// When such an array *is* immediately followed by a sibling with no room
/// to spare -- the real `dxc`-emitted layout instead expects that sibling
/// packed into whatever's left of the *last* element's own otherwise-
/// unused trailing padding, a shape this uniform substitution cannot
/// represent on its own, since every element (including the last) is the
/// same `Stride`-wide width here -- see
/// convertUndersizedScalarArrayMemberIgnoringDecorations's own doc comment
/// for the separate, struct-member-context-aware substitution
/// `convertOffsetStructTypeIgnoringDecorations` retries with instead
/// whenever this function's own (successful, but too-large) result
/// doesn't reproduce the declared offsets.
///
/// Returns null if the element type fails to convert, or if padding it to
/// the declared stride fails (the declared stride is narrower than the
/// element's own natural size) or is not needed (\p Stride is 0, meaning
/// no stride was declared at all -- e.g. a workgroup-shared array -- in
/// which case no padding applies and the plain element type is used
/// directly).
mlir::Type convertArrayTypeIgnoringDecorations(
    mlir::spirv::ArrayType Type, const mlir::TypeConverter &Converter) {
  mlir::Type ElementType = Converter.convertType(Type.getElementType());
  if (!ElementType)
    return nullptr;

  if (unsigned Stride = Type.getArrayStride()) {
    mlir::DataLayout DL;
    if (mlir::Type Padded = padStructToSize(ElementType, Stride, DL)) {
      ElementType = Padded;
    } else {
      uint64_t NaturalSize = DL.getTypeSize(ElementType);
      if (NaturalSize > Stride)
        return nullptr;
      if (NaturalSize < Stride)
        ElementType = mlir::LLVM::LLVMArrayType::get(
            mlir::IntegerType::get(Type.getContext(), 8), Stride);
    }
  }
  return mlir::LLVM::LLVMArrayType::get(ElementType, Type.getNumElements());
}

/// Converts a push constant `spirv.GlobalVariable` to an ordinary
/// `llvm.mlir.global` in the address space LLVM's SPIRV backend recognizes
/// as a push constant block (13, see `storageClassToAddressSpace` in
/// `llvm/lib/Target/SPIRV/SPIRVUtils.h`): its own `SPIRVPushConstantAccess`
/// pass finds every global there and rewrites it -- and every use of it --
/// into the `spirv.PushConstant` target extension type and the
/// `llvm.spv.pushconstant.getpointer` intrinsic itself, so FeMe does not
/// have to spell either one; unlike a resource or builtin variable, a push
/// constant's declaration survives the conversion as a real global; only its
/// storage class needs translating into that address space (see
/// feme::spirv::populateSPIRVToLLVMTargetTypeConversions), matching how
/// MLIR's own `GlobalVariablePattern` handles the storage classes it
/// supports -- `PushConstant` is just not one of them. The pointee type
/// itself goes through the ordinary type converter, which
/// `populateSPIRVToLLVMTargetTypeConversions` arranges to lay out a
/// `Block`-decorated struct's declared offsets correctly (see
/// `convertOffsetStructTypeIgnoringDecorations`'s comment).
class PushConstantGlobalVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GlobalVariableOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GlobalVariableOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto SrcType = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    if (SrcType.getStorageClass() != mlir::spirv::StorageClass::PushConstant)
      return Rewriter.notifyMatchFailure(Op, "not a push constant variable");

    mlir::Type DstType =
        getTypeConverter()->convertType(SrcType.getPointeeType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    Rewriter.replaceOpWithNewOp<mlir::LLVM::GlobalOp>(
        Op, DstType, /*isConstant=*/true, mlir::LLVM::Linkage::External,
        Op.getSymName(), mlir::Attribute(), /*alignment=*/0,
        /*addrSpace=*/13);
    return mlir::success();
  }
};

/// Converts a `spirv.AccessChain` whose base pointer's pointee is (or, per
/// roadmap H101n, is a single-dimensional array of) a `spirv::StructType`
/// requiring a leading/interior offset pad and/or (roadmap H101p) a member
/// reordering (see getOffsetSortedMemberIndices/layOutStructIfOffsetsMatch's
/// own comments). MLIR's own generic
/// `AccessChainPattern` forwards every index straight through to the
/// converted LLVM struct unmodified -- exactly wrong once
/// `layOutStructIfOffsetsMatch`'s own physical, ascending-offset field
/// order has shifted a real member's LLVM struct index away from its own
/// declared SPIR-V member index (whether by a leading/interior pad, an
/// arbitrary permutation from out-of-declaration-order members, or both at
/// once). This pattern is otherwise identical to the generic one
/// (same result-type/leading-zero handling), it just substitutes \p Op's
/// own member-selecting index -- the one that selects a member of the
/// struct itself, index 0 if that struct sits directly behind the base
/// pointer or index 1 if an outer array dimension comes first -- with its
/// own real physical LLVM field index (getStructMemberPhysicalIndex)
/// before forwarding every subsequent index
/// (navigating whatever that index selected, a type this pattern's own
/// reordering never touches) unchanged.
///
/// Scoped to the outermost struct a `spirv.AccessChain`'s own base pointer
/// directly points to, or (roadmap H101n) to that same struct one level
/// deeper, behind a single outer array dimension -- GLSL's own "array of
/// block instances" syntax (`layout(...) out Block { T member; } block[N];`,
/// e.g. `dEQP-VK.transform_feedback.fuzz.*instance_array*`'s own per-
/// instance interface blocks) addresses one instance's own member through
/// exactly that shape: `spirv.AccessChain`'s own first index selects the
/// array element, its *second* index selects the struct member this
/// pattern remaps. A struct member that is itself a struct independently
/// requiring its own leading pad or reordering, nested more than one level
/// deep, would need a similar adjustment at that deeper level too, which
/// this does not yet attempt (mirroring this file's own precedent
/// elsewhere, e.g. convertUndersizedScalarArrayMemberIgnoringDecorations's
/// "matching every real case" scoping, of not generalizing past what is
/// actually observed).
///
/// A struct (whether pointed to directly or through that one outer array
/// dimension) needing neither a pad nor any reordering is left to the
/// generic pattern (`notifyMatchFailure`), so no already-working access
/// chain changes. Likewise if the member-selecting index is not a
/// compile-time constant (always true for a real struct-member selector
/// per the SPIR-V spec, but declined rather than miscompiled if a
/// malformed module ever violates that), or if the access chain does not
/// reach far enough to select a struct member at all (e.g. only selects
/// the whole array element).
class OffsetStructMemberReorderAccessChainPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AccessChainOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::AccessChainOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AccessChainOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto PointerType =
        mlir::cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());
    // (Roadmap H129) A `spirv.VulkanBuffer`-backed (Block/Uniform-handle)
    // base pointer is BlockAccessChainPattern's/
    // ArrayedBlockAccessChainPattern's own job (via rewriteBlockAccess,
    // which does its own physical-index remap directly against the
    // resource getpointer intrinsic these patterns lower to) -- this
    // pattern's own plain `llvm.getelementptr` substitution would
    // otherwise build one straight through the still-opaque handle type
    // itself (an illegal, non-pointer `getelementptr` operand), matching
    // the "gep-pointer-type" bucket's own diagnostic (see this roadmap
    // item's own investigation notes).
    if (getBufferBlockElement(PointerType) ||
        getUniformBlockElement(PointerType))
      return Rewriter.notifyMatchFailure(
          Op, "block/uniform-handle base pointer handled by "
              "BlockAccessChainPattern instead");
    mlir::Type PointeeTy = PointerType.getPointeeType();
    // `MemberIndexPos` is which of `Op`'s own indices selects the
    // reordered/padded struct's own member -- index 0 if the struct sits
    // directly behind the base pointer, index 1 if an outer array
    // dimension (one "array of block instances" element) comes first.
    unsigned MemberIndexPos = 0;
    auto StructTy = mlir::dyn_cast<mlir::spirv::StructType>(PointeeTy);
    if (!StructTy) {
      if (auto ArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(PointeeTy)) {
        StructTy =
            mlir::dyn_cast<mlir::spirv::StructType>(ArrayTy.getElementType());
        MemberIndexPos = 1;
      }
    }
    // (Roadmap H129) Whether \p StructTy needs any remapping at all --
    // a leading pad, an interior pad, a declaration-order permutation, or
    // any combination -- is determined directly from
    // convertOffsetStructTypeIgnoringDecorations's own physical-index map
    // rather than re-derived from `getOffsetSortedMemberIndices`/
    // `structHasLeadingOffsetPad` alone: those two together only ever
    // detected a permutation or a single *leading* pad, never an
    // *interior* one (a struct laid out in already-ascending declared
    // order, but with a gap between two physically-adjacent members,
    // permutes nothing and has no leading pad, yet still needs every
    // member after that gap remapped one or more slots forward).
    //
    // (Roadmap H96) `StructTy.hasOffset()` must still be checked here: an
    // ordinary, non-block struct (no member `Offset` decorations, e.g.
    // an ordinary function-scope aggregate rather than a uniform/storage
    // block) has no physical-index map to speak of, and
    // convertOffsetStructTypeIgnoringDecorations's own early return for
    // that case (see layOutStructIfOffsetsMatch) already reports an
    // identity map, so this check is really just an optimization to
    // avoid the reconversion below for the common (non-offset) case.
    llvm::SmallVector<unsigned, 8> PhysicalIndexOf;
    bool NeedsRemap = false;
    if (StructTy && StructTy.hasOffset()) {
      if (!convertOffsetStructTypeIgnoringDecorations(
              StructTy, *getTypeConverter(), &PhysicalIndexOf))
        return Rewriter.notifyMatchFailure(Op, "type conversion failed");
      for (unsigned I = 0, E = PhysicalIndexOf.size(); !NeedsRemap && I != E;
           ++I)
        NeedsRemap = PhysicalIndexOf[I] != I;
    }
    if (!StructTy || !NeedsRemap)
      return Rewriter.notifyMatchFailure(
          Op, "no leading offset pad or member reordering needed");
    if (Op.getIndices().size() <= MemberIndexPos)
      return Rewriter.notifyMatchFailure(
          Op, "access chain does not select a struct member");

    std::optional<uint64_t> MemberIndex =
        getConstantMemberIndex(Op.getIndices()[MemberIndexPos]);
    if (!MemberIndex)
      return Rewriter.notifyMatchFailure(
          Op, "leading struct member selector is not a constant");

    mlir::Type DstType =
        getTypeConverter()->convertType(Op.getComponentPtr().getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    // The GEP's own source element type must match the base pointer's real
    // (possibly array-wrapping) pointee, not just the inner struct, or its
    // offset arithmetic would skip the outer array dimension entirely.
    mlir::Type ElementType = getTypeConverter()->convertType(PointeeTy);
    if (!ElementType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Type IndexType = Op.getIndices()[MemberIndexPos].getType();
    mlir::Type LLVMIndexType = getTypeConverter()->convertType(IndexType);
    if (!LLVMIndexType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    // The constant's own attribute type must be `LLVMIndexType`, not the
    // original (possibly `si32`-wrapped) SPIR-V `IndexType` -- an
    // `llvm.mlir.constant`'s attribute and result types must agree exactly.
    mlir::Value Zero = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, LLVMIndexType,
        Rewriter.getIntegerAttr(LLVMIndexType, 0));
    // \p MemberIndex's own real physical field is exactly what the
    // physical-index map computed above (via
    // convertOffsetStructTypeIgnoringDecorations) already says it is.
    uint64_t PhysicalIndex = PhysicalIndexOf[static_cast<unsigned>(*MemberIndex)];
    mlir::Value AdjustedMember = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, LLVMIndexType,
        Rewriter.getIntegerAttr(LLVMIndexType, PhysicalIndex));

    llvm::SmallVector<mlir::Value, 4> Indices;
    Indices.push_back(Zero);
    if (MemberIndexPos == 1) {
      // Forward the outer array index unchanged -- the reordered/padded
      // struct lives inside each array element itself, not across the
      // array dimension.
      Indices.push_back(Adaptor.getIndices().front());
    }
    Indices.push_back(AdjustedMember);
    // (Roadmap H133) Any further index may itself select into a member of
    // a further reordered/padded struct, nested more than one level below
    // \p StructTy -- remap every one of those exactly as \p MemberIndex's
    // own selector was above.
    mlir::Type SelectedMemberType =
        StructTy.getElementType(static_cast<unsigned>(*MemberIndex));
    llvm::SmallVector<mlir::Value, 4> RemappedTail(Adaptor.getIndices().begin(),
                                                   Adaptor.getIndices().end());
    if (!remapNestedStructMemberIndices(SelectedMemberType, Op,
                                        MemberIndexPos + 1, *getTypeConverter(),
                                        Rewriter, RemappedTail))
      return Rewriter.notifyMatchFailure(
          Op, "nested struct member selector is not a constant");
    llvm::append_range(
        Indices, llvm::ArrayRef(RemappedTail).drop_front(MemberIndexPos + 1));

    Rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
        Op, DstType, ElementType, Adaptor.getBasePtr(), Indices);
    return mlir::success();
  }
};

/// Whether \p Type is, or (recursively, through a `spirv::StructType`/
/// `spirv::ArrayType`) contains, SPIR-V's `OpTypeBool` (represented, like
/// MLIR's SPIR-V dialect itself, as a plain `i1` -- there is no distinct
/// `spirv::BoolType`). `OpTypeBool` has no defined memory representation
/// (its only legal storage classes -- `Workgroup`, `Private`, `Function` --
/// are exactly the ones this ICD's SPIR-V producers can put one in), and an
/// `i1` is only sound as a pure SSA value: once addressed by
/// `getelementptr` as part of an aggregate (any `Workgroup`-storage
/// `shared`/`groupshared` struct or array containing a `bool`/`bvec*`),
/// its 1-bit size is not byte-addressable, asserting deep in LLVM's own
/// `GetElementPtrTypeIterator` ("Not byte-addressable") rather than failing
/// to legalize (`dEQP-VK.compute.pipeline.
/// zero_initialize_workgroup_memory.composites.*`, whose per-case struct
/// mixes a `bool`/`bvec2`/`bvec3`/`bvec4` field in with real scalars).
/// `WorkgroupGlobalVariablePattern` checks this before converting so the
/// unsupported shape fails to legalize cleanly instead.
bool containsAddressableBool(mlir::Type Type) {
  if (Type.isInteger(1))
    return true;
  if (auto StructTy = mlir::dyn_cast<mlir::spirv::StructType>(Type)) {
    for (unsigned I = 0, E = StructTy.getNumElements(); I != E; ++I)
      if (containsAddressableBool(StructTy.getElementType(I)))
        return true;
    return false;
  }
  if (auto ArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(Type))
    return containsAddressableBool(ArrayTy.getElementType());
  return false;
}

/// Converts a `Workgroup`-storage-class `spirv.GlobalVariable` -- a GLSL
/// `shared`/HLSL `groupshared` variable declared directly in SPIR-V, rather
/// than raised from DXIL (see `feme::cpu::GroupSharedAddressSpace`'s own
/// comment) -- to an ordinary `llvm.mlir.global` in address space 3: the
/// same convention Clang's own HLSL `groupshared` codegen already uses (see
/// `LangAS::hlsl_groupshared`'s target address space mapping), which is what
/// lets `feme::cpu::SIMDizePass`/`feme::cpu::EntryWrapperPass` (Phase 4/6,
/// GroupShared.h) treat a variable imported from either source identically,
/// with no SPIR-V-specific case of their own. Neither pattern lives in
/// `feme/lib/Transforms/CPU` -- this file is shared by every FeMe target, not
/// just the CPU one -- so the address space is spelled as the literal `3`
/// here rather than including that header.
///
/// `zero_initialized` (`VK_KHR_zero_initialize_workgroup_memory`, roadmap
/// milestone E13) becomes the LLVM global's own `#llvm.zero` initializer:
/// `feme::cpu::GroupSharedLayout::NeedsZeroInit` reads `hasInitializer()`
/// back off exactly this global to decide whether the dispatch wrapper needs
/// to zero this group's groupshared buffer before running (see
/// GroupShared.h/EntryWrapper.cpp) -- an ordinary groupshared variable with
/// no zero-initializer of its own (Internal linkage would require a body,
/// so it is `External`, undefined, matching Clang's own HLSL groupshared
/// codegen) leaves that flag unset.
class WorkgroupGlobalVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GlobalVariableOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GlobalVariableOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto SrcType = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    if (SrcType.getStorageClass() != mlir::spirv::StorageClass::Workgroup)
      return Rewriter.notifyMatchFailure(Op, "not a workgroup variable");

    if (containsAddressableBool(SrcType.getPointeeType()))
      return Rewriter.notifyMatchFailure(
          Op, "a bool member of a workgroup variable is not yet supported "
              "(not byte-addressable)");

    mlir::Type DstType =
        getTypeConverter()->convertType(SrcType.getPointeeType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    if (Op.getZeroInitialized()) {
      Rewriter.replaceOpWithNewOp<mlir::LLVM::GlobalOp>(
          Op, DstType, /*isConstant=*/false, mlir::LLVM::Linkage::Internal,
          Op.getSymName(), mlir::LLVM::ZeroAttr::get(Rewriter.getContext()),
          /*alignment=*/0, /*addrSpace=*/3);
      return mlir::success();
    }

    Rewriter.replaceOpWithNewOp<mlir::LLVM::GlobalOp>(
        Op, DstType, /*isConstant=*/false, mlir::LLVM::Linkage::External,
        Op.getSymName(), mlir::Attribute(), /*alignment=*/0, /*addrSpace=*/3);
    return mlir::success();
  }
};

/// Converts a `TaskPayloadWorkgroupEXT`-storage-class `spirv.GlobalVariable`
/// -- a task entry's bounded payload variable (SPIR-V enum 5402), written by
/// `OpEmitMeshTasksEXT`'s own payload operand and read back by the mesh
/// stage it launches -- to an ordinary `llvm.mlir.global` in address space
/// 14. Unlike every other storage class `StageIOGlobalVariablePattern`/
/// `PushConstantGlobalVariablePattern`/`WorkgroupGlobalVariablePattern`
/// above reuse from LLVM's own SPIR-V backend
/// (`storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`),
/// that switch has no case for `TaskPayloadWorkgroupEXT` at all (it hits the
/// `report_fatal_error` default), so 14 is a FeMe-only convention -- the
/// next address space after the highest one (13, `PushConstant`) that
/// switch does define, and not otherwise used anywhere in this file
/// (roadmap H6h). A payload variable is read-write across the two stages
/// that share it (never constant, unlike `Input`) and, like `Workgroup`,
/// has no zero-initializer convention of its own to preserve.
class TaskPayloadGlobalVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GlobalVariableOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GlobalVariableOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GlobalVariableOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto SrcType = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    if (SrcType.getStorageClass() !=
        mlir::spirv::StorageClass::TaskPayloadWorkgroupEXT)
      return Rewriter.notifyMatchFailure(Op, "not a task payload variable");

    mlir::Type DstType =
        getTypeConverter()->convertType(SrcType.getPointeeType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    Rewriter.replaceOpWithNewOp<mlir::LLVM::GlobalOp>(
        Op, DstType, /*isConstant=*/false, mlir::LLVM::Linkage::External,
        Op.getSymName(), mlir::Attribute(), /*alignment=*/0,
        /*addrSpace=*/14);
    return mlir::success();
  }
};

/// Emits the `llvm.spv.resource.getpointer` call addressing \p Coordinate
/// within the resource \p Handle. LLVM's SPIRV backend selects
/// `OpImageRead`/`OpImageWrite` from the ordinary load or store through the
/// resulting pointer, which is also how the DXIL -> SPIR-V direction spells
/// a typed buffer access (feme::spirv::RaisedLoweringPass).
mlir::Value createResourcePointer(mlir::ConversionPatternRewriter &Rewriter,
                                  mlir::Location Loc, mlir::Value Handle,
                                  mlir::Value Coordinate) {
  return createIntrinsicCall(
      Rewriter, Loc, "llvm.spv.resource.getpointer",
      mlir::LLVM::LLVMPointerType::get(Rewriter.getContext()),
      {Handle, Coordinate});
}

/// Returns a new vector one lane wider than \p Coordinate, with \p
/// Coordinate's own lanes copied in order followed by \p ExtraLane as the
/// trailing lane (roadmap H19g). `llvm.spv.resource.getpointer` (a real,
/// fixed 2-operand LLVM intrinsic shared with the DXIL -> SPIR-V direction)
/// has no operand slot of its own for a multisampled storage image's
/// `Sample` image operand, so this widens the ordinary `(x, y)` coordinate
/// into a 3-component `(x, y, sample)` one instead -- exactly the same
/// coordinate shape `SPIRVResourceLowering.cpp`'s `classifyStorageImage2DHandle`/
/// `lowerImageAccesses` already extract an array layer or depth-slice
/// from for `Array2D`/`Plain3D` (see `ImageShape::Plain2DMS`'s own
/// comment), rather than inventing a separate mechanism to carry `Sample`.
mlir::Value appendVectorLane(mlir::ConversionPatternRewriter &Rewriter,
                             mlir::Location Loc, mlir::Value Coordinate,
                             mlir::Value ExtraLane) {
  auto CoordVecTy = mlir::cast<mlir::VectorType>(Coordinate.getType());
  auto WideTy = mlir::VectorType::get(
      {CoordVecTy.getNumElements() + 1}, CoordVecTy.getElementType());
  mlir::Value Wide = mlir::LLVM::PoisonOp::create(Rewriter, Loc, WideTy);
  for (int64_t I = 0, E = CoordVecTy.getNumElements(); I != E; ++I) {
    mlir::Value Index = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI64Type(), Rewriter.getI64IntegerAttr(I));
    mlir::Value Lane = mlir::LLVM::ExtractElementOp::create(
        Rewriter, Loc, Coordinate, Index);
    Wide = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Wide, Lane,
                                               Index);
  }
  mlir::Value LastIndex = mlir::LLVM::ConstantOp::create(
      Rewriter, Loc, Rewriter.getI64Type(),
      Rewriter.getI64IntegerAttr(CoordVecTy.getNumElements()));
  return mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Wide, ExtraLane,
                                             LastIndex);
}

/// Whether \p ImageType is a non-cube, non-3D, multisampled `Dim::2D`
/// image, plain or arrayed -- the multisampled shapes roadmap H19g/H19m's
/// `Sample`-image-operand-to-coordinate widening (`appendVectorLane`
/// above) supports today. SPIR-V's own `Coordinate` operand for an arrayed
/// image already carries the array layer as an ordinary coordinate
/// component (see `SPIRVResourceLowering.cpp`'s own `Array2D`/`Array2DMS`
/// comments) -- `appendVectorLane` needs no changes to append a `Sample`
/// lane after it, whatever the input width; only this gate needed
/// widening to admit the arrayed case. A cube multisampled image remains
/// out of scope: SPIR-V disallows a multisampled `Dim::Cube` image
/// outright (unlike `Dim::2D`, which both the plain and arrayed cases of
/// this check cover).
bool isMultisampled2DImage(mlir::spirv::ImageType ImageType) {
  return ImageType.getDim() == mlir::spirv::Dim::Dim2D &&
        ImageType.getSamplingInfo() == mlir::spirv::ImageSamplingInfo::MultiSampled;
}

/// SPIR-V 1.6's `Nontemporal` image-operand bit is a cache hint with no
/// correctness effect (see the SPIR-V spec's Image Operands table); this
/// converter has no caching model to honor it with, so every pattern below
/// accepts and discards it rather than rejecting it like an unmodeled
/// modifier or threading it through as if it changed the access performed.
///
/// Roadmap H71: `SignExtend`/`ZeroExtend` are discarded the same way, for a
/// different but equally model-free reason. Every SPIR-V `OpTypeImage`'s
/// "Sampled Type" is always its scalar component type at its *full*
/// register width (e.g. `i32` for any integer format, narrow or wide) --
/// the declared image *format* (`R8ui`, `R32ui`, etc.) only describes the
/// on-disk texel layout a real GPU's texture unit would need to widen from,
/// a distinction this CPU executor has no equivalent of: a resource's real
/// `VkFormat` (not the SPIR-V-level format literal, which is merely an
/// optional compile-time hint and can even be `Unknown`) already drives
/// every narrow-to-wide conversion this project performs, in the resource
/// load/store path itself (`ImageFixture.cpp`'s `packClearColor`/
/// `unpackColor`, and the CPU resource-access intrinsics that call them),
/// long before or after this SPIR-V-to-LLVM-dialect conversion ever runs.
/// So `SignExtend`/`ZeroExtend` -- which only ever tell a real GPU *how* to
/// perform a widening this pass never performs at all -- carry no
/// information this pass could act on, exactly like `Nontemporal`. Real
/// `deqp-vk` SPIR-V confirms `glslang` emits one or the other on every
/// integer-format `OpImageRead`/`OpImageWrite`/`OpImageSampleExplicitLod`
/// once the target environment reaches SPIR-V 1.4+ (mandatory for
/// `VK_EXT_mesh_shader`, whose required Vulkan 1.3 always implies at least
/// SPIR-V 1.6) -- unconditionally rejecting them here, as before this
/// milestone, broke every mesh-stage integer image access verbatim,
/// regardless of format width.
constexpr mlir::spirv::ImageOperands DiscardedImageOperandBits =
    mlir::spirv::ImageOperands::Nontemporal |
    mlir::spirv::ImageOperands::SignExtend |
    mlir::spirv::ImageOperands::ZeroExtend;

/// Returns the type a synthesized all-zero `Offset` operand should have when
/// a sample or fetch op has no real `ConstOffset` operand of its own to
/// borrow the type from (every pattern below that defaults a missing offset
/// to zero calls this, passing \p ImageTy -- either a sample op's own
/// `SampledImageType::getImageType()`, or a fetch op's own plain
/// `getImage()` type -- instead of deriving the shape from \p Coordinate's
/// own type directly). Mirrors `SPIRVResourceLowering.cpp`'s own
/// `isSupportedOffset` dimensionality rule: a `Dim::Dim1D` image's real
/// `ConstOffset` (`Plain1D`/`Array1D`, confirmed via a real `deqp-vk`
/// SPIR-V capture, roadmap L66(d)) is always a bare scalar `i32`, matching
/// the image's own 1-dimensional extent and excluding any array layer --
/// unlike every other coordinate component here, this never widens for
/// `Array1D`'s own arrayed 2-component `(U, ArrayLayer)` coordinate. Every
/// other dimension's own `ConstOffset` does mirror \p Coordinate's vector
/// width, so this only special-cases `Dim1D`, falling back to the original
/// coordinate-shape-mirroring logic otherwise. Roadmap L66(j): this bug was
/// silent for every already-working `Array1D` combination that carries a
/// real `ConstOffset` operand (whose type comes from SPIR-V import
/// directly, always correctly scalar), and only surfaced for a `Grad`+
/// `MinLod` sample with no `ConstOffset` at all -- the fallback zero-offset
/// type is the only path that ever went through this (buggy,
/// coordinate-derived) computation instead.
mlir::Type getDefaultZeroOffsetType(mlir::spirv::ImageType ImageTy,
                                    mlir::Type CoordinateType,
                                    mlir::OpBuilder &Builder) {
  if (ImageTy.getDim() == mlir::spirv::Dim::Dim1D)
    return Builder.getI32Type();
  auto CoordVecTy = mlir::dyn_cast<mlir::VectorType>(CoordinateType);
  return CoordVecTy ? mlir::cast<mlir::Type>(mlir::VectorType::get(
                          CoordVecTy.getShape(), Builder.getI32Type()))
                    : mlir::cast<mlir::Type>(Builder.getI32Type());
}

/// Returns true if \p ImageOperands names any actual modifier (e.g. `Lod`,
/// `Bias`) rather than being absent, the empty `None` bit-enum value, or one
/// of the discarded bits (`Nontemporal`/`SignExtend`/`ZeroExtend`, see
/// `DiscardedImageOperandBits` above) -- real `dxc`-compiled SPIR-V spells
/// "no modifiers" as an explicit `#spirv.image_operands<None>` attribute
/// rather than omitting the (optional) attribute entirely, so a presence
/// check alone rejects every image access real SPIR-V input produces.
bool hasImageOperands(std::optional<mlir::spirv::ImageOperands> ImageOperands) {
  if (!ImageOperands)
    return false;
  return mlir::spirv::bitEnumClear(*ImageOperands, DiscardedImageOperandBits) !=
         mlir::spirv::ImageOperands::None;
}

/// Returns true if \p ImageOperands is exactly \p Required, optionally
/// combined with any of the discarded bits (see above).
bool hasExactImageOperands(
    std::optional<mlir::spirv::ImageOperands> ImageOperands,
    mlir::spirv::ImageOperands Required) {
  if (!ImageOperands)
    return false;
  return mlir::spirv::bitEnumClear(*ImageOperands, DiscardedImageOperandBits) == Required;
}


/// Converts `spirv.ImageRead` or `spirv.ImageFetch` into a load through the
/// read location. The two ops are otherwise handled identically here: LLVM's
/// SPIRV backend picks `OpImageRead` vs `OpImageFetch` itself, from whether
/// the handle's underlying image type has its `Sampled` operand set to 1
/// (`spirv.ImageFetch`'s only legal operand, per its verifier) rather than
/// from which intrinsic produced the load -- see `generateImageReadOrFetch`
/// in `llvm/lib/Target/SPIRV/SPIRVInstructionSelector.cpp`.
///
/// Roadmap H19g: a `spirv.ImageRead` against a plain (non-arrayed)
/// multisampled 2D storage image may also carry a lone `Sample` image
/// operand (SPIR-V requires one whenever the image's own `MS == 1`); this
/// is accepted only for `ImageReadOp` (an `ImageFetchOp`'s own multisampled-
/// *sampled*-image case is still unstarted follow-on work) and its operand
/// value is appended as the coordinate vector's own trailing lane
/// (`appendVectorLane`), which `SPIRVResourceLowering.cpp`'s
/// `classifyStorageImage2DHandle`/`lowerImageAccesses` then extract exactly
/// like `Array2D`'s own array-layer/`Plain3D`'s own depth-slice 3rd
/// component (`ImageShape::Plain2DMS`, or roadmap H19m's `Array2DMS` when
/// the image is also arrayed).
template <typename ImageOpTy>
class ImageLoadPattern : public mlir::SPIRVToLLVMConversion<ImageOpTy> {
public:
  using mlir::SPIRVToLLVMConversion<ImageOpTy>::SPIRVToLLVMConversion;
  using OpAdaptor = typename mlir::SPIRVToLLVMConversion<ImageOpTy>::OpAdaptor;

  mlir::LogicalResult
  matchAndRewrite(ImageOpTy Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperands =
        Op.getImageOperands();
    bool HasSample = false;
    if constexpr (std::is_same_v<ImageOpTy, mlir::spirv::ImageReadOp>) {
      HasSample = hasExactImageOperands(ImageOperands,
                                        mlir::spirv::ImageOperands::Sample);
      if (HasSample) {
        auto ImageType =
            mlir::dyn_cast<mlir::spirv::ImageType>(Op.getImage().getType());
        if (!ImageType || !isMultisampled2DImage(ImageType))
          return Rewriter.notifyMatchFailure(
              Op, "Sample image operand only supported for a "
                  "multisampled 2D storage image");
        if (Adaptor.getOperandArguments().size() != 1)
          return Rewriter.notifyMatchFailure(
              Op, "Sample image operand needs exactly one operand argument");
      }
    }
    if (hasImageOperands(ImageOperands) && !HasSample)
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    mlir::Type ResultType = this->getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Value Coordinate = Adaptor.getCoordinate();
    if (HasSample)
      Coordinate = appendVectorLane(Rewriter, Op.getLoc(), Coordinate,
                                    Adaptor.getOperandArguments()[0]);

    mlir::Value Pointer = createResourcePointer(Rewriter, Op.getLoc(),
                                                Adaptor.getImage(), Coordinate);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(Op, ResultType, Pointer);
    return mlir::success();
  }
};
using ImageReadPattern = ImageLoadPattern<mlir::spirv::ImageReadOp>;
using ImageFetchPattern = ImageLoadPattern<mlir::spirv::ImageFetchOp>;

/// The `spirv.GlobalVariable` \p Image's `spirv.Load` reads, tracing through
/// its `spirv.mlir.addressof`, or a null op if \p Image was not produced
/// that way (every subpassInput read this milestone supports is: GLSL/
/// glslang always loads the image handle from its own module-scope variable
/// immediately before reading it, exactly like every other resource image).
mlir::spirv::GlobalVariableOp getSubpassVariable(mlir::Value Image) {
  auto Load = Image.getDefiningOp<mlir::spirv::LoadOp>();
  if (!Load)
    return nullptr;
  auto AddrOf = Load.getPtr().getDefiningOp<mlir::spirv::AddressOfOp>();
  if (!AddrOf)
    return nullptr;
  return getReferencedGlobal(AddrOf);
}

/// Declares (or finds) the `feme.stage.subpass.load.f32` function
/// `SubpassLoadPattern` calls: `(i32 attachment_index, i32 component,
/// i32 sample) -> f32`, matching `feme::StageOpKind::SubpassLoad`'s
/// always-`f32` shape (see StageOps.h) -- an ordinary named call, not an
/// `llvm.spv.*` intrinsic, since `feme.stage.*` calls (StageOps.h's file
/// comment) are FeMe's own vocabulary rather than a real target-independent
/// LLVM intrinsic. Named with the explicit `.f32` type suffix
/// `feme::getOrInsertStageOp` gives every overloaded `feme.stage.*` op
/// (SubpassLoad is marked overloaded for exactly this reason -- see
/// `StageOpKind::SubpassLoad`'s comment): `feme::cpu::SIMDizePass` widens
/// this scalar declaration into a *different*, `<W x f32>`-returning one
/// later, and the two must not collide under one name, or
/// `CallBase::getCalledFunction`'s function-type check (used throughout
/// this codebase, not least `feme::isStageOpCall`) would refuse to
/// recognize either call once both exist.
mlir::LLVM::LLVMFuncOp
getOrInsertSubpassLoadFunc(mlir::ConversionPatternRewriter &Rewriter,
                           mlir::ModuleOp Module) {
  constexpr llvm::StringLiteral Name = "feme.stage.subpass.load.f32";
  if (auto Existing = Module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(Name))
    return Existing;
  mlir::OpBuilder::InsertionGuard Guard(Rewriter);
  Rewriter.setInsertionPointToStart(Module.getBody());
  auto FuncTy = mlir::LLVM::LLVMFunctionType::get(
      mlir::Float32Type::get(Rewriter.getContext()),
      {Rewriter.getI32Type(), Rewriter.getI32Type(), Rewriter.getI32Type()});
  return mlir::LLVM::LLVMFuncOp::create(Rewriter, Module.getLoc(), Name,
                                        FuncTy, mlir::LLVM::Linkage::External);
}

/// Converts a `spirv.ImageRead` whose image is `Dim::SubpassData` -- a GLSL
/// `subpassLoad()`, i.e. roadmap F8a's dynamic-rendering-local-read shader
/// side -- directly into one `feme.stage.subpass.load` call per result
/// component, rather than through `ImageReadPattern`'s ordinary resource-
/// handle load: a subpass input is not read from the bound descriptor's
/// image memory at all (see `feme::StageOpKind::SubpassLoad`'s comment and
/// "Render passes and dynamic rendering" in feme/docs/FeMeVulkanDesign.md),
/// so `Adaptor.getImage()` -- whatever `ResourceGlobalVariablePattern`/
/// `ResourceAddressOfPattern` converted the variable's own `handlefrom
/// binding` load to -- is deliberately never referenced; it is left to
/// become dead code once this pattern consumes every other use of the
/// `spirv.ImageRead`. The `InputAttachmentIndex` decoration this needs is
/// read directly off the underlying `spirv.GlobalVariable` (getSubpass
/// Variable), which the SPIR-V deserializer now preserves as a plain
/// `input_attachment_index` integer attribute (mlir/lib/Target/SPIRV/
/// Deserialization/Deserializer.cpp) -- there is no dedicated
/// `GlobalVariableOp` accessor for it, the same way `component`/`index`
/// are read as plain attributes elsewhere in this file
/// (buildStageIODecorationsAttr).
///
/// Roadmap F8c: a `subpassInputMS`'s explicit-sample `subpassLoad(input,
/// sample)` form lowers to `OpImageRead`'s lone `Sample` image operand
/// (optionally combined with the discarded `Nontemporal` bit, see
/// `hasImageOperands`) -- that one modifier is now accepted and its operand
/// argument threaded through as `feme.stage.subpass.load`'s third operand,
/// rather than being rejected like every other modifier; a plain
/// `subpassInput`'s implicit form (no image operands at all) still
/// synthesizes a constant `0` the same way it always has.
///
/// Registered at a higher benefit than `ImageReadPattern` (see
/// populateSPIRVToLLVMTargetPatterns) so it wins for the `Dim::SubpassData`
/// case; `ImageReadPattern` still handles every other image dimension.
class SubpassLoadPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageReadOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageReadOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageReadOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto ImageType = mlir::dyn_cast<mlir::spirv::ImageType>(Op.getImage().getType());
    if (!ImageType || ImageType.getDim() != mlir::spirv::Dim::SubpassData)
      return Rewriter.notifyMatchFailure(Op, "not a subpass-data image read");
    std::optional<mlir::spirv::ImageOperands> ImageOperands =
        Op.getImageOperands();
    bool HasSample =
        hasExactImageOperands(ImageOperands, mlir::spirv::ImageOperands::Sample);
    if (hasImageOperands(ImageOperands) && !HasSample)
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");
    if (HasSample && Adaptor.getOperandArguments().size() != 1)
      return Rewriter.notifyMatchFailure(
          Op, "Sample image operand needs exactly one operand argument");

    mlir::spirv::GlobalVariableOp Global = getSubpassVariable(Op.getImage());
    if (!Global)
      return Rewriter.notifyMatchFailure(
          Op, "subpass image is not read directly from its own variable");
    auto IndexAttr =
        Global->getAttrOfType<mlir::IntegerAttr>("input_attachment_index");
    if (!IndexAttr)
      return Rewriter.notifyMatchFailure(
          Op, "subpass image variable has no InputAttachmentIndex decoration");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto VectorTy = mlir::dyn_cast<mlir::VectorType>(ResultType);
    unsigned NumComponents = VectorTy ? VectorTy.getNumElements() : 1;

    mlir::Location Loc = Op.getLoc();
    mlir::LLVM::LLVMFuncOp Callee = getOrInsertSubpassLoadFunc(
        Rewriter, Op->getParentOfType<mlir::ModuleOp>());
    mlir::Value IndexConst = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI32Type(),
        Rewriter.getI32IntegerAttr(static_cast<int32_t>(IndexAttr.getInt())));
    mlir::Value SampleVal =
        HasSample ? Adaptor.getOperandArguments()[0]
                  : mlir::LLVM::ConstantOp::create(
                        Rewriter, Loc, Rewriter.getI32Type(),
                        Rewriter.getI32IntegerAttr(0));

    mlir::Value Result =
        VectorTy ? mlir::LLVM::PoisonOp::create(Rewriter, Loc, VectorTy)
                 : mlir::Value();
    for (unsigned Component = 0; Component != NumComponents; ++Component) {
      mlir::Value ComponentConst = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI32Type(),
          Rewriter.getI32IntegerAttr(static_cast<int32_t>(Component)));
      mlir::Value Scalar =
          mlir::LLVM::CallOp::create(
              Rewriter, Loc, Callee,
              mlir::ValueRange{IndexConst, ComponentConst, SampleVal})
              .getResult();
      if (!VectorTy) {
        Result = Scalar;
        break;
      }
      mlir::Value LaneIndex = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI64Type(),
          Rewriter.getI64IntegerAttr(Component));
      Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                   Scalar, LaneIndex);
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Declares (or finds) the `feme.stage.stream.*` function \p Name calls:
/// `(i32 stream) -> void`, matching `feme::StageOpKind::StreamEmit`/
/// `StreamCut`'s shape (StageOps.h) -- an ordinary named call, not an
/// `llvm.spv.*` intrinsic, for the same reason `getOrInsertSubpassLoadFunc`
/// above is: `feme.stage.*` calls are FeMe's own vocabulary, not a real
/// target-independent LLVM intrinsic, and neither `StreamEmit` nor
/// `StreamCut` is overloaded (StageOps.cpp's table), so \p Name is used
/// as-is with no type-mangling suffix.
mlir::LLVM::LLVMFuncOp
getOrInsertStreamOpFunc(mlir::ConversionPatternRewriter &Rewriter,
                        mlir::ModuleOp Module, llvm::StringRef Name) {
  if (auto Existing = Module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(Name))
    return Existing;
  mlir::OpBuilder::InsertionGuard Guard(Rewriter);
  Rewriter.setInsertionPointToStart(Module.getBody());
  auto FuncTy = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(Rewriter.getContext()),
      {Rewriter.getI32Type()});
  return mlir::LLVM::LLVMFuncOp::create(Rewriter, Module.getLoc(), Name, FuncTy,
                                        mlir::LLVM::Linkage::External);
}

/// Converts `spirv.EmitVertex` (GLSL geometry shader `EmitVertex()`) into a
/// call to `feme.stage.stream.emit(0)` -- the same
/// `feme::StageOpKind::StreamEmit` intrinsic
/// `feme::cpu::lowerGeometryStreamEmit` (GeometryWrapper.cpp, built under
/// roadmap G5) already knows how to lower into a
/// `GeometryStreamBuilder::emit`, mirroring how every other stage-IO
/// SPIR-V op already routes through a `feme.stage.*` intrinsic rather than
/// a bespoke LLVM IR shape (roadmap H5e-a). The stream operand is always a
/// constant `0`: `spirv.EmitVertex`'s own SPIR-V spec text requires it be
/// used "only ... when only one stream is present" (multiple output
/// streams are a later milestone, not yet supported by
/// `GeometryState`/`FemeGeometryArgs`). Not a terminator -- like
/// `spirv.DemoteToHelperInvocation` above, this op is simply erased in
/// place once its call is emitted.
class EmitVertexConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::EmitVertexOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::EmitVertexOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::EmitVertexOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::LLVM::LLVMFuncOp Callee =
        getOrInsertStreamOpFunc(Rewriter, Op->getParentOfType<mlir::ModuleOp>(),
                                "feme.stage.stream.emit");
    mlir::Value StreamConst = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI32Type(), Rewriter.getI32IntegerAttr(0));
    mlir::LLVM::CallOp::create(Rewriter, Loc, Callee,
                               mlir::ValueRange{StreamConst});
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Converts `spirv.EndPrimitive` (GLSL geometry shader `EndPrimitive()`)
/// into a call to `feme.stage.stream.cut(0)`, mirroring
/// `EmitVertexConversionPattern` above exactly except for the callee name
/// and the `feme::cpu::lowerGeometryStreamCut` consumer it targets.
class EndPrimitiveConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::EndPrimitiveOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::EndPrimitiveOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::EndPrimitiveOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::LLVM::LLVMFuncOp Callee =
        getOrInsertStreamOpFunc(Rewriter, Op->getParentOfType<mlir::ModuleOp>(),
                                "feme.stage.stream.cut");
    mlir::Value StreamConst = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI32Type(), Rewriter.getI32IntegerAttr(0));
    mlir::LLVM::CallOp::create(Rewriter, Loc, Callee,
                               mlir::ValueRange{StreamConst});
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Declares (or finds) the `feme.stage.set_mesh_outputs` function: `(i32
/// vertex_count, i32 primitive_count) -> void`, matching
/// `feme::StageOpKind::SetMeshOutputs`'s shape (StageOps.h) -- an ordinary
/// named call, not overloaded (both operands are always `i32`, mirroring
/// `getOrInsertStreamOpFunc`'s own single-name-no-mangling convention for
/// `StreamEmit`/`StreamCut`), so \p Module gets at most one declaration of
/// it.
mlir::LLVM::LLVMFuncOp
getOrInsertSetMeshOutputsFunc(mlir::ConversionPatternRewriter &Rewriter,
                              mlir::ModuleOp Module) {
  constexpr llvm::StringLiteral Name = "feme.stage.set_mesh_outputs";
  if (auto Existing = Module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(Name))
    return Existing;
  mlir::OpBuilder::InsertionGuard Guard(Rewriter);
  Rewriter.setInsertionPointToStart(Module.getBody());
  auto FuncTy = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(Rewriter.getContext()),
      {Rewriter.getI32Type(), Rewriter.getI32Type()});
  return mlir::LLVM::LLVMFuncOp::create(Rewriter, Module.getLoc(), Name, FuncTy,
                                        mlir::LLVM::Linkage::External);
}

/// Converts `spirv.SetMeshOutputsEXT` (roadmap H6c-a-a-i) into a call to
/// `feme.stage.set_mesh_outputs(vertexCount, primitiveCount)`, the same
/// "route straight into a `feme.stage.*` intrinsic rather than a bespoke
/// LLVM IR shape" treatment `EmitVertexConversionPattern`/
/// `EndPrimitiveConversionPattern` above already give the geometry stage's
/// own no-signature-element ops. `feme::cpu::MeshOutputWrapperPass`
/// (MeshOutputWrapper.cpp) is what actually lowers the resulting call, into
/// `FemeMeshArgs::ActualVertexCount`/`ActualPrimitiveCount`. Not a
/// terminator -- like `EmitVertex`/`EndPrimitive`, this op is simply erased
/// in place once its call is emitted.
class SetMeshOutputsEXTConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::EXTSetMeshOutputsOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::EXTSetMeshOutputsOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::EXTSetMeshOutputsOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::LLVM::LLVMFuncOp Callee = getOrInsertSetMeshOutputsFunc(
        Rewriter, Op->getParentOfType<mlir::ModuleOp>());
    mlir::LLVM::CallOp::create(Rewriter, Loc, Callee,
                               mlir::ValueRange{Adaptor.getVertexCount(),
                                                Adaptor.getPrimitiveCount()});
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Declares (or finds) the `feme.stage.emit_mesh_tasks` function: `(i32
/// group_count_x, i32 group_count_y, i32 group_count_z) -> void`, matching
/// `feme::StageOpKind::EmitMeshTasks`'s shape (StageOps.h), the same
/// unmangled single-declaration convention `getOrInsertSetMeshOutputsFunc`
/// above already uses.
mlir::LLVM::LLVMFuncOp
getOrInsertEmitMeshTasksFunc(mlir::ConversionPatternRewriter &Rewriter,
                             mlir::ModuleOp Module) {
  constexpr llvm::StringLiteral Name = "feme.stage.emit_mesh_tasks";
  if (auto Existing = Module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(Name))
    return Existing;
  mlir::OpBuilder::InsertionGuard Guard(Rewriter);
  Rewriter.setInsertionPointToStart(Module.getBody());
  auto FuncTy = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(Rewriter.getContext()),
      {Rewriter.getI32Type(), Rewriter.getI32Type(), Rewriter.getI32Type()});
  return mlir::LLVM::LLVMFuncOp::create(Rewriter, Module.getLoc(), Name, FuncTy,
                                        mlir::LLVM::Linkage::External);
}

/// Converts `spirv.EXT.EmitMeshTasks` (roadmap H6s) into a call to
/// `feme.stage.emit_mesh_tasks(groupCountX, groupCountY, groupCountZ)`
/// followed by an `llvm.return`, the same "route straight into a
/// `feme.stage.*` intrinsic" treatment `SetMeshOutputsEXTConversionPattern`
/// above already gives the mesh stage's own no-signature-element op --
/// except, unlike that op, `spirv.EXT.EmitMeshTasks` **is** a terminator
/// (the SPIR-V spec requires it be the last instruction in its block, and
/// says it "ceases all further processing"), so simply erasing it in place
/// like `SetMeshOutputsEXTConversionPattern` does would leave its block
/// with no terminator at all. `feme::cpu::TaskPayloadWrapperPass`
/// (TaskPayloadWrapper.cpp) is what actually lowers the resulting call,
/// writing the requested group count into `FemeTaskArgs::MeshGroupCount`.
///
/// The op's own optional `Payload` operand (a pointer to whichever
/// `TaskPayloadWorkgroupEXT`-storage-class global variable this task
/// workgroup wrote) is intentionally never read here: it carries no value
/// of its own to forward, only identifying *which* global was written, and
/// every write to that global already converted to a
/// `feme.stage.task.payload.store` call earlier in this same block (via
/// this file's `TaskPayloadWorkgroupEXT` global-variable/store handling) --
/// by the time this op converts, the payload's real bytes are already
/// sitting in `FemeTaskArgs::Payload`, verbatim, with nothing left for this
/// op to do with the pointer itself.
class EmitMeshTasksEXTConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::EXTEmitMeshTasksOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::EXTEmitMeshTasksOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::EXTEmitMeshTasksOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::LLVM::LLVMFuncOp Callee = getOrInsertEmitMeshTasksFunc(
        Rewriter, Op->getParentOfType<mlir::ModuleOp>());
    mlir::LLVM::CallOp::create(
        Rewriter, Loc, Callee,
        mlir::ValueRange{Adaptor.getGroupCountX(), Adaptor.getGroupCountY(),
                         Adaptor.getGroupCountZ()});
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ReturnOp>(Op,
                                                      mlir::ValueRange{});
    return mlir::success();
  }
};

/// Converts a `spirv.ImageFetch` with the `Lod` image operand, optionally
/// combined with a real `ConstOffset` (roadmap L72(b): GLSL's
/// `texelFetchOffset()`, confirmed via a real `deqp-vk` CTS shader capture
/// against `dEQP-VK.glsl.texture_functions.texelfetchoffset.*_compute` to
/// emit exactly this `Lod|ConstOffset` combination -- unlike the
/// `ImageSampleDrefExplicitLod` half of the same roadmap row, whose real gap
/// turned out to be an unrelated literal-zero-Lod restriction, this
/// `ImageFetch` pattern's own restriction genuinely was "no `ConstOffset`
/// support at all", exactly as the roadmap row originally described) into
/// the `llvm.spv.resource.load.level` intrinsic call, mirroring
/// `ImageFetchPattern`'s unmodified case above but threading the explicit
/// mip level through instead of rejecting it -- see
/// `llvm/test/CodeGen/SPIRV/hlsl-resources/LoadLevel.ll` for the backend
/// side of this intrinsic, which (like `ImageFetchPattern`'s plain load)
/// selects `OpImageFetch` vs `OpImageRead` itself. `dxc` always emits an
/// explicit `Lod` operand for `Texture2D<T>::Load` (even a literal 0 mip),
/// so without this pattern no ordinary non-multisampled texel fetch
/// converts at all.
class ImageFetchLodPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageFetchOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageFetchOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageFetchOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    if (!mlir::spirv::bitEnumContainsAny(Actual,
                                         mlir::spirv::ImageOperands::Lod))
      return Rewriter.notifyMatchFailure(Op, "Lod image operand is required");

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Lod |
        mlir::spirv::ImageOperands::ConstOffset;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Coordinate = Adaptor.getCoordinate();

    // Positional order follows the fixed SPIR-V Image Operands bit order
    // (`Bias, Lod, Grad, ConstOffset, ...`, see `SPIRV_BitEnumAttr<
    // "ImageOperands", ...>`): `Lod` first (always present, matched above),
    // then `ConstOffset` if present.
    mlir::ValueRange OperandArguments = Adaptor.getOperandArguments();
    size_t Index = 0;
    mlir::Value Lod = OperandArguments[Index++];
    mlir::Value Offset =
        HasConstOffset ? OperandArguments[Index++] : mlir::Value();
    if (Index != OperandArguments.size())
      return Rewriter.notifyMatchFailure(Op, "unexpected operand count");

    // `llvm.spv.resource.load.level` always takes a texel offset; use the
    // real `ConstOffset` value if the op had one, otherwise zero.
    auto ImageTy = mlir::cast<mlir::spirv::ImageType>(Op.getImage().getType());
    mlir::Type OffsetType =
        getDefaultZeroOffsetType(ImageTy, Coordinate.getType(), Rewriter);
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.load.level",
                                ResultType,
                                {Adaptor.getImage(), Coordinate, Lod, Offset}));
    return mlir::success();
  }
};

/// Converts `spirv.ImageWrite` into a store through the written location.
/// Roadmap H19g/H19m: like `ImageLoadPattern` above, a lone `Sample` image
/// operand is accepted for a plain or arrayed multisampled 2D storage
/// image, appended to the coordinate the same way.
class ImageWritePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageWriteOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageWriteOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageWriteOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperands =
        Op.getImageOperands();
    bool HasSample =
        hasExactImageOperands(ImageOperands, mlir::spirv::ImageOperands::Sample);
    if (HasSample) {
      auto ImageType =
          mlir::dyn_cast<mlir::spirv::ImageType>(Op.getImage().getType());
      if (!ImageType || !isMultisampled2DImage(ImageType))
        return Rewriter.notifyMatchFailure(
            Op, "Sample image operand only supported for a "
                "multisampled 2D storage image");
      if (Adaptor.getOperandArguments().size() != 1)
        return Rewriter.notifyMatchFailure(
            Op, "Sample image operand needs exactly one operand argument");
    }
    if (hasImageOperands(ImageOperands) && !HasSample)
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    mlir::Value Coordinate = Adaptor.getCoordinate();
    if (HasSample)
      Coordinate = appendVectorLane(Rewriter, Op.getLoc(), Coordinate,
                                    Adaptor.getOperandArguments()[0]);

    mlir::Value Pointer = createResourcePointer(Rewriter, Op.getLoc(),
                                                Adaptor.getImage(), Coordinate);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::StoreOp>(Op, Adaptor.getTexel(),
                                                     Pointer);
    return mlir::success();
  }
};

/// Converts `spirv.ImageTexelPointer` (roadmap R39/H8u) into the same
/// `llvm.spv.resource.getpointer` call `ImageReadPattern`/`ImageWritePattern`
/// use to form a storage-image texel address, so a following
/// `spirv.Atomic*` op (already converted generically by MLIR's own
/// upstream `spirv` -> `llvm` patterns, since every `Atomic*` op is generic
/// over any pointer type) becomes an ordinary LLVM `atomicrmw`/`cmpxchg`
/// against that pointer with no new intrinsic of its own.
///
/// `$image`'s type is a pointer-to-image (`!spirv.ptr<!spirv.image<...>,
/// UniformConstant>`), unlike `ImageRead`'s/`ImageWrite`'s own loaded-image-
/// value `$image` operand -- but `ResourceAddressOfPattern` already
/// converts a resource variable's `spirv.mlir.addressof` directly to the
/// handle value itself (see its own comment), so `Adaptor.getImage()` here
/// is already that same handle, needing no further indirection.
class ImageTexelPointerPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageTexelPointerOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageTexelPointerOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageTexelPointerOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto ImagePtrType =
        mlir::cast<mlir::spirv::PointerType>(Op.getImage().getType());
    auto ImageType =
        mlir::cast<mlir::spirv::ImageType>(ImagePtrType.getPointeeType());

    mlir::Value Coordinate = Adaptor.getCoordinate();
    if (isMultisampled2DImage(ImageType))
      Coordinate = appendVectorLane(Rewriter, Op.getLoc(), Coordinate,
                                    Adaptor.getSample());
    // A single-sampled image's `Sample` operand carries no address
    // information of its own -- the SPIR-V spec requires it to be 0 in
    // that case (its value is otherwise undefined) -- so every other
    // image dimension this converter already handles (Plain2D/Array2D/
    // Plain3D) needs no change to admit `ImageTexelPointer` alongside
    // `ImageRead`/`ImageWrite`.

    Rewriter.replaceOp(Op, createResourcePointer(Rewriter, Op.getLoc(),
                                                 Adaptor.getImage(),
                                                 Coordinate));
    return mlir::success();
  }
};

/// Roadmap R39/H8u: MLIR's own upstream `spirv` -> `llvm` conversion has no
/// pattern at all for any `spirv.Atomic*` op (confirmed by grep -- unlike
/// the ordinary arithmetic/logical ops, which upstream already converts
/// with `DirectConversionPattern`), so every atomic op this converter
/// needs to support against a storage-image texel pointer (formed by
/// `ImageTexelPointerPattern` above) needs its own `feme`-side pattern.
///
/// Maps a SPIR-V `MemorySemantics` bitmask onto the closest LLVM
/// `AtomicOrdering`. This converter targets a single CPU device with one
/// coherent address space, so there is no weaker-than-`seq_cst` ordering
/// that is unsafe to use -- `seq_cst` is picked whenever none of the
/// ordering-relevant bits are set, which is always correct (if
/// conservative) for every caller.
static mlir::LLVM::AtomicOrdering
convertAtomicOrdering(mlir::spirv::MemorySemantics Semantics) {
  bool Acquire =
      mlir::spirv::bitEnumContainsAll(Semantics, mlir::spirv::MemorySemantics::Acquire);
  bool Release =
      mlir::spirv::bitEnumContainsAll(Semantics, mlir::spirv::MemorySemantics::Release);
  if (Acquire && Release)
    return mlir::LLVM::AtomicOrdering::acq_rel;
  if (Acquire)
    return mlir::LLVM::AtomicOrdering::acquire;
  if (Release)
    return mlir::LLVM::AtomicOrdering::release;
  return mlir::LLVM::AtomicOrdering::seq_cst;
}

/// Converts an `AtomicUpdateWithValueOp`-shaped `spirv.Atomic*` RMW op (and
/// `spirv.AtomicExchange`, which shares the same `pointer`/`memory_scope`/
/// `semantics`/`value` operand shape) into an `llvm.atomicrmw` of the given
/// `BinOp` kind.
template <typename SPIRVOpTy, mlir::LLVM::AtomicBinOp BinOp>
class AtomicRMWPattern : public mlir::SPIRVToLLVMConversion<SPIRVOpTy> {
public:
  using mlir::SPIRVToLLVMConversion<SPIRVOpTy>::SPIRVToLLVMConversion;
  using OpAdaptor = typename mlir::SPIRVToLLVMConversion<SPIRVOpTy>::OpAdaptor;

  mlir::LogicalResult
  matchAndRewrite(SPIRVOpTy Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::LLVM::AtomicOrdering Ordering =
        convertAtomicOrdering(Op.getSemantics());
    Rewriter.replaceOpWithNewOp<mlir::LLVM::AtomicRMWOp>(
        Op, BinOp, Adaptor.getPointer(), Adaptor.getValue(), Ordering);
    return mlir::success();
  }
};

/// Converts `spirv.AtomicCompareExchange` into an `llvm.cmpxchg` plus the
/// `extractvalue` picking out the *old* value -- SPIR-V's own result is
/// always the value that was in memory before the swap, whether or not the
/// comparison succeeded, matching `llvm.cmpxchg`'s first result element.
class AtomicCompareExchangePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::AtomicCompareExchangeOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::AtomicCompareExchangeOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::AtomicCompareExchangeOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::LLVM::AtomicOrdering SuccessOrdering =
        convertAtomicOrdering(Op.getEqualSemantics());
    mlir::LLVM::AtomicOrdering FailureOrdering =
        convertAtomicOrdering(Op.getUnequalSemantics());
    auto CmpXchg = mlir::LLVM::AtomicCmpXchgOp::create(
        Rewriter, Op.getLoc(), Adaptor.getPointer(), Adaptor.getComparator(),
        Adaptor.getValue(), SuccessOrdering, FailureOrdering);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
        Op, CmpXchg.getResult(), llvm::ArrayRef<int64_t>{0});
    return mlir::success();
  }
};

/// Converts `spirv.ImageQuerySize` into the `llvm.spv.resource.getdimensions`
/// intrinsic returning as many dimensions as the query asks for.
class ImageQuerySizePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageQuerySizeOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageQuerySizeOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageQuerySizeOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    auto VectorTy = mlir::dyn_cast<mlir::VectorType>(ResultType);
    int64_t Dimensions = VectorTy ? VectorTy.getNumElements() : 1;
    llvm::StringRef Intrinsic;
    switch (Dimensions) {
    case 1:
      Intrinsic = "llvm.spv.resource.getdimensions.x";
      break;
    case 2:
      Intrinsic = "llvm.spv.resource.getdimensions.xy";
      break;
    case 3:
      Intrinsic = "llvm.spv.resource.getdimensions.xyz";
      break;
    default:
      return Rewriter.notifyMatchFailure(Op, "unsupported dimension count");
    }

    Rewriter.replaceOp(Op, createIntrinsicCall(Rewriter, Op.getLoc(), Intrinsic,
                                               ResultType, Adaptor.getImage()));
    return mlir::success();
  }
};

/// Converts `spirv.Image`, which extracts the image handle back out of a
/// combined `!spirv.sampled_image` value (e.g. so it can feed an
/// `spirv.ImageFetch`/`spirv.ImageQuerySize`, both of which -- unlike an
/// actual sample -- take a plain image handle, not a sampled one), into an
/// `llvm.extractvalue` reading field 0 of the very
/// `!llvm.struct<(ImageHandle, SamplerHandle)>` SampledImagePattern (below)
/// builds -- the inverse of that pattern's `llvm.insertvalue` pair.
class ImagePattern : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ImageHandleType = getTypeConverter()->convertType(Op.getType());
    if (!ImageHandleType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    Rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
        Op, ImageHandleType, Adaptor.getSampledImage(),
        llvm::ArrayRef<int64_t>{0});
    return mlir::success();
  }
};

/// Converts `spirv.SampledImage`, which combines an image and a sampler
/// handle into one `!spirv.sampled_image` value, into the
/// `!llvm.struct<(ImageHandle, SamplerHandle)>` FeMe's own
/// `spirv.SampledImageType` conversion produces (see
/// populateSPIRVToLLVMTargetTypeConversions): unlike MLIR's own conversion,
/// which folds both handles into one combined target extension type for the
/// SPIR-V *runner*, LLVM's SPIRV backend intrinsics for sampling
/// (`llvm.spv.resource.sample*`) take the image and sampler handles as two
/// separate arguments, so nothing needs a single combined handle value; the
/// struct is just a vehicle for carrying both through the dialect
/// conversion until ImageSampleImplicitLodPattern unpacks it again.
class SampledImagePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::SampledImageOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::SampledImageOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::SampledImageOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type StructType = getTypeConverter()->convertType(Op.getType());
    if (!StructType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Result =
        mlir::LLVM::PoisonOp::create(Rewriter, Loc, StructType);
    Result = mlir::LLVM::InsertValueOp::create(
        Rewriter, Loc, Result, Adaptor.getImage(), llvm::ArrayRef<int64_t>{0});
    Result = mlir::LLVM::InsertValueOp::create(Rewriter, Loc, Result,
                                               Adaptor.getSampler(),
                                               llvm::ArrayRef<int64_t>{1});
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts a `spirv.ImageSampleImplicitLod` with any combination of `Bias`,
/// `ConstOffset`, and `MinLod` (a min-LOD clamp, HLSL's `Texture2D::Sample`'s
/// own optional trailing `clamp` argument) into the
/// `llvm.spv.resource.sample`/`llvm.spv.resource.samplebias` (or their
/// `.clamp` siblings, once `MinLod` is present) intrinsic call LLVM's SPIRV
/// backend selects `OpSampledImage`+`OpImageSampleImplicitLod` from -- see
/// `llvm/test/CodeGen/SPIRV/hlsl-resources/{Sample,SampleBias}.ll`'s own
/// `res2`/`res3` cases for the `.clamp` intrinsics' exact operand order
/// (image, sampler, coord, [bias,] offset, clamp). The backend itself
/// decides whether to actually emit the `ConstOffset` image operand,
/// folding it away when the offset constant is all-zero (independently of
/// whether `MinLod` is also present, see `Sample.ll`'s own `res2` case: a
/// zero offset with a non-zero clamp still emits `MinLod` alone, not
/// `ConstOffset|MinLod`), so this pattern can always thread the real
/// offset/bias/clamp values through uniformly instead of hardcoding a
/// default only when each operand bit was absent. Roadmap L22: gradient/
/// comparison/gather variants (which need still more operands this pattern
/// does not yet supply) remain uncovered -- see the "Known gap" note in
/// the SPIR-V section of `feme/docs/Design.md`.
class ImageSampleImplicitLodPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleImplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleImplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleImplicitLodOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    // `Actual` must be a subset of the three bits this pattern understands
    // (in any combination, including none of them) -- `bitEnumContainsAll`
    // checks the first argument contains every bit the second has, so this
    // checks the reverse of its usual "does X have all of Y" reading:
    // "does the supported mask have every bit `Actual` itself sets".
    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Bias |
        mlir::spirv::ImageOperands::ConstOffset |
        mlir::spirv::ImageOperands::MinLod;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasBias = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::Bias);
    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);
    bool HasMinLod = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::MinLod);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});

    mlir::Value Coordinate = Adaptor.getCoordinate();
    auto ImageTy = mlir::cast<mlir::spirv::ImageType>(
        mlir::cast<mlir::spirv::SampledImageType>(
            Op.getSampledImage().getType())
            .getImageType());
    mlir::Type OffsetType =
        getDefaultZeroOffsetType(ImageTy, Coordinate.getType(), Rewriter);

    // Positional order follows the fixed SPIR-V Image Operands bit order
    // (`Bias, Lod, Grad, ConstOffset, Offset, ConstOffsets, Sample,
    // MinLod, ...`, see `SPIRV_BitEnumAttr<"ImageOperands", ...>`): `Bias`
    // first, then `ConstOffset`, then `MinLod` last, whichever subset is
    // actually present.
    mlir::ValueRange OperandArguments = Adaptor.getOperandArguments();
    size_t Index = 0;
    mlir::Value Bias = HasBias ? OperandArguments[Index++] : mlir::Value();
    mlir::Value Offset =
        HasConstOffset ? OperandArguments[Index++] : mlir::Value();
    mlir::Value Clamp = HasMinLod ? OperandArguments[Index++] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    llvm::StringRef IntrinsicName;
    llvm::SmallVector<mlir::Value, 6> Arguments = {Image, Sampler, Coordinate};
    if (Bias)
      Arguments.push_back(Bias);
    Arguments.push_back(Offset);
    if (Clamp)
      Arguments.push_back(Clamp);
    if (Bias)
      IntrinsicName = Clamp ? "llvm.spv.resource.samplebias.clamp"
                            : "llvm.spv.resource.samplebias";
    else
      IntrinsicName =
          Clamp ? "llvm.spv.resource.sample.clamp" : "llvm.spv.resource.sample";

    Rewriter.replaceOp(Op, createIntrinsicCall(Rewriter, Loc, IntrinsicName,
                                               ResultType, Arguments));
    return mlir::success();
  }
};
/// Converts a `spirv.ImageSampleExplicitLod` with the `Lod` image operand
/// (mandatory here -- SPIR-V requires exactly one of `Lod`/`Grad` on this
/// op), optionally combined with `ConstOffset` (roadmap L68: GLSL's own
/// vertex-stage `texture()`/`textureOffset()` calls lower to an explicit
/// `Lod` rather than `ImageSampleImplicitLod`, since vertex shaders have no
/// automatic derivatives to drive an implicit LOD -- confirmed via a real
/// CTS sweep hitting `Lod|ConstOffset` identically across every shape's own
/// `_vertex`-stage `textureoffset*` group, not modeled here before this
/// roadmap row), into the `llvm.spv.resource.samplelevel` intrinsic call,
/// mirroring `ImageSampleImplicitLodPattern`'s own combinatorial operand
/// handling above (see roadmap R30, "SPIR-V (including Design.md's §1.2
/// sampling variants)"). Unlike `ImageSampleImplicitLodPattern`'s `Bias`/
/// `MinLod`, `Lod` has no optional counterpart of its own here to omit --
/// it is this op's own mandatory modifier, not a combinatorial choice --
/// so only `ConstOffset`'s presence varies the operand count. A `Grad`
/// (gradient) operand -- `ImageSampleExplicitLod`'s other legal modifier --
/// is handled by the separate `ImageSampleGradPattern` below instead, since
/// it needs an entirely different intrinsic/operand shape and SPIR-V
/// forbids combining `Lod` and `Grad` on the same instruction; this
/// pattern's own missing-`Lod` check below simply fails to match a `Grad`
/// call, letting the greedy pattern rewriter try `ImageSampleGradPattern`
/// next (both are registered against the same `ImageSampleExplicitLodOp`
/// type, see `populateSPIRVToLLVMTargetPatterns`).
class ImageSampleExplicitLodPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleExplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleExplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleExplicitLodOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    if (!mlir::spirv::bitEnumContainsAny(Actual,
                                         mlir::spirv::ImageOperands::Lod))
      return Rewriter.notifyMatchFailure(Op, "Lod image operand is required");

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Lod |
        mlir::spirv::ImageOperands::ConstOffset;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});

    // Positional order follows the fixed SPIR-V Image Operands bit order
    // (`Bias, Lod, Grad, ConstOffset, ...`, see `SPIRV_BitEnumAttr<
    // "ImageOperands", ...>`): `Lod` first (always present), then
    // `ConstOffset` if present.
    mlir::ValueRange OperandArguments = Adaptor.getOperandArguments();
    size_t Index = 0;
    mlir::Value Lod = OperandArguments[Index++];
    mlir::Value Offset =
        HasConstOffset ? OperandArguments[Index++] : mlir::Value();
    if (Index != OperandArguments.size())
      return Rewriter.notifyMatchFailure(Op, "unexpected operand count");

    mlir::Value Coordinate = Adaptor.getCoordinate();
    auto ImageTy = mlir::cast<mlir::spirv::ImageType>(
        mlir::cast<mlir::spirv::SampledImageType>(
            Op.getSampledImage().getType())
            .getImageType());
    mlir::Type OffsetType =
        getDefaultZeroOffsetType(ImageTy, Coordinate.getType(), Rewriter);
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.samplelevel",
                                ResultType,
                                {Image, Sampler, Coordinate, Lod, Offset}));
    return mlir::success();
  }
};

/// Converts a `spirv.ImageSampleExplicitLod` with the `Grad` image operand
/// (an explicit pair of screen-space partial-derivative vectors, GLSL's
/// `textureGrad()`/`textureGradOffset()`, HLSL's `Texture2D::SampleGrad`),
/// optionally combined with `ConstOffset` and/or `MinLod`, into the
/// `llvm.spv.resource.samplegrad`/`.samplegrad.clamp` intrinsic call --
/// mirroring `ImageSampleImplicitLodPattern`'s own combinatorial handling
/// of `Bias`/`ConstOffset`/`MinLod` above, but for `Grad`'s two vector
/// operands instead of a single scalar `Bias`. Per the fixed SPIR-V Image
/// Operands bit order (`Bias, Lod, Grad, ConstOffset, Offset,
/// ConstOffsets, Sample, MinLod, ...`), `Grad`'s own pair -- `dPdx` then
/// `dPdy` -- precedes `ConstOffset`, which itself precedes `MinLod`,
/// exactly as `int_spv_resource_samplegrad{,_clamp}`'s own operand list
/// expects (`llvm/include/llvm/IR/IntrinsicsSPIRV.td`: `(image, sampler,
/// coord, dPdx, dPdy, offset[, clamp])`). Unlike `ImageSampleImplicitLodPattern`,
/// there is no bare `Grad`-less `.clamp`/non-`.clamp` pair to also cover
/// here -- `Grad` is this pattern's own mandatory operand, not an optional
/// modifier of some other base intrinsic -- so a missing `Grad` bit simply
/// fails to match, falling through to `ImageSampleExplicitLodPattern`
/// above for a plain `Lod` operand instead (see its own updated doc).
class ImageSampleGradPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleExplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleExplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleExplicitLodOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    if (!mlir::spirv::bitEnumContainsAny(Actual,
                                         mlir::spirv::ImageOperands::Grad))
      return Rewriter.notifyMatchFailure(Op, "no Grad image operand");

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Grad |
        mlir::spirv::ImageOperands::ConstOffset |
        mlir::spirv::ImageOperands::MinLod;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);
    bool HasMinLod = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::MinLod);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});

    mlir::Value Coordinate = Adaptor.getCoordinate();
    auto ImageTy = mlir::cast<mlir::spirv::ImageType>(
        mlir::cast<mlir::spirv::SampledImageType>(
            Op.getSampledImage().getType())
            .getImageType());
    mlir::Type OffsetType =
        getDefaultZeroOffsetType(ImageTy, Coordinate.getType(), Rewriter);

    // `Grad`'s own pair (`dPdx` then `dPdy`) always comes first -- it is
    // this pattern's own mandatory operand, unlike `ImageSampleImplicitLodPattern`'s
    // optional `Bias` -- followed by `ConstOffset`, then `MinLod`,
    // whichever of the latter two subset is actually present.
    mlir::ValueRange OperandArguments = Adaptor.getOperandArguments();
    size_t Index = 0;
    mlir::Value DPdx = OperandArguments[Index++];
    mlir::Value DPdy = OperandArguments[Index++];
    mlir::Value Offset =
        HasConstOffset ? OperandArguments[Index++] : mlir::Value();
    mlir::Value Clamp = HasMinLod ? OperandArguments[Index++] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    llvm::SmallVector<mlir::Value, 7> Arguments = {Image, Sampler, Coordinate,
                                                    DPdx, DPdy, Offset};
    if (Clamp)
      Arguments.push_back(Clamp);
    llvm::StringRef IntrinsicName = Clamp
                                        ? "llvm.spv.resource.samplegrad.clamp"
                                        : "llvm.spv.resource.samplegrad";

    Rewriter.replaceOp(Op, createIntrinsicCall(Rewriter, Loc, IntrinsicName,
                                               ResultType, Arguments));
    return mlir::success();
  }
};

/// Converts a `spirv.ImageSampleDrefImplicitLod` with any combination of
/// `Bias`, `ConstOffset`, and `MinLod` into the corresponding one of the
/// four `llvm.spv.resource.samplecmp`/`.samplecmp.clamp`/`.samplecmpbias`/
/// `.samplecmpbias.clamp` intrinsic calls LLVM's SPIRV backend selects
/// `OpSampledImage`+`OpImageSampleDrefImplicitLod` from -- see
/// `llvm/test/CodeGen/SPIRV/hlsl-resources/SampleCmp.ll`'s own operand
/// order (image, sampler, coord, dref, [bias,] offset[, clamp]),
/// mirroring `ImageSampleImplicitLodPattern` above. The non-bias pair is
/// the depth-comparison sibling HLSL's `Texture*::SampleCmp` compiles
/// down to (roadmap L25); the `samplecmpbias` pair (roadmap L52(b)) has
/// no HLSL spelling but is what GLSL's own
/// `texture(sampler2DShadow, coord, bias)` emits, so a real SPIR-V module
/// imported from GLSL reaches it where an HLSL-originated one never
/// would.
class ImageSampleDrefImplicitLodPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleDrefImplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleDrefImplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleDrefImplicitLodOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Bias |
        mlir::spirv::ImageOperands::ConstOffset |
        mlir::spirv::ImageOperands::MinLod;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasBias = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::Bias);
    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);
    bool HasMinLod = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::MinLod);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Dref = Adaptor.getDref();

    mlir::Value Coordinate = Adaptor.getCoordinate();
    // Unlike an ordinary sample's own `getDefaultZeroOffsetType` (see its
    // own comment), a depth-comparison sample's `Coordinate` is always a
    // genuine vector, even against `Plain1D`/`Array1D` (a real `deqp-vk`
    // SPIR-V capture confirms a shadow sampler's own coordinate is a
    // `vec3(u, <unused-or-layer>, compare)`, never a bare scalar --
    // `ImageSampleDrefImplicitLodPattern`'s own comment), so this
    // synthesized zero offset must mirror it directly instead: every
    // shape's own switch arm in `SPIRVResourceLowering.cpp`'s dref-sample
    // handling always extracts `OffsetX`/`OffsetY` from it unconditionally
    // before dispatching per-shape (`Plain1D`/`Array1D`'s own arms simply
    // never consume the extracted values), so it can never be a bare
    // scalar here even for `Dim::Dim1D`.
    auto CoordVecTy = mlir::cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        mlir::VectorType::get(CoordVecTy.getShape(), Rewriter.getI32Type());

    // Same fixed Image Operands bit order as `ImageSampleImplicitLodPattern`
    // above (`Bias` before `ConstOffset` before `MinLod`), whichever subset
    // is present.
    mlir::ValueRange OperandArguments = Adaptor.getOperandArguments();
    size_t Index = 0;
    mlir::Value Bias = HasBias ? OperandArguments[Index++] : mlir::Value();
    mlir::Value Offset =
        HasConstOffset ? OperandArguments[Index++] : mlir::Value();
    mlir::Value Clamp = HasMinLod ? OperandArguments[Index++] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    llvm::SmallVector<mlir::Value, 7> Arguments = {Image, Sampler, Coordinate,
                                                   Dref};
    if (Bias)
      Arguments.push_back(Bias);
    Arguments.push_back(Offset);
    if (Clamp)
      Arguments.push_back(Clamp);

    llvm::StringRef IntrinsicName;
    if (Bias)
      IntrinsicName = Clamp ? "llvm.spv.resource.samplecmpbias.clamp"
                            : "llvm.spv.resource.samplecmpbias";
    else
      IntrinsicName = Clamp ? "llvm.spv.resource.samplecmp.clamp"
                            : "llvm.spv.resource.samplecmp";

    Rewriter.replaceOp(Op, createIntrinsicCall(Rewriter, Loc, IntrinsicName,
                                               ResultType, Arguments));
    return mlir::success();
  }
};

/// Converts a `spirv.ImageSampleDrefExplicitLod` with a mandatory `Grad`
/// image operand (optionally combined with `ConstOffset` and `MinLod`)
/// into one of the two `llvm.spv.resource.samplecmpgrad`/
/// `.samplecmpgrad.clamp` intrinsic calls -- the depth-comparison
/// counterpart of `ImageSampleGradPattern` above (roadmap L59), and the
/// `Grad` sibling of `ImageSampleDrefImplicitLodPattern`'s own `Bias`
/// handling (roadmap L52(b)/L66(c)): `Grad` and `Bias` are mutually
/// exclusive on the same instruction, so there is no combined shape to
/// also cover here. `Plain2D` (roadmap L66(c)), `Plain1D`, `Array1D`
/// (roadmap L66(f)), and `Array2D` (roadmap L66(g)) image handles reach a
/// real `feme.cpu.image.samplecmp.{2d,1d,1darray,2darray}.f32` call once
/// lowered (`SPIRVResourceLowering.cpp`'s own `DrefHasGrad` restriction;
/// every other depth-comparison-capable shape remains unstarted follow-on
/// work) -- registered ahead of `ImageSampleDrefExplicitLodPattern` below
/// so this pattern gets first refusal on a `Grad` operand before that
/// pattern's own (mutually exclusive) literal-zero-`Lod` handling is
/// tried.
class ImageSampleDrefGradPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleDrefExplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleDrefExplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleDrefExplicitLodOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::spirv::ImageOperands Actual =
        mlir::spirv::bitEnumClear(Op.getImageOperands(), DiscardedImageOperandBits);

    if (!mlir::spirv::bitEnumContainsAny(Actual,
                                         mlir::spirv::ImageOperands::Grad))
      return Rewriter.notifyMatchFailure(Op, "no Grad image operand");

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Grad |
        mlir::spirv::ImageOperands::ConstOffset |
        mlir::spirv::ImageOperands::MinLod;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);
    bool HasMinLod = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::MinLod);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Dref = Adaptor.getDref();

    mlir::Value Coordinate = Adaptor.getCoordinate();
    // Unlike an ordinary sample's own `getDefaultZeroOffsetType` (see its
    // own comment), a depth-comparison sample's `Coordinate` is always a
    // genuine vector, even against `Plain1D`/`Array1D` (a real `deqp-vk`
    // SPIR-V capture confirms a shadow sampler's own coordinate is a
    // `vec3(u, <unused-or-layer>, compare)`, never a bare scalar --
    // `ImageSampleDrefImplicitLodPattern`'s own comment), so this
    // synthesized zero offset must mirror it directly instead: every
    // shape's own switch arm in `SPIRVResourceLowering.cpp`'s dref-sample
    // handling always extracts `OffsetX`/`OffsetY` from it unconditionally
    // before dispatching per-shape (`Plain1D`/`Array1D`'s own arms simply
    // never consume the extracted values), so it can never be a bare
    // scalar here even for `Dim::Dim1D`.
    auto CoordVecTy = mlir::cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        mlir::VectorType::get(CoordVecTy.getShape(), Rewriter.getI32Type());

    // Same fixed order as `ImageSampleGradPattern` above (`Grad`'s own
    // pair always comes first, then `ConstOffset`, then `MinLod`), but
    // with `Dref` inserted right after the coordinate, matching
    // `ImageSampleDrefImplicitLodPattern`'s own `Dref` placement.
    mlir::ValueRange OperandArguments = Adaptor.getOperandArguments();
    size_t Index = 0;
    mlir::Value DPdx = OperandArguments[Index++];
    mlir::Value DPdy = OperandArguments[Index++];
    mlir::Value Offset =
        HasConstOffset ? OperandArguments[Index++] : mlir::Value();
    mlir::Value Clamp = HasMinLod ? OperandArguments[Index++] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    llvm::SmallVector<mlir::Value, 8> Arguments = {
        Image, Sampler, Coordinate, Dref, DPdx, DPdy, Offset};
    if (Clamp)
      Arguments.push_back(Clamp);
    llvm::StringRef IntrinsicName =
        Clamp ? "llvm.spv.resource.samplecmpgrad.clamp"
              : "llvm.spv.resource.samplecmpgrad";

    Rewriter.replaceOp(Op, createIntrinsicCall(Rewriter, Loc, IntrinsicName,
                                               ResultType, Arguments));
    return mlir::success();
  }
};

/// Converts a `spirv.ImageSampleDrefExplicitLod` (optionally combined with
/// `ConstOffset`; a `Grad` operand has no supported mapping here and is
/// rejected, see `ImageSampleDrefGradPattern` instead) into either
/// `llvm.spv.resource.samplecmplevelzero` (when the pre-conversion `Lod`
/// operand is a literal-zero `spirv.ConstantOp`) or
/// `llvm.spv.resource.samplecmplevel` (roadmap L72(b): any other `Lod`
/// value, real or computed) -- a real `deqp-vk` SPIR-V capture of GLSL's
/// own `textureLodOffset(sampler2DShadow, ...)` (`dEQP-VK.glsl.
/// texture_functions.texturelodoffset.repeat.sampler2dshadow_compute`)
/// confirms a shadow sampler's own explicit Lod is not always a literal
/// zero -- it can be an arbitrary runtime-computed value (a varying
/// interpolated per-invocation across the CTS test's own coordinate
/// sweep), which `samplecmplevelzero` has no way to represent (it has no
/// Lod operand at all -- it always implicitly samples mip level zero).
/// `samplecmplevelzero` remains the preferred, narrower mapping for a
/// literal-zero `Lod` (the exact shape HLSL's own `Texture*::
/// SampleCmpLevelZero` always compiles down to, roadmap L25; see
/// `llvm/test/CodeGen/SPIRV/hlsl-resources/SampleCmpLevelZero.ll`'s own
/// operand order, image, sampler, coord, dref, offset), since it is
/// already a fully exercised, narrower-surface-area path; `
/// samplecmplevel` (mirroring `ImageSampleExplicitLodPattern`'s own
/// `samplelevel`, which likewise threads an arbitrary explicit LOD
/// through for an ordinary, non-comparison sample) covers every other
/// `Lod` value instead.
class ImageSampleDrefExplicitLodPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleDrefExplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleDrefExplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleDrefExplicitLodOp Op,
                  OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::spirv::ImageOperands Actual =
        mlir::spirv::bitEnumClear(Op.getImageOperands(), DiscardedImageOperandBits);
    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::Lod |
        mlir::spirv::ImageOperands::ConstOffset;
    if (!mlir::spirv::bitEnumContainsAny(Actual,
                                        mlir::spirv::ImageOperands::Lod) ||
        !mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(
          Op, "only a literal-zero Lod, optionally with ConstOffset, is "
              "supported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);

    // Image Operands are laid out in the SPIR-V spec's own fixed bit order
    // (`Lod` before `ConstOffset`); the pre-conversion operand (not
    // `Adaptor`'s, which may already have been converted to an
    // `llvm.mlir.constant`) is checked for a literal zero, mirroring
    // `getConstantMemberIndex`'s own pre-conversion constant check above.
    // Roadmap L72(b): a literal-zero `Lod` still prefers the narrower
    // `samplecmplevelzero` mapping below; any other `Lod` (non-constant,
    // or a constant that isn't exactly zero) instead falls through to
    // `samplecmplevel`, threading the real (post-conversion) `Lod` value
    // through instead of rejecting the match outright.
    mlir::Value PreConversionLod = Op.getOperandArguments()[0];
    bool IsLiteralZeroLod = false;
    if (auto LodConstant =
            PreConversionLod.getDefiningOp<mlir::spirv::ConstantOp>()) {
      auto LodFloat = mlir::dyn_cast<mlir::FloatAttr>(LodConstant.getValue());
      IsLiteralZeroLod = LodFloat && LodFloat.getValue().isZero();
    }

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Dref = Adaptor.getDref();

    mlir::Value Coordinate = Adaptor.getCoordinate();
    // Unlike an ordinary sample's own `getDefaultZeroOffsetType` (see its
    // own comment), a depth-comparison sample's `Coordinate` is always a
    // genuine vector, even against `Plain1D`/`Array1D` (a real `deqp-vk`
    // SPIR-V capture confirms a shadow sampler's own coordinate is a
    // `vec3(u, <unused-or-layer>, compare)`, never a bare scalar --
    // `ImageSampleDrefImplicitLodPattern`'s own comment), so this
    // synthesized zero offset must mirror it directly instead: every
    // shape's own switch arm in `SPIRVResourceLowering.cpp`'s dref-sample
    // handling always extracts `OffsetX`/`OffsetY` from it unconditionally
    // before dispatching per-shape (`Plain1D`/`Array1D`'s own arms simply
    // never consume the extracted values), so it can never be a bare
    // scalar here even for `Dim::Dim1D`.
    auto CoordVecTy = mlir::cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        mlir::VectorType::get(CoordVecTy.getShape(), Rewriter.getI32Type());
    mlir::Value Offset =
        HasConstOffset ? Adaptor.getOperandArguments()[1] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    if (IsLiteralZeroLod) {
      Rewriter.replaceOp(
          Op, createIntrinsicCall(
                  Rewriter, Loc, "llvm.spv.resource.samplecmplevelzero",
                  ResultType, {Image, Sampler, Coordinate, Dref, Offset}));
      return mlir::success();
    }

    // Roadmap L72(b): the real (post-conversion) `Lod`, threaded through
    // in the same operand slot `samplecmpbias` gives its own `Bias`
    // (mutually exclusive with it -- SPIR-V forbids combining `Bias` and
    // `Lod` on the same instruction).
    mlir::Value Lod = Adaptor.getOperandArguments()[0];
    Rewriter.replaceOp(
        Op, createIntrinsicCall(
                Rewriter, Loc, "llvm.spv.resource.samplecmplevel", ResultType,
                {Image, Sampler, Coordinate, Dref, Lod, Offset}));
    return mlir::success();
  }
};

/// Converts a `spirv.ImageGather` with `None` or `ConstOffset` image
/// operands into an `llvm.spv.resource.gather` intrinsic call (roadmap
/// L7g, split out of L7's original filing text; confirmed via a real
/// `Texture2D::Gather()`/`GatherRed`/`GatherGreen`/`GatherBlue`/`GatherAlpha`
/// repro compiled with `dxc -fspv-target-env=vulkan1.3` --
/// `offload-test-suite`'s own `Vk.SampledTexture2D.Gather.test.yaml`; the
/// gap this row's own filing text originally cited was a full opcode-level
/// deserialization gap, not just a missing legalization pattern -- see
/// `spirv.ImageGather`'s own new op definition, added by this same row, in
/// `SPIRVImageOps.td`, mirroring the already-existing `spirv.ImageDrefGather`
/// op's own shape exactly, but with a `Component` operand (a 32-bit integer
/// selecting which of the four gathered texels' components to return) in
/// place of `ImageDrefGather`'s own `Dref` (a 32-bit float depth-comparison
/// reference value) -- the two ops are otherwise structurally identical,
/// including their availability, verifier, and assembly format). Note
/// `int_spv_resource_gather` is *not* a new intrinsic this row adds either
/// -- it already exists upstream in `IntrinsicsSPIRV.td`, already fully
/// wired up in `SPIRVInstructionSelector.cpp`'s own `selectGatherIntrinsic`
/// (matching the exact `(image, sampler, coordinate, component, offset)`
/// operand shape this pattern emits below, mirroring
/// `ImageDrefGatherPattern`'s own `(image, sampler, coordinate, dref,
/// offset)` shape one-for-one), simply never reached from MLIR's own
/// SPIR-V dialect before this row added both the missing op definition and
/// this one MLIR-side conversion pattern needed to reach it. See
/// `ImageDrefGatherPattern`'s own doc comment immediately below for the
/// shared reasoning behind why only `ConstOffset` (no `Bias`/`Lod`/`Grad`/
/// `MinLod`) needs to be recognized, and why `Coordinate` needs no
/// `DrefCoordWidth`-style padding logic -- both apply identically here,
/// confirmed via the same real `dxc`-compiled repro cited above.
class ImageGatherPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageGatherOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageGatherOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageGatherOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::ConstOffset;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Component = Adaptor.getComponent();
    mlir::Value Coordinate = Adaptor.getCoordinate();

    auto CoordVecTy = mlir::cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        mlir::VectorType::get(CoordVecTy.getShape(), Rewriter.getI32Type());
    mlir::Value Offset =
        HasConstOffset ? Adaptor.getOperandArguments()[0] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.gather",
                                ResultType, {Image, Sampler, Coordinate,
                                             Component, Offset}));
    return mlir::success();
  }
};

/// Converts a `spirv.ImageDrefGather` with `None` or `ConstOffset` image
/// operands into an `llvm.spv.resource.gather.cmp` intrinsic call (roadmap
/// L7d, split out of L7's original filing text; confirmed via a real
/// `vk::SampledTexture2D`+`GatherCmp` repro compiled with `dxc
/// -fspv-target-env=vulkan1.3` -- `offload-test-suite`'s own
/// `Vk.SampledTexture2D.GatherCmp.test.yaml`). Note
/// `int_spv_resource_gather_cmp` is *not* a new intrinsic this row adds --
/// it already exists upstream in `IntrinsicsSPIRV.td`, already fully wired
/// up in `SPIRVInstructionSelector.cpp`'s own `selectGatherIntrinsic`
/// (matching the exact `(image, sampler, coordinate, dref, offset)`
/// operand shape this pattern emits below, `OffsetReg` always required,
/// even when zero -- `selectGatherIntrinsic` has no "offset operand
/// omitted" case the way some other selectors do, hence this pattern
/// always synthesizes a zero offset itself rather than omitting it),
/// simply never reached from MLIR's own SPIR-V dialect before this row
/// added the one MLIR-side conversion pattern needed to reach it. Unlike
/// `ImageSampleDrefImplicitLodPattern`'s own family, `spirv.ImageDrefGather`
/// has no `Bias`/`Lod`/`Grad`/`MinLod` image operand of its own at all --
/// per the SPIR-V spec, a gather instruction (`OpImage{,Dref}Gather`)
/// always operates at mip level 0, with no way to request otherwise -- so
/// `ConstOffset` is the only image operand this pattern needs to
/// recognize. Also unlike `ImageSampleDrefImplicitLodPattern`'s own
/// `Coordinate`, `spirv.ImageDrefGather`'s own `Coordinate` is never
/// padded with a redundant extra component the way a depth-comparison
/// *sample*'s is (confirmed via a real SPIR-V capture of the repro above:
/// `dxc` emits a plain, unpadded 2-wide `vector<2xf32>` `Coordinate` for a
/// `Plain2D` `GatherCmp`, identical to an ordinary sample's own
/// `Coordinate` width) -- matching the SPIR-V spec's own description of
/// this op's `Coordinate` ("contains (u[, v] ... [, array layer]) as
/// needed", the same wording `spirv.ImageSampleImplicitLod`'s own
/// ordinary, non-dref `Coordinate` uses, not `ImageSampleDrefImplicitLod`'s
/// own "may be a vector larger than needed" caveat), so no
/// `DrefCoordWidth`-style padding logic is needed here at all.
class ImageDrefGatherPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageDrefGatherOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageDrefGatherOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageDrefGatherOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    std::optional<mlir::spirv::ImageOperands> ImageOperandsAttr =
        Op.getImageOperands();
    mlir::spirv::ImageOperands Actual = mlir::spirv::ImageOperands::None;
    if (ImageOperandsAttr)
      Actual = mlir::spirv::bitEnumClear(*ImageOperandsAttr, DiscardedImageOperandBits);

    mlir::spirv::ImageOperands SupportedMask =
        mlir::spirv::ImageOperands::ConstOffset;
    if (!mlir::spirv::bitEnumContainsAll(SupportedMask, Actual))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    bool HasConstOffset = mlir::spirv::bitEnumContainsAny(
        Actual, mlir::spirv::ImageOperands::ConstOffset);

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Dref = Adaptor.getDref();
    mlir::Value Coordinate = Adaptor.getCoordinate();

    auto CoordVecTy = mlir::cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        mlir::VectorType::get(CoordVecTy.getShape(), Rewriter.getI32Type());
    mlir::Value Offset =
        HasConstOffset ? Adaptor.getOperandArguments()[0] : mlir::Value();
    if (!Offset)
      Offset = mlir::LLVM::ConstantOp::create(Rewriter, Loc, OffsetType,
                                              Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.gather.cmp",
                                ResultType, {Image, Sampler, Coordinate, Dref,
                                             Offset}));
    return mlir::success();
  }
};

/// Converts `spirv.ImageQueryLod` into two `llvm.spv.resource.calculate.
/// lod`/`.calculate.lod.unclamped` intrinsic calls (LLVM's SPIRV backend's
/// own `OpImageQueryLod` selection runs the reverse direction, building one
/// two-component result *from* these same two intrinsics -- see
/// `llvm/test/CodeGen/SPIRV/hlsl-resources/CalculateLevelOfDetail.ll`),
/// combined into one `vector<2xf32>` result via two `llvm.insertelement`s:
/// lane 0 (per the op's own result convention -- SPIR-V's spec: "The first
/// component ... contains the mipmap array layer[; t]he second component
/// ... contains the implicit level of detail" -- this converter's single
/// supported image shape is always non-arrayed, so lane 0 is always the
/// clamped LOD) from `calculate.lod`, lane 1 (the unclamped LOD) from
/// `calculate.lod.unclamped`. This is the instruction HLSL's
/// `Texture*::CalculateLevelOfDetail`/`CalculateLevelOfDetailUnclamped`
/// both compile down to (roadmap L25); both lanes are always computed
/// regardless of which one a given HLSL call actually reads, since SPIR-V's
/// `OpImageQueryLod` itself has no way to request only one.
class ImageQueryLodPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageQueryLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageQueryLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageQueryLodOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    auto ResultVecTy = mlir::dyn_cast_or_null<mlir::VectorType>(ResultType);
    if (!ResultVecTy)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    mlir::Type ElementType = ResultVecTy.getElementType();

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Coordinate = Adaptor.getCoordinate();

    mlir::Value Clamped = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.resource.calculate.lod", ElementType,
        {Image, Sampler, Coordinate});
    mlir::Value Unclamped = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.resource.calculate.lod.unclamped",
        ElementType, {Image, Sampler, Coordinate});

    mlir::Value Zero = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI64Type(), Rewriter.getI64IntegerAttr(0));
    mlir::Value One = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI64Type(), Rewriter.getI64IntegerAttr(1));
    mlir::Value Result =
        mlir::LLVM::PoisonOp::create(Rewriter, Loc, ResultVecTy);
    Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                 Clamped, Zero);
    Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                 Unclamped, One);
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};
/// array is nested.
void flattenConstantElements(mlir::Attribute Value,
                             llvm::SmallVectorImpl<mlir::Attribute> &Out) {
  if (auto Elements = mlir::dyn_cast<mlir::DenseElementsAttr>(Value)) {
    llvm::append_range(Out, Elements.getValues<mlir::Attribute>());
    return;
  }
  if (auto Array = mlir::dyn_cast<mlir::ArrayAttr>(Value)) {
    for (mlir::Attribute Element : Array.getValue())
      flattenConstantElements(Element, Out);
    return;
  }
  Out.push_back(Value);
}

/// Returns the scalar type at the bottom of \p Type's `!llvm.array`/
/// `vector` nesting, e.g. `f32` for `!llvm.array<8 x vector<3xf32>>`.
mlir::Type getFlatElementType(mlir::Type Type) {
  while (auto Array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(Type))
    Type = Array.getElementType();
  if (auto Vector = mlir::dyn_cast<mlir::VectorType>(Type))
    return Vector.getElementType();
  return Type;
}

/// Returns the number of scalar leaves `llvm.mlir.constant`'s `ElementsAttr`
/// encoding requires for \p Type, i.e. the product of every `!llvm.array`/
/// `vector` nesting's element counts (mirroring
/// `LLVM::ConstantOp::verify`'s own element-count computation). Any other
/// type not built purely out of that nesting -- most notably `!llvm.struct`,
/// which `ElementsAttr` cannot represent regardless of whether its members'
/// leaf types are uniform -- contributes exactly 1, so a caller comparing
/// this against its own flattened constituent count can detect (and reject)
/// that shape instead of building an `ElementsAttr` the verifier will never
/// accept.
int64_t getFlatElementCount(mlir::Type Type) {
  if (auto Array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(Type))
    return static_cast<int64_t>(Array.getNumElements()) *
           getFlatElementCount(Array.getElementType());
  if (auto Vector = mlir::dyn_cast<mlir::VectorType>(Type))
    return Vector.getNumElements();
  return 1;
}

/// Appends \p Type's own `!llvm.array`/`vector` nesting sizes to \p Shape,
/// e.g. `{4, 4}` for `!llvm.array<4 x vector<4xf32>>` (a matrix, see the
/// `spirv.MatrixType` conversion) or `{8}` for a plain `!llvm.array<8 x
/// f32>`. `ArrayConstantPattern` needs this (not merely `getFlatElementCount`'s
/// own flat product) because upstream MLIR's own LLVM IR translation of an
/// `llvm.mlir.constant` `DenseElementsAttr`
/// (`convertDenseElementsAttr`, `mlir/lib/Target/LLVMIR/ModuleTranslation.cpp`)
/// reassembles the nested `!llvm.array<... x vector<...>>` constant from the
/// attribute's own tensor *shape*, not from the constant op's declared
/// result type: a fully-flattened, one-dimensional attribute (this
/// function's caller's prior behavior) reassembles into a flat
/// `!llvm.array<N x T>` of scalar leaves instead of the intended nested
/// array-of-vectors shape, silently producing a `llvm.mlir.constant`/
/// `llvm.store` pair whose real LLVM IR types disagree with the global's own
/// declared type -- exactly the roadmap L29 bug (a matrix-typed vertex
/// output's per-row `feme.stage.output.store` calls, built from that
/// mismatched store's operand type, ended up with 16 rows instead of the
/// real 4, since the flattened `[16 x float]` store operand has no vector
/// leaf for `CanonicalizeStagePass`'s `getStageIORowShape` to recognize).
void getFlatElementShape(mlir::Type Type,
                         llvm::SmallVectorImpl<int64_t> &Shape) {
  if (auto Array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(Type)) {
    Shape.push_back(Array.getNumElements());
    getFlatElementShape(Array.getElementType(), Shape);
    return;
  }
  if (auto Vector = mlir::dyn_cast<mlir::VectorType>(Type))
    Shape.push_back(Vector.getNumElements());
}

/// Whether \p Type's `!llvm.array` nesting bottoms out in a `vector<...>`
/// (e.g. `!llvm.array<4 x vector<4xf32>>`, a matrix) rather than a plain
/// scalar (e.g. `!llvm.array<2 x array<3 x f32>>`). `ArrayConstantPattern`
/// needs this to decide which `ShapedType` upstream MLIR's own
/// `convertDenseElementsAttr` (`mlir/lib/Target/LLVMIR/
/// ModuleTranslation.cpp`) expects for \p Type's own innermost dimension:
/// a `mlir::VectorType` shape when it bottoms out in a real vector (so that
/// function's own `isa<VectorType>(type)` branch builds each chunk as a
/// `ConstantDataVector` matching the vector leaf), or a plain
/// `mlir::RankedTensorType` shape otherwise (so its `isa<TensorType>`/
/// `!vectorElementType` branch builds each chunk as a `ConstantDataArray`
/// matching the scalar leaf) -- getting this wrong silently reassembles the
/// wrong LLVM IR constant shape (see `getFlatElementShape`'s own comment).
bool hasVectorLeaf(mlir::Type Type) {
  while (auto Array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(Type))
    Type = Array.getElementType();
  return mlir::isa<mlir::VectorType>(Type);
}

/// Converts SPIR-V `ConstantOp` with `spirv.array` or `spirv.matrix` type --
/// MLIR's own `ConstantScalarAndVectorPattern` only matches a scalar or
/// vector `spirv.Constant` (see its `srcType` check), leaving an array or
/// matrix constant illegal, which is exactly the shape a `const static` HLSL
/// array (e.g. a palette of `float3`s) or a `const static float4x4`
/// compiles down to. `llvm.mlir.constant` has no such restriction: it
/// accepts a `DenseElementsAttr` for a whole `!llvm.array<... x
/// vector<...>>` so long as its element count and scalar element type match
/// (see `LLVM::ConstantOp::verify`'s `ElementsAttr` case), whatever the
/// array's rank or whether its leaves are vectors or scalars -- and a
/// matrix converts to exactly that same shape, an `!llvm.array` of column
/// vectors (see the `spirv.MatrixType` conversion in
/// populateSPIRVToLLVMTargetTypeConversions), so it needs no separate
/// handling here beyond accepting its type up front. This pattern flattens
/// the SPIR-V constant's (possibly nested) constituents into a single list,
/// but (roadmap L29) must still shape the resulting `DenseElementsAttr`'s
/// own tensor to mirror `DstType`'s real array/vector nesting (via
/// `getFlatElementShape`), rather than a single flat dimension: upstream
/// MLIR's own LLVM IR translation of this constant
/// (`convertDenseElementsAttr`, `mlir/lib/Target/LLVMIR/
/// ModuleTranslation.cpp`) reassembles the nested LLVM constant from the
/// attribute's own tensor shape, not from `DstType` itself, so a flat
/// one-dimensional attribute silently produces a flat `!llvm.array<N x T>`
/// LLVM IR constant instead of the intended nested one.
class ArrayConstantPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ConstantOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ConstantOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ConstantOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!mlir::isa<mlir::spirv::ArrayType, mlir::spirv::MatrixType>(
            Op.getType()))
      return Rewriter.notifyMatchFailure(Op, "not an array/matrix constant");

    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    llvm::SmallVector<mlir::Attribute, 16> Elements;
    flattenConstantElements(Op.getValue(), Elements);

    // `llvm.mlir.constant`'s `ElementsAttr` encoding can only represent a
    // pure `!llvm.array`/`vector` nesting (see `LLVM::ConstantOp::verify`'s
    // own element-count computation): a struct constituent anywhere in
    // `DstType` (e.g. the array-of-struct-of-array shape an HLSL struct
    // array compiles down to) makes this flattening unrepresentable, so
    // reject that case up front instead of building an `ElementsAttr` the
    // verifier will never accept for it.
    if (getFlatElementCount(DstType) != static_cast<int64_t>(Elements.size()))
      return Rewriter.notifyMatchFailure(
          Op, "array constant is not a pure array/vector nesting");

    // Integer leaves need the same signed/unsigned -> signless retyping
    // `ConstantScalarAndVectorPattern` gives a top-level scalar/vector
    // constant; float leaves need none, since SPIR-V and LLVM float types
    // already coincide.
    mlir::Type LeafType = getFlatElementType(DstType);
    if (mlir::isa<mlir::IntegerType>(LeafType)) {
      for (mlir::Attribute &Element : Elements)
        Element = mlir::IntegerAttr::get(
            LeafType, mlir::cast<mlir::IntegerAttr>(Element).getValue());
    }

    // Build the multi-dimensional shape matching `DstType`'s own
    // `!llvm.array`/`vector` nesting (see `getFlatElementShape`'s own
    // comment above), not merely a flat one-dimensional element count: the
    // real LLVM IR translation of this constant reassembles its nesting
    // from this attribute's own shape, not from `DstType` itself. That
    // translation (`convertDenseElementsAttr`) also distinguishes a
    // `mlir::VectorType`-shaped attribute (each chunk built as a
    // `ConstantDataVector`) from a `mlir::RankedTensorType`-shaped one
    // (each chunk built as a `ConstantDataArray`) when reassembling the
    // innermost dimension, so a matrix/array-of-vectors' shape (see
    // `hasVectorLeaf`) must use the former, or the innermost chunk it
    // builds mismatches the vector leaf's own real LLVM type.
    llvm::SmallVector<int64_t, 4> Shape;
    getFlatElementShape(DstType, Shape);
    mlir::ShapedType ShapeType =
        hasVectorLeaf(DstType)
            ? mlir::cast<mlir::ShapedType>(mlir::VectorType::get(Shape, LeafType))
            : mlir::cast<mlir::ShapedType>(
                  mlir::RankedTensorType::get(Shape, LeafType));
    auto FlatAttr = mlir::DenseElementsAttr::get(ShapeType, Elements);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ConstantOp>(Op, DstType, FlatAttr);
    return mlir::success();
  }
};

/// Converts a function-local `spirv.Variable` whose initializer is an
/// aggregate (array, matrix, or struct) rather than a scalar or vector --
/// e.g. a GLSL local `const vec4 positions[4] = vec4[](...)` array, the
/// real shape `dEQP-VK.mesh_shader.ext.smoke.*.fullscreen_gradient`'s own
/// mesh shader declares to hold its per-vertex position/color lookup
/// tables (roadmap H111). Upstream MLIR's own `VariablePattern`
/// (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) explicitly restricts
/// initialization support to `pointeeType.isIntOrFloat()` or a plain
/// `VectorType`, rejecting anything else outright ("Initialization is
/// supported for scalars and vectors only") -- even though the
/// `alloca`-then-`store` lowering it already performs for those two cases
/// is entirely type-agnostic and works identically for any type
/// `ArrayConstantPattern`/`CompositeConstructPattern`/etc. can already
/// build a real `!llvm.array`/`struct` constant or value for. Registered
/// at `FeMeBenefit` so it wins over upstream's own pattern for exactly
/// this shape; falls back to `failure()` (letting upstream's own pattern
/// handle it, unchanged) for every scalar/vector/uninitialized case this
/// pattern does not need to touch.
class AggregateInitializedVariablePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::VariableOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::VariableOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::VariableOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Value Init = Op.getInitializer();
    if (!Init)
      return Rewriter.notifyMatchFailure(Op, "no initializer");
    auto PointeeType =
        mlir::cast<mlir::spirv::PointerType>(Op.getType()).getPointeeType();
    if (PointeeType.isIntOrFloat() || mlir::isa<mlir::VectorType>(PointeeType))
      return Rewriter.notifyMatchFailure(
          Op, "scalar/vector initializer already handled upstream");

    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    mlir::Type ElementType = getTypeConverter()->convertType(PointeeType);
    if (!DstType || !ElementType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Size =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI32Type(), 1);
    mlir::Value Allocated =
        mlir::LLVM::AllocaOp::create(Rewriter, Loc, DstType, ElementType, Size);
    mlir::LLVM::StoreOp::create(Rewriter, Loc, Adaptor.getInitializer(),
                                Allocated);
    Rewriter.replaceOp(Op, Allocated);
    return mlir::success();
  }
};

/// Converts `spirv.CompositeConstruct` building a 1-D vector out of scalar
/// and/or shorter-vector constituents (e.g. HLSL's `float3(x, x, x)`, which
/// SPIR-V spells as a `CompositeConstruct` of three scalar constituents, or
/// a `.xxx` splat's `CompositeConstruct` of the same scalar three times),
/// building a struct value out of one constituent per member (e.g.
/// assembling a whole HLSL struct value before storing it in one shot), or
/// building a matrix value out of one column-vector constituent per column
/// (e.g. GLSL's `mat4(c0, c1, c2, c3)`). MLIR has no pattern for this op at
/// all, for any of the composite kinds (vector, array, struct, matrix) it
/// can build; the vector, struct, and matrix cases are implemented here.
/// The vector case lowers to an `llvm.mlir.poison` seed with one
/// `llvm.insertelement` per resulting lane -- each lane's value either the
/// scalar constituent supplying it directly, or one `llvm.extractelement`
/// out of the vector constituent supplying a contiguous run of lanes, per
/// this op's "contiguous subset of scalars" semantics. The struct case
/// lowers to an `llvm.mlir.poison` seed with one `llvm.insertvalue` per
/// member, in order (SPIR-V's own `OpCompositeConstruct` for a struct
/// result requires exactly one constituent per member, each already of
/// that member's own type -- unlike the vector case's "contiguous subset"
/// flexibility); a constituent whose type doesn't exactly match its
/// member's converted LLVM type is reassembled lane-by-lane into that
/// member's type first (an `llvm.extractelement` per lane followed by an
/// `llvm.insertvalue` into a poison array at the same position --
/// `llvm.bitcast` itself cannot do this directly, since its own verifier
/// requires a non-aggregate result), needed whenever that member is a
/// vector the struct's own conversion (see
/// convertOffsetStructTypeIgnoringDecorations's "tight-vector retry")
/// substituted a same-bit-width tightly-packed array for. The matrix case
/// mirrors `MatrixCompositeExtractPattern`/`MatrixCompositeInsertPattern`'s
/// own understanding of a matrix's converted representation (an
/// `!llvm.array` of column vectors, see the `spirv.MatrixType` conversion
/// in populateSPIRVToLLVMTargetTypeConversions): `OpCompositeConstruct` for
/// a matrix result requires exactly one whole-column constituent per
/// column, so it lowers to an `llvm.mlir.poison` seed with one
/// `llvm.insertvalue` per column, each constituent inserted as-is (already
/// a converted column vector, with no "contiguous subset"/tight-vector
/// reassembly the vector/struct cases above need). The array case (e.g. a
/// tessellation-control-shader patch function assembling its own
/// `HSInput[3]`-shaped output from three per-control-point struct values)
/// mirrors the matrix case exactly -- `OpCompositeConstruct` for an array
/// result requires exactly one whole-element constituent per array
/// element, already of the array's own element type, so it lowers the same
/// way: one `llvm.insertvalue` per element, each constituent inserted
/// as-is.
class CompositeConstructPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::CompositeConstructOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::CompositeConstructOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::CompositeConstructOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (auto StructTy = mlir::dyn_cast<mlir::spirv::StructType>(Op.getType()))
      return convertStruct(Op, Adaptor, Rewriter, StructTy);

    if (auto MatrixTy = mlir::dyn_cast<mlir::spirv::MatrixType>(Op.getType()))
      return convertMatrix(Op, Adaptor, Rewriter, MatrixTy);

    if (auto ArrayTy = mlir::dyn_cast<mlir::spirv::ArrayType>(Op.getType()))
      return convertArray(Op, Adaptor, Rewriter, ArrayTy);

    auto ResultType = mlir::dyn_cast<mlir::VectorType>(Op.getType());
    if (!ResultType || ResultType.getRank() != 1)
      return Rewriter.notifyMatchFailure(
          Op, "not a 1-D vector composite construct");

    mlir::Type DstType = getTypeConverter()->convertType(ResultType);
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    // Validate before emitting anything: every constituent is either a
    // scalar (one lane) or a 1-D vector (a contiguous run of lanes), and
    // together they add up to exactly the result's lane count.
    int64_t TotalLanes = 0;
    for (mlir::Value Constituent : Adaptor.getConstituents()) {
      if (auto VecTy =
              mlir::dyn_cast<mlir::VectorType>(Constituent.getType())) {
        if (VecTy.getRank() != 1)
          return Rewriter.notifyMatchFailure(Op, "not a 1-D vector operand");
        TotalLanes += VecTy.getNumElements();
        continue;
      }
      ++TotalLanes;
    }
    if (TotalLanes != ResultType.getNumElements())
      return Rewriter.notifyMatchFailure(
          Op, "constituent lane count does not match result vector size");

    mlir::Location Loc = Op.getLoc();
    mlir::Type I32 = Rewriter.getI32Type();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, DstType);
    int64_t Lane = 0;
    for (mlir::Value Constituent : Adaptor.getConstituents()) {
      auto VecTy = mlir::dyn_cast<mlir::VectorType>(Constituent.getType());
      if (!VecTy) {
        mlir::Value DstIndex =
            mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, Lane++);
        Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                     Constituent, DstIndex);
        continue;
      }
      for (int64_t I = 0, E = VecTy.getNumElements(); I != E; ++I, ++Lane) {
        mlir::Value SrcIndex =
            mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, I);
        mlir::Value DstIndex =
            mlir::LLVM::ConstantOp::create(Rewriter, Loc, I32, Lane);
        mlir::Value Component = mlir::LLVM::ExtractElementOp::create(
            Rewriter, Loc, Constituent, SrcIndex);
        Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                     Component, DstIndex);
      }
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }

private:
  mlir::LogicalResult convertStruct(mlir::spirv::CompositeConstructOp Op,
                                    OpAdaptor Adaptor,
                                    mlir::ConversionPatternRewriter &Rewriter,
                                    mlir::spirv::StructType StructTy) const {
    mlir::Type DstType = getTypeConverter()->convertType(StructTy);
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto LLVMStructTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(DstType);
    if (!LLVMStructTy)
      return Rewriter.notifyMatchFailure(Op, "not an LLVM struct result");

    llvm::ArrayRef<mlir::Type> FieldTypes = LLVMStructTy.getBody();
    // (Roadmap H135) FieldTypes may outnumber the declared SPIR-V members:
    // layOutStructIfOffsetsMatch now always materializes every gap (even a
    // purely natural-alignment one) as its own synthetic `[N x i8]`
    // member, since the resulting struct is packed and LLVM no longer
    // inserts that padding implicitly. CompositeConstruct only ever
    // supplies one constituent per *declared* SPIR-V member, so map each
    // declared index to its own physical field index (skipping over any
    // interleaved pad members) instead of assuming a 1:1 correspondence.
    if (Adaptor.getConstituents().size() != StructTy.getNumElements())
      return Rewriter.notifyMatchFailure(
          Op, "constituent count does not match struct member count");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, DstType);
    for (auto [DeclaredIndex, Constituent] :
         llvm::enumerate(Adaptor.getConstituents())) {
      unsigned Index = StructTy.hasOffset()
                           ? getStructMemberPhysicalIndex(
                                 StructTy, DeclaredIndex, *getTypeConverter())
                           : static_cast<unsigned>(DeclaredIndex);
      if (Index >= FieldTypes.size())
        return Rewriter.notifyMatchFailure(Op, "physical index out of range");
      mlir::Type FieldTy = FieldTypes[Index];
      mlir::Value Field = Constituent;
      if (Field.getType() != FieldTy) {
        // Only the "tight-vector retry" substitution described above this
        // pattern's own comment is expected to disagree here: a real
        // vector-typed constituent whose member converted to a
        // same-bit-width tightly-packed array instead -- itself wrapped
        // in `getTightVectorArrayType`'s own marker struct (roadmap
        // H101j), so unwrap that first if present. `llvm.bitcast` itself
        // cannot reinterpret a vector as an array (its own verifier
        // requires a non-aggregate result), so reassemble lane-by-lane
        // instead: extract each vector lane and insert it into a poison
        // array at the same position.
        mlir::Type MarkerInnerTy = getTightVectorMarkerInnerType(FieldTy);
        auto VecTy = mlir::dyn_cast<mlir::VectorType>(Constituent.getType());
        auto ArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(
            MarkerInnerTy ? MarkerInnerTy : FieldTy);
        if (!VecTy || !ArrTy ||
            static_cast<uint64_t>(VecTy.getNumElements()) !=
                ArrTy.getNumElements())
          return Rewriter.notifyMatchFailure(
              Op, "constituent type does not match struct member type");
        mlir::Value Array = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ArrTy);
        for (int64_t Lane = 0, E = VecTy.getNumElements(); Lane != E; ++Lane) {
          mlir::Value LaneIndex = mlir::LLVM::ConstantOp::create(
              Rewriter, Loc, Rewriter.getI32Type(), Lane);
          mlir::Value Element = mlir::LLVM::ExtractElementOp::create(
              Rewriter, Loc, Constituent, LaneIndex);
          Array = mlir::LLVM::InsertValueOp::create(
              Rewriter, Loc, Array, Element, llvm::ArrayRef<int64_t>{Lane});
        }
        if (MarkerInnerTy) {
          mlir::Value Wrapped =
              mlir::LLVM::PoisonOp::create(Rewriter, Loc, FieldTy);
          Array = mlir::LLVM::InsertValueOp::create(
              Rewriter, Loc, Wrapped, Array, llvm::ArrayRef<int64_t>{0});
        }
        Field = Array;
      }
      Result = mlir::LLVM::InsertValueOp::create(
          Rewriter, Loc, Result, Field,
          llvm::ArrayRef<int64_t>{static_cast<int64_t>(Index)});
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }

  /// Builds a matrix's `!llvm.array` of column vectors from one whole
  /// column-vector constituent per column, mirroring how
  /// `MatrixCompositeExtractPattern`/`MatrixCompositeInsertPattern` already
  /// understand that representation (see this class's own file comment).
  mlir::LogicalResult convertMatrix(mlir::spirv::CompositeConstructOp Op,
                                    OpAdaptor Adaptor,
                                    mlir::ConversionPatternRewriter &Rewriter,
                                    mlir::spirv::MatrixType MatrixTy) const {
    mlir::Type DstType = getTypeConverter()->convertType(MatrixTy);
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto ArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(DstType);
    if (!ArrTy)
      return Rewriter.notifyMatchFailure(Op, "not an LLVM array result");

    // `OpCompositeConstruct` for a matrix result requires exactly one
    // constituent per column (SPIR-V spec, `OpCompositeConstruct`'s own
    // "Matrix type" validation rule), unlike the vector case's "contiguous
    // subset of scalars" flexibility.
    if (Adaptor.getConstituents().size() != ArrTy.getNumElements())
      return Rewriter.notifyMatchFailure(
          Op, "constituent count does not match matrix column count");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ArrTy);
    for (auto [Index, Column] : llvm::enumerate(Adaptor.getConstituents()))
      Result = mlir::LLVM::InsertValueOp::create(
          Rewriter, Loc, Result, Column,
          llvm::ArrayRef<int64_t>{static_cast<int64_t>(Index)});
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }

  /// Builds an array's `!llvm.array` from one whole-element constituent per
  /// array element (e.g. a tessellation-control-shader patch function's own
  /// `HSInput[3]` output, assembled from three already-converted `HSInput`
  /// struct values, one per control point) -- see this class's own file
  /// comment.
  mlir::LogicalResult convertArray(mlir::spirv::CompositeConstructOp Op,
                                   OpAdaptor Adaptor,
                                   mlir::ConversionPatternRewriter &Rewriter,
                                   mlir::spirv::ArrayType ArrayTy) const {
    mlir::Type DstType = getTypeConverter()->convertType(ArrayTy);
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto ArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(DstType);
    if (!ArrTy)
      return Rewriter.notifyMatchFailure(Op, "not an LLVM array result");

    // `OpCompositeConstruct` for an array result requires exactly one
    // constituent per element (SPIR-V spec, `OpCompositeConstruct`'s own
    // "Array type" validation rule), each already of the array's own
    // (converted) element type -- unlike the vector case's "contiguous
    // subset of scalars" flexibility.
    if (Adaptor.getConstituents().size() != ArrTy.getNumElements())
      return Rewriter.notifyMatchFailure(
          Op, "constituent count does not match array element count");

    mlir::Type ElementTy = ArrTy.getElementType();
    for (mlir::Value Constituent : Adaptor.getConstituents()) {
      if (Constituent.getType() != ElementTy)
        return Rewriter.notifyMatchFailure(
            Op, "constituent type does not match array element type");
    }

    mlir::Location Loc = Op.getLoc();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ArrTy);
    for (auto [Index, Element] : llvm::enumerate(Adaptor.getConstituents()))
      Result = mlir::LLVM::InsertValueOp::create(
          Rewriter, Loc, Result, Element,
          llvm::ArrayRef<int64_t>{static_cast<int64_t>(Index)});
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts `spirv.CompositeExtract` when its container is a matrix, which
/// converts to LLVM's own natural array-of-column-vectors representation
/// (see the `spirv.MatrixType` conversion in
/// populateSPIRVToLLVMTargetTypeConversions) -- MLIR's own
/// `CompositeExtractPattern` assumes any non-`VectorType` container is a
/// pure `llvm.extractvalue`-shaped aggregate (struct/array nesting all the
/// way down) and asserts once an index remains past the array level, which
/// a matrix's own vector-typed columns are not. One index selects a whole
/// column (an ordinary `llvm.extractvalue`, same as MLIR's own pattern
/// would produce); two select a column then a scalar element within it,
/// needing an `llvm.extractelement` for that second step instead.
class MatrixCompositeExtractPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::CompositeExtractOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::CompositeExtractOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::CompositeExtractOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!mlir::isa<mlir::spirv::MatrixType>(Op.getComposite().getType()))
      return Rewriter.notifyMatchFailure(Op, "not a matrix composite");

    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    llvm::ArrayRef<mlir::Attribute> Indices = Op.getIndices().getValue();
    mlir::Location Loc = Op.getLoc();
    mlir::Value Column = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, Adaptor.getComposite(),
        mlir::cast<mlir::IntegerAttr>(Indices[0]).getInt());
    if (Indices.size() == 1) {
      Rewriter.replaceOp(Op, Column);
      return mlir::success();
    }

    mlir::Value RowIndex = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI32Type(),
        static_cast<int32_t>(
            mlir::cast<mlir::IntegerAttr>(Indices[1]).getInt()));
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractElementOp>(Op, DstType,
                                                              Column, RowIndex);
    return mlir::success();
  }
};

/// The `spirv.CompositeInsert` counterpart of MatrixCompositeExtractPattern
/// above: inserting a whole column is an ordinary `llvm.insertvalue`; a
/// single element needs its column extracted, updated with
/// `llvm.insertelement`, and written back with `llvm.insertvalue`, rather
/// than MLIR's own `CompositeInsertPattern`'s single `llvm.insertvalue`
/// with both indices, which would try to insert a scalar directly into an
/// array position expecting a whole vector.
class MatrixCompositeInsertPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::CompositeInsertOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::CompositeInsertOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::CompositeInsertOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!mlir::isa<mlir::spirv::MatrixType>(Op.getComposite().getType()))
      return Rewriter.notifyMatchFailure(Op, "not a matrix composite");

    llvm::ArrayRef<mlir::Attribute> Indices = Op.getIndices().getValue();
    int64_t ColumnIndex = mlir::cast<mlir::IntegerAttr>(Indices[0]).getInt();
    mlir::Location Loc = Op.getLoc();
    if (Indices.size() == 1) {
      Rewriter.replaceOpWithNewOp<mlir::LLVM::InsertValueOp>(
          Op, Adaptor.getComposite(), Adaptor.getObject(), ColumnIndex);
      return mlir::success();
    }

    mlir::Value Column = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, Adaptor.getComposite(), ColumnIndex);
    mlir::Value RowIndex = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Rewriter.getI32Type(),
        static_cast<int32_t>(
            mlir::cast<mlir::IntegerAttr>(Indices[1]).getInt()));
    mlir::Value UpdatedColumn = mlir::LLVM::InsertElementOp::create(
        Rewriter, Loc, Column, Adaptor.getObject(), RowIndex);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::InsertValueOp>(
        Op, Adaptor.getComposite(), UpdatedColumn, ColumnIndex);
    return mlir::success();
  }
};

/// Broadcasts a scalar to every lane of \p VecTy, the canonical LLVM
/// broadcast idiom: insert the scalar into lane 0 of a poison seed, then a
/// zero-mask `llvm.shufflevector` replicates that one lane across the
/// whole result. Every matrix arithmetic pattern below needs this to scale
/// a column vector by a single extracted scalar component (there is no
/// vector-by-scalar multiply instruction to reach for directly).
static mlir::Value broadcastScalar(mlir::ConversionPatternRewriter &Rewriter,
                                   mlir::Location Loc, mlir::Value Scalar,
                                   mlir::VectorType VecTy) {
  mlir::Value Poison = mlir::LLVM::PoisonOp::create(Rewriter, Loc, VecTy);
  mlir::Value ZeroLane = mlir::LLVM::ConstantOp::create(
      Rewriter, Loc, Rewriter.getI32Type(), 0);
  mlir::Value Inserted = mlir::LLVM::InsertElementOp::create(
      Rewriter, Loc, Poison, Scalar, ZeroLane);
  llvm::SmallVector<int32_t> Mask(VecTy.getNumElements(), 0);
  return mlir::LLVM::ShuffleVectorOp::create(Rewriter, Loc, Inserted, Poison,
                                             Mask);
}

/// Extracts column \p Index (an `llvm.extractvalue` out of the matrix's
/// own `!llvm.array` of column vectors, see the `spirv.MatrixType`
/// conversion in populateSPIRVToLLVMTargetTypeConversions).
static mlir::Value extractColumn(mlir::ConversionPatternRewriter &Rewriter,
                                 mlir::Location Loc, mlir::Value Matrix,
                                 int64_t Index) {
  return mlir::LLVM::ExtractValueOp::create(Rewriter, Loc, Matrix, Index);
}

/// Computes `Matrix * Vector` (an ordinary linear-algebraic matrix-vector
/// product) directly against the converted LLVM values -- shared by
/// `MatrixTimesVectorPattern` below and `MatrixTimesMatrixPattern`, which
/// is itself just this same product applied once per column of its right
/// operand. Column `j` of Matrix is weighted by lane `j` of Vector and the
/// weighted columns are summed: `result = sum_j vector[j] * column[j]`,
/// avoiding any per-row horizontal reduction (unlike
/// `computeVectorTimesMatrix` below, whose per-column dot product needs
/// exactly that).
static mlir::Value computeMatrixTimesVector(
    mlir::ConversionPatternRewriter &Rewriter, mlir::Location Loc,
    mlir::Value Matrix, mlir::Value Vector, int64_t NumColumns,
    mlir::VectorType ColumnTy) {
  mlir::Value Result;
  for (int64_t J = 0; J != NumColumns; ++J) {
    mlir::Value LaneIndex =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI32Type(), J);
    mlir::Value Lane =
        mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Vector, LaneIndex);
    mlir::Value Weight = broadcastScalar(Rewriter, Loc, Lane, ColumnTy);
    mlir::Value Column = extractColumn(Rewriter, Loc, Matrix, J);
    mlir::Value Term =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, ColumnTy, Weight, Column);
    Result = Result ? mlir::LLVM::FAddOp::create(Rewriter, Loc, ColumnTy,
                                                 Result, Term)
                    : Term;
  }
  return Result;
}

/// Computes `Vector * Matrix` (linear-algebraic vector-matrix product):
/// lane `j` of the result is the dot product of Vector with column `j` of
/// Matrix, unlike `computeMatrixTimesVector`'s per-column weighted sum --
/// each column needs its own full horizontal reduction here, since the
/// matrix is on the *right* and each output lane draws from a whole
/// column rather than each input lane weighting a whole column.
static mlir::Value computeVectorTimesMatrix(
    mlir::ConversionPatternRewriter &Rewriter, mlir::Location Loc,
    mlir::Value Vector, mlir::Value Matrix, int64_t NumColumns,
    int64_t NumRows, mlir::VectorType ColumnTy, mlir::Type ResultTy) {
  mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ResultTy);
  for (int64_t J = 0; J != NumColumns; ++J) {
    mlir::Value Column = extractColumn(Rewriter, Loc, Matrix, J);
    mlir::Value Products =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, ColumnTy, Vector, Column);
    mlir::Value Dot;
    for (int64_t I = 0; I != NumRows; ++I) {
      mlir::Value LaneIndex = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI32Type(), I);
      mlir::Value Lane = mlir::LLVM::ExtractElementOp::create(
          Rewriter, Loc, Products, LaneIndex);
      Dot = Dot ? mlir::LLVM::FAddOp::create(
                     Rewriter, Loc, ColumnTy.getElementType(), Dot, Lane)
                : Lane;
    }
    mlir::Value DstIndex =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI32Type(), J);
    Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result, Dot,
                                                 DstIndex);
  }
  return Result;
}

/// Converts `spirv.MatrixTimesVector` (roadmap H10f: an ordinary `mat *
/// vec` transform, one of the most common shapes in any real vertex
/// shader -- found entirely unimplemented by a real Vulkan-CTS run,
/// `dEQP-VK.wsi.xcb.swapchain.render.basic` et al.). MLIR has no pattern
/// for any of this file's five matrix-arithmetic ops, so this whole
/// family (this pattern plus `VectorTimesMatrixPattern`,
/// `MatrixTimesMatrixPattern`, `MatrixTimesScalarPattern`,
/// `TransposePattern` below) is new. See `computeMatrixTimesVector`'s own
/// comment for the actual per-column-weighted-sum algorithm.
class MatrixTimesVectorPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::MatrixTimesVectorOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::MatrixTimesVectorOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::MatrixTimesVectorOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto MatrixTy = mlir::cast<mlir::spirv::MatrixType>(Op.getMatrix().getType());
    mlir::Type ColumnTy = getTypeConverter()->convertType(MatrixTy.getColumnType());
    if (!ColumnTy)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    mlir::Value Result = computeMatrixTimesVector(
        Rewriter, Op.getLoc(), Adaptor.getMatrix(), Adaptor.getVector(),
        MatrixTy.getNumColumns(), mlir::cast<mlir::VectorType>(ColumnTy));
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts `spirv.VectorTimesMatrix` (roadmap H10f). See
/// `computeVectorTimesMatrix`'s own comment for the per-column dot-product
/// algorithm.
class VectorTimesMatrixPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::VectorTimesMatrixOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::VectorTimesMatrixOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::VectorTimesMatrixOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto MatrixTy = mlir::cast<mlir::spirv::MatrixType>(Op.getMatrix().getType());
    mlir::Type ColumnTy = getTypeConverter()->convertType(MatrixTy.getColumnType());
    mlir::Type ResultTy = getTypeConverter()->convertType(Op.getType());
    if (!ColumnTy || !ResultTy)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    mlir::Value Result = computeVectorTimesMatrix(
        Rewriter, Op.getLoc(), Adaptor.getVector(), Adaptor.getMatrix(),
        MatrixTy.getNumColumns(), MatrixTy.getNumRows(),
        mlir::cast<mlir::VectorType>(ColumnTy), ResultTy);
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts `spirv.MatrixTimesMatrix` (roadmap H10f): `LeftMatrix *
/// RightMatrix`, column `j` of the result is `LeftMatrix *
/// RightMatrix.column[j]` -- exactly `computeMatrixTimesVector`'s own
/// matrix-vector product, applied once per column of RightMatrix (the
/// standard "matrix-vector product per output column" definition of
/// matrix-matrix multiplication).
class MatrixTimesMatrixPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::MatrixTimesMatrixOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::MatrixTimesMatrixOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::MatrixTimesMatrixOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto LeftTy =
        mlir::cast<mlir::spirv::MatrixType>(Op.getLeftmatrix().getType());
    auto RightTy =
        mlir::cast<mlir::spirv::MatrixType>(Op.getRightmatrix().getType());
    mlir::Type ColumnTy = getTypeConverter()->convertType(LeftTy.getColumnType());
    mlir::Type ResultTy = getTypeConverter()->convertType(Op.getType());
    if (!ColumnTy || !ResultTy)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto ResultArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(ResultTy);
    if (!ResultArrTy)
      return Rewriter.notifyMatchFailure(Op, "not an LLVM array result");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ResultArrTy);
    auto ColumnVecTy = mlir::cast<mlir::VectorType>(ColumnTy);
    for (int64_t J = 0; J != RightTy.getNumColumns(); ++J) {
      mlir::Value RightColumn =
          extractColumn(Rewriter, Loc, Adaptor.getRightmatrix(), J);
      mlir::Value ResultColumn = computeMatrixTimesVector(
          Rewriter, Loc, Adaptor.getLeftmatrix(), RightColumn,
          LeftTy.getNumColumns(), ColumnVecTy);
      Result = mlir::LLVM::InsertValueOp::create(Rewriter, Loc, Result,
                                                 ResultColumn, J);
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts `spirv.MatrixTimesScalar` (roadmap H10f): scales every column
/// by the same broadcast scalar.
class MatrixTimesScalarPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::MatrixTimesScalarOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::MatrixTimesScalarOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::MatrixTimesScalarOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto MatrixTy = mlir::cast<mlir::spirv::MatrixType>(Op.getMatrix().getType());
    mlir::Type ColumnTy = getTypeConverter()->convertType(MatrixTy.getColumnType());
    mlir::Type ResultTy = getTypeConverter()->convertType(Op.getType());
    if (!ColumnTy || !ResultTy)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto ResultArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(ResultTy);
    if (!ResultArrTy)
      return Rewriter.notifyMatchFailure(Op, "not an LLVM array result");

    mlir::Location Loc = Op.getLoc();
    auto ColumnVecTy = mlir::cast<mlir::VectorType>(ColumnTy);
    mlir::Value Scalar = broadcastScalar(Rewriter, Loc, Adaptor.getScalar(),
                                        ColumnVecTy);
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ResultArrTy);
    for (int64_t J = 0; J != MatrixTy.getNumColumns(); ++J) {
      mlir::Value Column =
          extractColumn(Rewriter, Loc, Adaptor.getMatrix(), J);
      mlir::Value Scaled = mlir::LLVM::FMulOp::create(Rewriter, Loc, ColumnTy,
                                                      Column, Scalar);
      Result =
          mlir::LLVM::InsertValueOp::create(Rewriter, Loc, Result, Scaled, J);
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Transposes \p Matrix -- an `!llvm.array<NumColumns x vector<NumRows x
/// T>>` value, i.e. the "natural" (always column-major) representation
/// MatrixType conversion always builds (see
/// populateSPIRVToLLVMTargetTypeConversions) -- into an
/// `!llvm.array<NumRows x ResultColumnTy>` value (`ResultColumnTy` must
/// itself be a `vector<NumColumns x T>`): new column `r` (one per original
/// row) is built from lane `r` of every one of the `NumColumns` original
/// columns, one `llvm.extractelement`/`llvm.insertelement` pair per
/// (row, column) cell -- there is no bulk "transpose" instruction to reach
/// for, since the source and destination are both arrays of vectors, not
/// a single flat buffer a shuffle could reinterpret. Self-inverse: calling
/// this again on the result with `NumColumns`/`NumRows` swapped (and a
/// `ResultColumnTy` swapped back to the original column type) reproduces
/// \p Matrix. Shared by `TransposePattern` (`spirv.Transpose`) and
/// `RowMajorMatrixStorePattern`/`RowMajorMatrixLoadPattern` below, for whom
/// a physical `RowMajor` storage layout is exactly this same transpose of
/// the logical, always-column-major value MatrixType conversion builds --
/// see the latter two patterns' own comments.
static mlir::Value
transposeMatrixValue(mlir::ConversionPatternRewriter &Rewriter,
                     mlir::Location Loc, mlir::Value Matrix,
                     int64_t NumColumns, int64_t NumRows,
                     mlir::VectorType ResultColumnTy) {
  auto ResultArrTy = mlir::LLVM::LLVMArrayType::get(ResultColumnTy, NumRows);
  mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ResultArrTy);
  for (int64_t R = 0; R != NumRows; ++R) {
    mlir::Value RowIndex =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI32Type(), R);
    mlir::Value NewColumn =
        mlir::LLVM::PoisonOp::create(Rewriter, Loc, ResultColumnTy);
    for (int64_t C = 0; C != NumColumns; ++C) {
      mlir::Value Column = extractColumn(Rewriter, Loc, Matrix, C);
      mlir::Value Elem = mlir::LLVM::ExtractElementOp::create(
          Rewriter, Loc, Column, RowIndex);
      mlir::Value DstIndex = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI32Type(), C);
      NewColumn = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, NewColumn,
                                                      Elem, DstIndex);
    }
    Result =
        mlir::LLVM::InsertValueOp::create(Rewriter, Loc, Result, NewColumn, R);
  }
  return Result;
}

/// Converts `spirv.Transpose` (roadmap H10f) via transposeMatrixValue.
class TransposePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::TransposeOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::TransposeOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::TransposeOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto MatrixTy = mlir::cast<mlir::spirv::MatrixType>(Op.getMatrix().getType());
    mlir::Type ResultTy = getTypeConverter()->convertType(Op.getType());
    if (!ResultTy)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");
    auto ResultArrTy = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(ResultTy);
    if (!ResultArrTy)
      return Rewriter.notifyMatchFailure(Op, "not an LLVM array result");
    auto ResultColumnTy =
        mlir::cast<mlir::VectorType>(ResultArrTy.getElementType());

    mlir::Value Result = transposeMatrixValue(
        Rewriter, Op.getLoc(), Adaptor.getMatrix(), MatrixTy.getNumColumns(),
        MatrixTy.getNumRows(), ResultColumnTy);
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// A whole-matrix `spirv.AccessChain` access this file's own physical
/// layout substitution (getPhysicalMatrixMemberType) can interpret: the
/// matrix's ordinary (logical, always column-major) MatrixType, and the
/// RowMajor/ColMajor- and MatrixStride-decoration facts describing its
/// member's own physical (substituted) layout.
struct MatrixWholeAccess {
  mlir::spirv::MatrixType MatrixTy;
  MatrixMemberLayout Layout;
};

/// Returns \p Op's own matrix type and physical-layout decorations if it
/// is a `spirv.AccessChain` selecting one *whole* matrix that
/// isMatrixMemberLayoutRepresentable rejected (any `RowMajor`- or
/// `ColMajor`-decorated matrix whose `MatrixStride` does not exactly
/// match its natural, tightly packed size -- e.g. real HLSL cbuffer
/// packing's whole-register-per-row/column rule, roadmap H124b), in
/// either of the two shapes FeMe's own upstream HLSL resource
/// representation produces (see BlockElement's own comment): a matrix
/// directly a named struct member (the `cbuffer`/`ConstantBuffer<T>`
/// shape), or a matrix directly the element type of a dxc wrapper's own
/// dynamically-indexed array member (the `RWStructuredBuffer<matCxR>`/
/// `StructuredBuffer<matCxR>` shape, roadmap L83). Returns `std::nullopt`
/// for every other case (not a block access, a natural layout the
/// ordinary conversion already handles correctly, or an access reaching
/// only part of a matrix -- a single row/column or scalar element, which
/// this conversion does not yet cover -- see BlockElement's own comment
/// for the reasoning; the matrix's own physical layout may not even be
/// addressable one part at a time when RowMajor, so no such access is
/// handled here yet).
std::optional<MatrixWholeAccess>
getMatrixWholeAccess(mlir::spirv::AccessChainOp Op) {
  auto MatrixTy = mlir::dyn_cast<mlir::spirv::MatrixType>(
      mlir::cast<mlir::spirv::PointerType>(Op.getComponentPtr().getType())
          .getPointeeType());
  if (!MatrixTy)
    return std::nullopt;

  auto PointerType =
      mlir::dyn_cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());
  if (!PointerType)
    return std::nullopt;
  std::optional<BlockElement> Element = getBufferBlockElement(PointerType);
  if (!Element)
    Element = getUniformBlockElement(PointerType);
  if (!Element)
    return std::nullopt;

  auto Struct =
      mlir::cast<mlir::spirv::StructType>(PointerType.getPointeeType());
  unsigned MemberIndex;
  if (Element->HasWrapper) {
    // The wrapper's own sole member is always index 0; a second index
    // selects the specific array element (the runtime index into the
    // `RWStructuredBuffer`/`StructuredBuffer`) and nothing further,
    // matching this pattern's own "whole matrix" scope -- its own value
    // does not matter here, since every element of the array shares the
    // same layout. The decorations describing that shared layout are
    // attached to the wrapper's own member 0 (the array itself), not to
    // any one of its elements.
    if (Op.getIndices().size() != 2 ||
        !mlir::isa<mlir::spirv::RuntimeArrayType, mlir::spirv::ArrayType>(
            Element->Content))
      return std::nullopt;
    MemberIndex = 0;
  } else {
    // A matrix directly a named struct member (the `cbuffer`/
    // `ConstantBuffer<T>` shape all of H124b's real failures hit) --
    // exactly one index (the member selector) reaches a whole matrix,
    // with no room for a further one that would select only part of it.
    if (Op.getIndices().size() != 1)
      return std::nullopt;
    std::optional<uint64_t> Idx =
        getConstantMemberIndex(Op.getIndices()[0]);
    if (!Idx)
      return std::nullopt;
    MemberIndex = static_cast<unsigned>(*Idx);
  }

  std::optional<MatrixMemberLayout> Layout =
      getMatrixMemberLayout(Struct, MemberIndex);
  if (!Layout)
    return std::nullopt;
  return MatrixWholeAccess{MatrixTy, *Layout};
}

/// Converts a `spirv.Store` of a whole matrix through a member whose
/// declared layout isMatrixMemberLayoutRepresentable rejects -- any
/// `RowMajor`- or `ColMajor`-decorated matrix whose `MatrixStride` is not
/// exactly its natural, tightly packed size (roadmap H124b, generalizing
/// roadmap L83's own original `RowMajor`+exact-stride-only wrapper-array
/// case) -- see getMatrixWholeAccess's own comment for the two shapes
/// matched. The value to store is always modeled with the ordinary
/// "logical" (natural, always column-major) MatrixType -> LLVM
/// conversion throughout the rest of the IR (arithmetic, temporaries,
/// `spirv.CompositeConstruct`/`Extract`, an `!llvm.array<NumColumns x
/// vector<NumRows x T>>`); this pattern transposes (if `RowMajor`) and
/// pads (if `MatrixStride` exceeds one major entry's own natural size)
/// into the "physical" layout getPhysicalMatrixMemberType's own struct
/// type conversion already substituted for this member, only at the
/// exact point the value crosses the memory boundary, immediately before
/// the real `llvm.store`. A physical "major" entry (one row if
/// `RowMajor`, one column if `ColMajor`) is a flat, packed
/// `!llvm.array<MinorCount x T>` of scalars -- deliberately *not* a
/// `vector<MinorCount x T>`, since LLVM's own data layout pads a
/// non-power-of-two-width vector's in-memory (store/alloc) size up to
/// the next power of two on this target (e.g. `vector<3xf32>` occupies
/// 16 bytes, not 12), which would silently corrupt this exact stride
/// whenever `MinorCount` isn't a power of two -- an array's own
/// per-element layout has no such padding, matching
/// getPhysicalMatrixMemberType's own choice. Registered at `FeMeBenefit`,
/// above upstream's own generic `spirv.Store` pattern (registered at the
/// default benefit by populateSPIRVToLLVMConversionPatterns), so it wins
/// only for this one shape; every other store keeps using the upstream
/// pattern unchanged.
class RowMajorMatrixStorePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::StoreOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::StoreOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::StoreOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto AccessChain =
        Op.getPtr().getDefiningOp<mlir::spirv::AccessChainOp>();
    if (!AccessChain)
      return Rewriter.notifyMatchFailure(Op, "not an access chain store");
    std::optional<MatrixWholeAccess> Access = getMatrixWholeAccess(AccessChain);
    if (!Access)
      return Rewriter.notifyMatchFailure(
          Op, "not a physically-substituted whole-matrix store");

    mlir::Location Loc = Op.getLoc();
    mlir::spirv::MatrixType MatrixTy = Access->MatrixTy;
    bool IsRowMajor = Access->Layout.IsRowMajor;
    mlir::Type ElemTy =
        getTypeConverter()->convertType(MatrixTy.getElementType());
    mlir::DataLayout DL;
    uint64_t ElemSize = DL.getTypeSize(ElemTy);
    int64_t MajorCount =
        IsRowMajor ? MatrixTy.getNumRows() : MatrixTy.getNumColumns();
    int64_t MinorCount =
        IsRowMajor ? MatrixTy.getNumColumns() : MatrixTy.getNumRows();
    uint64_t NaturalMinorBytes = static_cast<uint64_t>(MinorCount) * ElemSize;
    bool NeedsPad = Access->Layout.Stride != NaturalMinorBytes;

    auto MinorArrTy = mlir::LLVM::LLVMArrayType::get(ElemTy, MinorCount);
    mlir::Type MajorEntryTy = MinorArrTy;
    if (NeedsPad) {
      auto PadTy = mlir::LLVM::LLVMArrayType::get(
          mlir::IntegerType::get(Rewriter.getContext(), 8),
          Access->Layout.Stride - NaturalMinorBytes);
      MajorEntryTy = mlir::LLVM::LLVMStructType::getLiteral(
          Rewriter.getContext(), {MinorArrTy, PadTy}, /*isPacked=*/true);
    }
    auto PhysicalArrTy = mlir::LLVM::LLVMArrayType::get(MajorEntryTy, MajorCount);

    mlir::Value Physical =
        mlir::LLVM::PoisonOp::create(Rewriter, Loc, PhysicalArrTy);
    for (int64_t Major = 0; Major != MajorCount; ++Major) {
      mlir::Value MinorArr =
          mlir::LLVM::PoisonOp::create(Rewriter, Loc, MinorArrTy);
      for (int64_t Minor = 0; Minor != MinorCount; ++Minor) {
        int64_t Col = IsRowMajor ? Minor : Major;
        int64_t Row = IsRowMajor ? Major : Minor;
        mlir::Value Column =
            extractColumn(Rewriter, Loc, Adaptor.getValue(), Col);
        mlir::Value RowIndex = mlir::LLVM::ConstantOp::create(
            Rewriter, Loc, Rewriter.getI32Type(), Row);
        mlir::Value Elem = mlir::LLVM::ExtractElementOp::create(
            Rewriter, Loc, Column, RowIndex);
        MinorArr = mlir::LLVM::InsertValueOp::create(Rewriter, Loc, MinorArr,
                                                      Elem, Minor);
      }
      mlir::Value Entry = MinorArr;
      if (NeedsPad) {
        mlir::Value PaddedEntry =
            mlir::LLVM::PoisonOp::create(Rewriter, Loc, MajorEntryTy);
        Entry = mlir::LLVM::InsertValueOp::create(
            Rewriter, Loc, PaddedEntry, MinorArr, llvm::ArrayRef<int64_t>{0});
      }
      Physical = mlir::LLVM::InsertValueOp::create(Rewriter, Loc, Physical,
                                                    Entry, Major);
    }
    Rewriter.replaceOpWithNewOp<mlir::LLVM::StoreOp>(Op, Physical,
                                                     Adaptor.getPtr());
    return mlir::success();
  }
};

/// Converts a `spirv.Load` of a whole matrix through a member whose
/// declared layout isMatrixMemberLayoutRepresentable rejects -- the exact
/// inverse of RowMajorMatrixStorePattern above (see its own comment, and
/// getMatrixWholeAccess's, for the shapes matched and why a physical
/// major entry is a flat scalar array rather than a vector): the real
/// `llvm.load` reads the physical bytes as an `!llvm.array<MajorCount x
/// MajorEntryTy>` (getPhysicalMatrixMemberType's own substituted member
/// type), which this pattern then depads (if `MatrixStride` exceeds one
/// major entry's own natural size) and transposes (if `RowMajor`) back
/// into the ordinary logical (natural, column-major) MatrixType
/// representation used everywhere else in the IR. Registered at
/// `FeMeBenefit` for the same reason as RowMajorMatrixStorePattern.
class RowMajorMatrixLoadPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::LoadOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::LoadOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::LoadOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto AccessChain =
        Op.getPtr().getDefiningOp<mlir::spirv::AccessChainOp>();
    if (!AccessChain)
      return Rewriter.notifyMatchFailure(Op, "not an access chain load");
    std::optional<MatrixWholeAccess> Access = getMatrixWholeAccess(AccessChain);
    if (!Access)
      return Rewriter.notifyMatchFailure(
          Op, "not a physically-substituted whole-matrix load");

    mlir::Location Loc = Op.getLoc();
    mlir::spirv::MatrixType MatrixTy = Access->MatrixTy;
    bool IsRowMajor = Access->Layout.IsRowMajor;
    int64_t NumColumns = MatrixTy.getNumColumns();
    mlir::Type ElemTy =
        getTypeConverter()->convertType(MatrixTy.getElementType());
    mlir::DataLayout DL;
    uint64_t ElemSize = DL.getTypeSize(ElemTy);
    int64_t MajorCount =
        IsRowMajor ? MatrixTy.getNumRows() : MatrixTy.getNumColumns();
    int64_t MinorCount =
        IsRowMajor ? MatrixTy.getNumColumns() : MatrixTy.getNumRows();
    uint64_t NaturalMinorBytes = static_cast<uint64_t>(MinorCount) * ElemSize;
    bool NeedsPad = Access->Layout.Stride != NaturalMinorBytes;

    auto MinorArrTy = mlir::LLVM::LLVMArrayType::get(ElemTy, MinorCount);
    mlir::Type MajorEntryTy = MinorArrTy;
    if (NeedsPad) {
      auto PadTy = mlir::LLVM::LLVMArrayType::get(
          mlir::IntegerType::get(Rewriter.getContext(), 8),
          Access->Layout.Stride - NaturalMinorBytes);
      MajorEntryTy = mlir::LLVM::LLVMStructType::getLiteral(
          Rewriter.getContext(), {MinorArrTy, PadTy}, /*isPacked=*/true);
    }
    auto PhysicalArrTy = mlir::LLVM::LLVMArrayType::get(MajorEntryTy, MajorCount);
    mlir::Value Physical = mlir::LLVM::LoadOp::create(Rewriter, Loc,
                                                      PhysicalArrTy,
                                                      Adaptor.getPtr());

    auto LogicalColumnTy = mlir::cast<mlir::VectorType>(
        getTypeConverter()->convertType(MatrixTy.getColumnType()));
    auto LogicalArrTy =
        mlir::LLVM::LLVMArrayType::get(LogicalColumnTy, NumColumns);
    mlir::Value Logical =
        mlir::LLVM::PoisonOp::create(Rewriter, Loc, LogicalArrTy);
    for (int64_t Col = 0; Col != NumColumns; ++Col) {
      mlir::Value NewColumn =
          mlir::LLVM::PoisonOp::create(Rewriter, Loc, LogicalColumnTy);
      for (int64_t Row = 0, NumRows = MatrixTy.getNumRows(); Row != NumRows;
           ++Row) {
        int64_t Major = IsRowMajor ? Row : Col;
        int64_t Minor = IsRowMajor ? Col : Row;
        mlir::Value Entry =
            mlir::LLVM::ExtractValueOp::create(Rewriter, Loc, Physical, Major);
        mlir::Value MinorArr =
            NeedsPad ? mlir::LLVM::ExtractValueOp::create(
                           Rewriter, Loc, Entry,
                           0)
                     : Entry;
        mlir::Value Elem =
            mlir::LLVM::ExtractValueOp::create(Rewriter, Loc, MinorArr, Minor);
        mlir::Value DstIndex = mlir::LLVM::ConstantOp::create(
            Rewriter, Loc, Rewriter.getI32Type(), Row);
        NewColumn = mlir::LLVM::InsertElementOp::create(Rewriter, Loc,
                                                        NewColumn, Elem,
                                                        DstIndex);
      }
      Logical = mlir::LLVM::InsertValueOp::create(Rewriter, Loc, Logical,
                                                  NewColumn, Col);
    }
    Rewriter.replaceOp(Op, Logical);
    return mlir::success();
  }
};

/// A column-select `spirv.AccessChain` -- the member selector plus
/// exactly one further (possibly dynamic) index selecting one whole
/// logical column (`matrix[col]`, a `vector<NumRows x T>` -- see
/// rewriteBlockAccess's own comment for why SPIR-V's matrix indexing is
/// always column-first regardless of physical RowMajor/ColMajor storage)
/// -- into a `RowMajor` matrix member whose declared layout
/// isMatrixMemberLayoutRepresentable rejects (roadmap H129; RowMajor is
/// always non-representable regardless of its own `MatrixStride`, so
/// checking `IsRowMajor` alone suffices here). The ColMajor half of this
/// same shape needs no dedicated pattern at all -- a single, correctly
/// strided GEP is enough, and rewriteBlockAccess's own AccessChain
/// conversion already produces it directly (see its own comment) -- only
/// RowMajor, whose logical column is scattered across `NumRows` separate
/// physical row entries rather than contiguous, needs the genuine gather
/// MatrixColumnLoadPattern/MatrixColumnStorePattern below perform.
///
/// `ColumnIndex` is still the original, unconverted SPIR-V dialect value
/// (this AccessChain's own second operand): the caller recovers its
/// already-converted (LLVM dialect) form via
/// `ConversionPatternRewriter::getRemappedValue`, since a Load/Store
/// pattern's own OpAdaptor exposes only the Load/Store op's own operands
/// (the pointer/value), not this defining AccessChain's own index list.
///
/// Restricted to the direct (`cbuffer`/`ConstantBuffer<T>`) struct-member
/// shape (`Element.HasWrapper == false`) -- the only shape any of this
/// roadmap entry's own 236 real `dEQP-VK.ubo.*` failures exercised;
/// extending to the wrapper (`RWStructuredBuffer<matCxR>`) shape is
/// deferred until a real case needs it.
struct MatrixColumnAccess {
  mlir::spirv::MatrixType MatrixTy;
  MatrixMemberLayout Layout;
  mlir::Value ColumnIndex;
};

/// Returns \p Op's own MatrixColumnAccess facts if it matches that shape,
/// or `std::nullopt` otherwise (not a block access, a wrapper-shape
/// access, not exactly a member-plus-column-index AccessChain, the member
/// isn't a matrix, or it isn't `RowMajor`).
std::optional<MatrixColumnAccess>
getMatrixColumnAccess(mlir::spirv::AccessChainOp Op) {
  auto PointerType =
      mlir::dyn_cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());
  if (!PointerType)
    return std::nullopt;
  std::optional<BlockElement> Element = getBufferBlockElement(PointerType);
  if (!Element)
    Element = getUniformBlockElement(PointerType);
  if (!Element || Element->HasWrapper)
    return std::nullopt;
  auto Struct = mlir::dyn_cast<mlir::spirv::StructType>(Element->Content);
  if (!Struct || Op.getIndices().size() != 2)
    return std::nullopt;
  std::optional<uint64_t> MemberIndex =
      getConstantMemberIndex(Op.getIndices()[0]);
  if (!MemberIndex)
    return std::nullopt;
  auto MatrixTy = mlir::dyn_cast<mlir::spirv::MatrixType>(
      Struct.getElementType(static_cast<unsigned>(*MemberIndex)));
  if (!MatrixTy)
    return std::nullopt;
  std::optional<MatrixMemberLayout> Layout =
      getMatrixMemberLayout(Struct, static_cast<unsigned>(*MemberIndex));
  if (!Layout || !Layout->IsRowMajor)
    return std::nullopt;
  return MatrixColumnAccess{MatrixTy, *Layout, Op.getIndices()[1]};
}

/// Builds the `!llvm.array<NumRows x MajorEntryTy>` physical member type
/// MatrixColumnLoadPattern/MatrixColumnStorePattern GEP into (the same
/// substitution getPhysicalMatrixMemberType's own whole-matrix-access
/// caller uses, recomputed here directly since that helper takes a
/// `spirv.MatrixType`+`mlir::DataLayout` pair this file's own
/// `mlir::DataLayout DL;` default-construction already matches).
static mlir::LLVM::LLVMArrayType
getRowMajorPhysicalMemberType(mlir::spirv::MatrixType MatrixTy,
                              const MatrixMemberLayout &Layout,
                              mlir::Type ElemTy, mlir::DataLayout &DL,
                              bool &NeedsPad) {
  int64_t NumRows = MatrixTy.getNumRows();
  int64_t NumColumns = MatrixTy.getNumColumns();
  uint64_t ElemSize = DL.getTypeSize(ElemTy);
  uint64_t NaturalMinorBytes = static_cast<uint64_t>(NumColumns) * ElemSize;
  NeedsPad = Layout.Stride != NaturalMinorBytes;
  auto MinorArrTy = mlir::LLVM::LLVMArrayType::get(ElemTy, NumColumns);
  mlir::Type MajorEntryTy = MinorArrTy;
  if (NeedsPad) {
    auto PadTy = mlir::LLVM::LLVMArrayType::get(
        mlir::IntegerType::get(MatrixTy.getContext(), 8),
        Layout.Stride - NaturalMinorBytes);
    MajorEntryTy = mlir::LLVM::LLVMStructType::getLiteral(
        MatrixTy.getContext(), {MinorArrTy, PadTy}, /*isPacked=*/true);
  }
  return mlir::LLVM::LLVMArrayType::get(MajorEntryTy, NumRows);
}

/// Converts a `spirv.Load` of one logical column (`matrix[col]`) out of a
/// `RowMajor` matrix member whose declared layout
/// isMatrixMemberLayoutRepresentable rejects (roadmap H129; see
/// MatrixColumnAccess's own comment for the shape matched, and why
/// RowMajor's own column needs a real gather rather than a single
/// address). `Adaptor.getPtr()` here is the matrix member's own base
/// address, unconditionally returned unconverted by rewriteBlockAccess's
/// own RowMajor column-select case -- exactly the same "deferred
/// address" trick the existing whole-matrix case already relies on for
/// RowMajorMatrixLoadPattern -- since this pattern, not the AccessChain's
/// own conversion, is the one that actually knows how to interpret it for
/// this shape: it GEPs+loads one scalar per row, directly out of the same
/// physical `!llvm.array<NumRows x MajorEntryTy>` layout
/// RowMajorMatrixLoadPattern's own whole-matrix load already reads (see
/// its own comment for `MajorEntryTy`'s own shape), and assembles the
/// `NumRows` scalars into the logical `vector<NumRows x T>` result via
/// `llvm.insertelement`. Registered at `FeMeBenefit`, above upstream's
/// own generic `spirv.Load` pattern, for the same reason as
/// RowMajorMatrixLoadPattern.
class MatrixColumnLoadPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::LoadOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::LoadOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::LoadOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto AccessChain =
        Op.getPtr().getDefiningOp<mlir::spirv::AccessChainOp>();
    if (!AccessChain)
      return Rewriter.notifyMatchFailure(Op, "not an access chain load");
    std::optional<MatrixColumnAccess> Access =
        getMatrixColumnAccess(AccessChain);
    if (!Access)
      return Rewriter.notifyMatchFailure(
          Op, "not a RowMajor matrix column-select load");
    mlir::Value ColumnIndex = Rewriter.getRemappedValue(Access->ColumnIndex);
    if (!ColumnIndex)
      return Rewriter.notifyMatchFailure(Op,
                                         "column index not yet converted");

    mlir::Location Loc = Op.getLoc();
    mlir::spirv::MatrixType MatrixTy = Access->MatrixTy;
    int64_t NumRows = MatrixTy.getNumRows();
    mlir::Type ElemTy =
        getTypeConverter()->convertType(MatrixTy.getElementType());
    mlir::DataLayout DL;
    bool NeedsPad;
    auto PhysicalArrTy = getRowMajorPhysicalMemberType(
        MatrixTy, Access->Layout, ElemTy, DL, NeedsPad);

    auto ColumnTy = mlir::cast<mlir::VectorType>(
        getTypeConverter()->convertType(MatrixTy.getColumnType()));
    mlir::Value Result = mlir::LLVM::PoisonOp::create(Rewriter, Loc, ColumnTy);
    mlir::Type PtrTy = Adaptor.getPtr().getType();
    for (int64_t Row = 0; Row != NumRows; ++Row) {
      llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
      GEPIndices.push_back(0);
      GEPIndices.push_back(Row);
      if (NeedsPad)
        GEPIndices.push_back(0);
      GEPIndices.push_back(ColumnIndex);
      mlir::Value ElemPtr = mlir::LLVM::GEPOp::create(
          Rewriter, Loc, PtrTy, PhysicalArrTy, Adaptor.getPtr(), GEPIndices,
          mlir::LLVM::GEPNoWrapFlags::inbounds);
      mlir::Value Elem =
          mlir::LLVM::LoadOp::create(Rewriter, Loc, ElemTy, ElemPtr);
      mlir::Value RowIndex = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI32Type(), Row);
      Result = mlir::LLVM::InsertElementOp::create(Rewriter, Loc, Result,
                                                    Elem, RowIndex);
    }
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

/// Converts a `spirv.Store` of one logical column (`matrix[col]`) into a
/// `RowMajor` matrix member whose declared layout
/// isMatrixMemberLayoutRepresentable rejects -- the exact inverse of
/// MatrixColumnLoadPattern above (see its own comment for the shape
/// matched and how the physical member layout is interpreted): extracts
/// each of the `vector<NumRows x T>` value's own `NumRows` lanes via
/// `llvm.extractelement` and GEPs+stores each one directly into the same
/// physical layout. Not currently exercised by any real `dEQP-VK.ubo.*`
/// case (`Uniform`/UBO storage is read-only from shader code; only
/// `StorageBuffer`/SSBO supports `spirv.Store`), but implemented for
/// symmetry with MatrixColumnLoadPattern and to be ready for a future
/// SSBO-side case reusing this exact shape. Registered at `FeMeBenefit`
/// for the same reason as MatrixColumnLoadPattern.
class MatrixColumnStorePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::StoreOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::StoreOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::StoreOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto AccessChain =
        Op.getPtr().getDefiningOp<mlir::spirv::AccessChainOp>();
    if (!AccessChain)
      return Rewriter.notifyMatchFailure(Op, "not an access chain store");
    std::optional<MatrixColumnAccess> Access =
        getMatrixColumnAccess(AccessChain);
    if (!Access)
      return Rewriter.notifyMatchFailure(
          Op, "not a RowMajor matrix column-select store");
    mlir::Value ColumnIndex = Rewriter.getRemappedValue(Access->ColumnIndex);
    if (!ColumnIndex)
      return Rewriter.notifyMatchFailure(Op,
                                         "column index not yet converted");

    mlir::Location Loc = Op.getLoc();
    mlir::spirv::MatrixType MatrixTy = Access->MatrixTy;
    int64_t NumRows = MatrixTy.getNumRows();
    mlir::Type ElemTy =
        getTypeConverter()->convertType(MatrixTy.getElementType());
    mlir::DataLayout DL;
    bool NeedsPad;
    auto PhysicalArrTy = getRowMajorPhysicalMemberType(
        MatrixTy, Access->Layout, ElemTy, DL, NeedsPad);

    mlir::Type PtrTy = Adaptor.getPtr().getType();
    for (int64_t Row = 0; Row != NumRows; ++Row) {
      mlir::Value RowIndex = mlir::LLVM::ConstantOp::create(
          Rewriter, Loc, Rewriter.getI32Type(), Row);
      mlir::Value Elem = mlir::LLVM::ExtractElementOp::create(
          Rewriter, Loc, Adaptor.getValue(), RowIndex);
      llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
      GEPIndices.push_back(0);
      GEPIndices.push_back(Row);
      if (NeedsPad)
        GEPIndices.push_back(0);
      GEPIndices.push_back(ColumnIndex);
      mlir::Value ElemPtr = mlir::LLVM::GEPOp::create(
          Rewriter, Loc, PtrTy, PhysicalArrTy, Adaptor.getPtr(), GEPIndices,
          mlir::LLVM::GEPNoWrapFlags::inbounds);
      mlir::LLVM::StoreOp::create(Rewriter, Loc, Elem, ElemPtr);
    }
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Drops `spirv.ExecutionMode`, whose contents FeMe instead reads before
/// conversion and re-emits as function attributes on the entry point (see
/// feme::spirv::createConvertSPIRVToLLVMPass). MLIR's own pattern turns it
/// into a `__spv__<entry>_execution_mode_info_<mode>` global describing the
/// mode to the SPIR-V *runner*, which LLVM's SPIRV backend has no notion of.
///
/// This also covers `VK_KHR_shader_float_controls`'s (roadmap F3)
/// `DenormPreserve`/`RoundingModeRTE`/`SignedZeroInfNanPreserve` execution
/// modes: they already describe the strict, denormal-preserving,
/// round-to-nearest-even code every FP op conversion pattern produces by
/// default, so, like `LocalSize`, they need only be read (which
/// `collectEntryPoints` does) before being dropped here with everything
/// else. `RoundingModeRTZ` and `DenormFlushToZero` are read the same way,
/// into the widths FloatControlArithmeticPattern below consults to honor
/// them (roadmap F15a/F15b): every `VK_KHR_shader_float_controls` execution
/// mode is now genuinely honored rather than merely diagnosed.
class ExecutionModePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ExecutionModeOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ExecutionModeOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ExecutionModeOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Returns a bool-typed (`i1`) value with the same shape as \p Ty: `i1`
/// itself for a scalar float type, or a vector of `i1` with the same shape
/// for a vector one. This is the result type `llvm.is.fpclass` (see
/// flushSubnormalToZero) needs alongside \p Ty's own arithmetic operands.
mlir::Type getBoolTypeLike(mlir::Type Ty) {
  mlir::Type I1 = mlir::IntegerType::get(Ty.getContext(), 1);
  if (auto Shaped = mlir::dyn_cast<mlir::ShapedType>(Ty))
    return Shaped.clone(I1);
  return I1;
}

/// Returns a same-signed zero of \p V's type if \p V is subnormal, or \p V
/// itself otherwise: the "software flush-to-zero" `DenormFlushToZero`
/// (roadmap F15b) needs in place of an intrinsic swap, since, unlike
/// `RoundingModeRTZ`, LLVM has no constrained-intrinsics equivalent for
/// flush-to-zero, and its `denormal-fp-math`/`denormal-fp-math-f32`
/// function attributes cover only `f32` as a whole function, not this
/// execution mode's own independent per-width, per-operation request.
/// `llvm.is.fpclass`'s bit mask `0x90` is the two subnormal classes (`0x10`
/// negative, `0x80` positive; see LLVM's LangRef, "'llvm.is.fpclass'
/// Intrinsic"); `llvm.copysign` then gives the flushed zero \p V's own sign
/// so a subsequent operation's sign-dependent behavior (e.g. `spirv.FDiv`
/// by it producing a signed infinity) is unaffected by the flush itself.
mlir::Value flushSubnormalToZero(mlir::ConversionPatternRewriter &Rewriter,
                                  mlir::Location Loc, mlir::Value V) {
  constexpr uint32_t SubnormalClassMask = 0x90;
  mlir::Type Ty = V.getType();
  mlir::Value IsSubnormal = mlir::LLVM::IsFPClass::create(
      Rewriter, Loc, getBoolTypeLike(Ty), V, SubnormalClassMask);
  mlir::Value Zero = mlir::LLVM::ConstantOp::create(
      Rewriter, Loc, Ty, Rewriter.getZeroAttr(Ty));
  mlir::Value SignedZero =
      mlir::LLVM::CopySignOp::create(Rewriter, Loc, Ty, Zero, V);
  return mlir::LLVM::SelectOp::create(Rewriter, Loc, Ty, IsSubnormal,
                                       SignedZero, V);
}

/// Creates an `llvm.mlir.constant` of \p Value, scalar or splatted to match
/// \p Ty's shape (mirroring upstream `SPIRVToLLVM.cpp`'s own file-local,
/// inaccessible-from-here `createFPConstant`).
mlir::Value createSameShapeFPConstant(mlir::ConversionPatternRewriter &Rewriter,
                                      mlir::Location Loc, mlir::Type Ty,
                                      double Value) {
  if (auto Shaped = mlir::dyn_cast<mlir::ShapedType>(Ty)) {
    auto FloatTy = mlir::cast<mlir::FloatType>(Shaped.getElementType());
    return mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, Ty,
        mlir::SplatElementsAttr::get(mlir::cast<mlir::ShapedType>(Ty),
                                     Rewriter.getFloatAttr(FloatTy, Value)));
  }
  auto FloatTy = mlir::cast<mlir::FloatType>(Ty);
  return mlir::LLVM::ConstantOp::create(Rewriter, Loc, Ty,
                                        Rewriter.getFloatAttr(FloatTy, Value));
}

/// Converts a GLSL.std.450 "special function" unary op (roadmap H6m) that a
/// real GPU's own special-function hardware unit typically evaluates via a
/// fast approximation, by first flushing a subnormal operand to a
/// same-signed zero (`flushSubnormalToZero`) before the plain LLVM
/// intrinsic \p LLVMOp itself runs -- unlike `FloatControlArithmeticPattern`
/// above (ordinary arithmetic, only flushed when an entry point's own
/// `DenormFlushToZero` execution mode, `VK_KHR_shader_float_controls`,
/// F15b, asks for it), this flush is unconditional: it models a real
/// hardware quirk of the special-function unit itself, not a
/// shader-requested precision relaxation, so it applies regardless of
/// whether the module declares `DenormFlushToZero` at all (`dxc` never
/// emits that execution mode for a plain HLSL `log`/`sqrt`/etc. call, yet a
/// real GPU's own reference output for a subnormal input still behaves as
/// if it had been flushed first -- see `Feature/HLSLLib/{log,log10,log2,
/// sqrt,sinh}.32.test`, whose golden `ExpectedOut` data assumes exactly
/// this). Registered only for the ops whose real math actually differs
/// from the flushed-input answer at `f32` precision for these tests' own
/// denormal inputs (`Log`, `Log2`, `Sqrt`, `Sinh`) -- siblings like `Exp`,
/// `Cos`, `Asin`, etc. already produce the same answer either way (their
/// own math is continuous and non-singular near zero), so they are left on
/// MLIR's own unconditional `DirectConversionPattern` rather than needing
/// this override too.
template <typename SPIRVOp, typename LLVMOp>
class TranscendentalFlushInputPattern
    : public mlir::SPIRVToLLVMConversion<SPIRVOp> {
public:
  using mlir::SPIRVToLLVMConversion<SPIRVOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(SPIRVOp Op, typename SPIRVOp::Adaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = this->getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Operand =
        flushSubnormalToZero(Rewriter, Loc, Adaptor.getOperand());
    Rewriter.replaceOpWithNewOp<LLVMOp>(Op, DstType, Operand);
    return mlir::success();
  }
};

/// Converts `spirv.GL.InverseSqrt` (HLSL `rsqrt`, roadmap H6m) the same way
/// upstream's own `InverseSqrtPattern` does (`1.0 / llvm.sqrt(x)`), except
/// flushing a subnormal operand to a same-signed zero first, for the same
/// real-hardware-special-function-unit reason
/// `TranscendentalFlushInputPattern` above documents: e.g. `rsqrt` of a
/// negative subnormal is otherwise `1.0 / sqrt(negative) = 1.0 / NaN =
/// NaN`, while a real GPU's own reference answer (and
/// `Feature/HLSLLib/rsqrt.32.test`'s own golden data) is `1.0 / sqrt(-0.0)
/// = -inf`, as if the subnormal operand had been flushed to `-0.0` first.
class FlushedInverseSqrtPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLInverseSqrtOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLInverseSqrtOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLInverseSqrtOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type SrcType = Op.getType();
    mlir::Type DstType = getTypeConverter()->convertType(SrcType);
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Operand =
        flushSubnormalToZero(Rewriter, Loc, Adaptor.getOperand());
    mlir::Value One = createSameShapeFPConstant(Rewriter, Loc, DstType, 1.0);
    mlir::Value Sqrt =
        mlir::LLVM::SqrtOp::create(Rewriter, Loc, DstType, Operand);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::FDivOp>(Op, DstType, One, Sqrt);
    return mlir::success();
  }
};

/// Converts `spirv.GL.Radians`/`spirv.GL.Degrees` (roadmap H6m) the same way
/// upstream's own `ScalePattern` does (a plain multiply by a compile-time
/// constant), except flushing a subnormal operand to a same-signed zero
/// first, for the same real-hardware-special-function-unit reason
/// `TranscendentalFlushInputPattern` above documents: unlike an ordinary
/// `spirv.FMul` by a non-unit constant, scaling a subnormal by `pi/180` or
/// `180/pi` keeps the result subnormal-scale (nonzero) under real,
/// denormal-preserving IEEE-754 math, while a real GPU's own reference
/// answer (and `Feature/HLSLLib/{radians,degrees}.32.test`'s own golden
/// data) is exactly a same-signed zero, as if the operand had been flushed
/// first.
template <typename SPIRVOp>
class FlushedScalePattern : public mlir::SPIRVToLLVMConversion<SPIRVOp> {
public:
  template <typename... Args>
  FlushedScalePattern(double Scale, Args &&...ArgPack)
      : mlir::SPIRVToLLVMConversion<SPIRVOp>(std::forward<Args>(ArgPack)...),
        Scale(Scale) {}

  mlir::LogicalResult
  matchAndRewrite(SPIRVOp Op, typename SPIRVOp::Adaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = this->getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Operand =
        flushSubnormalToZero(Rewriter, Loc, Adaptor.getOperand());
    mlir::Value Factor =
        createSameShapeFPConstant(Rewriter, Loc, DstType, Scale);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::FMulOp>(Op, DstType, Operand,
                                                    Factor);
    return mlir::success();
  }

private:
  double Scale;
};

/// Converts `spirv.GL.Atan2` (roadmap L7c) directly to `llvm.intr.atan2`, a
/// component-wise equivalent needing no arithmetic decomposition -- upstream
/// `SPIRVToLLVM.cpp`'s own `DirectConversionPattern` template isn't usable
/// from here (it's file-local to that translation unit), so this is a thin,
/// one-off restatement of that same shape, mirroring upstream's own
/// `DirectConversionPattern<spirv::CLAtan2Op, LLVM::ATan2Op>` for the
/// OpenCL extended-instruction-set sibling of this same GLSL.std.450 op
/// (both `lhs`/`rhs`-shaped binary ops forwarding their operands in the
/// same order `llvm.intr.atan2(y, x)` expects).
class GLAtan2Pattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLAtan2Op> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLAtan2Op>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLAtan2Op Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    Rewriter.replaceOpWithNewOp<mlir::LLVM::ATan2Op>(
        Op, DstType, Adaptor.getLhs(), Adaptor.getRhs());
    return mlir::success();
  }
};

/// Converts `spirv.GL.Step` (roadmap L7c), whose GLSL.std.450 semantics
/// (`0.0` if `x < edge`, else `1.0`, computed per component) have no direct
/// single LLVM instruction/intrinsic equivalent, unlike e.g. `spirv.GL.
/// FMax`/`FMin`'s direct `llvm.intr.maxnum`/`minnum` mapping. Lowered to an
/// `llvm.fcmp olt` (ordered: per the GLSL.std.450 spec, `Step`'s own result
/// is only defined for non-NaN operands, so an unordered predicate would
/// just as validly satisfy the spec, but `olt` matches every sibling
/// `spirv.GL.*` compare-based pattern already in this file, e.g.
/// `SignPattern` above) followed by a scalar-or-vector-shaped
/// `llvm.select` between the two compile-time constants, mirroring
/// `SignPattern`'s own compare-then-select shape.
class GLStepPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLStepOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLStepOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLStepOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Edge = Adaptor.getLhs();
    mlir::Value X = Adaptor.getRhs();
    mlir::Value IsLess =
        mlir::LLVM::FCmpOp::create(Rewriter, Loc, getBoolTypeLike(DstType),
                                   mlir::LLVM::FCmpPredicate::olt, X, Edge);
    mlir::Value Zero = createSameShapeFPConstant(Rewriter, Loc, DstType, 0.0);
    mlir::Value One = createSameShapeFPConstant(Rewriter, Loc, DstType, 1.0);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::SelectOp>(Op, DstType, IsLess, Zero,
                                                      One);
    return mlir::success();
  }
};

/// Converts `spirv.GL.SmoothStep` (roadmap L7c) into the GLSL.std.450 spec's
/// own literal definition: `t = clamp((x - edge0) / (edge1 - edge0), 0, 1)`
/// followed by `t * t * (3 - 2 * t)` (a cubic Hermite interpolant), using
/// `llvm.intr.maxnum`/`llvm.intr.minnum` for the clamp -- the same pair
/// upstream's own `spirv.GL.FMax`/`FMin` `DirectConversionPattern`s already
/// map to -- rather than a compare-and-select chain, since `maxnum`/
/// `minnum`'s own NaN-quieting behavior already matches `FClamp`'s spec'd
/// per-component semantics with no extra pattern needed. Note
/// `spirv.GL.SmoothStep`'s generic ternary `x`/`y`/`z` operand names (see
/// `SPIRV_GLTernaryArithmeticOp` in SPIRVGLOps.td, shared with `spirv.GL.
/// FClamp` et al.) map to this op's own `edge0`/`edge1`/`x` positions in
/// that order, not `x`/`y`/`z`'s own literal meaning elsewhere.
class GLSmoothStepPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLSmoothStepOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLSmoothStepOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLSmoothStepOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Edge0 = Adaptor.getX();
    mlir::Value Edge1 = Adaptor.getY();
    mlir::Value X = Adaptor.getZ();
    mlir::Value Zero = createSameShapeFPConstant(Rewriter, Loc, DstType, 0.0);
    mlir::Value One = createSameShapeFPConstant(Rewriter, Loc, DstType, 1.0);
    mlir::Value Two = createSameShapeFPConstant(Rewriter, Loc, DstType, 2.0);
    mlir::Value Three = createSameShapeFPConstant(Rewriter, Loc, DstType, 3.0);

    mlir::Value Num =
        mlir::LLVM::FSubOp::create(Rewriter, Loc, DstType, X, Edge0);
    mlir::Value Den =
        mlir::LLVM::FSubOp::create(Rewriter, Loc, DstType, Edge1, Edge0);
    mlir::Value Ratio =
        mlir::LLVM::FDivOp::create(Rewriter, Loc, DstType, Num, Den);
    mlir::Value ClampedLow =
        mlir::LLVM::MaxNumOp::create(Rewriter, Loc, DstType, Ratio, Zero);
    mlir::Value T =
        mlir::LLVM::MinNumOp::create(Rewriter, Loc, DstType, ClampedLow, One);

    mlir::Value TwoT =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, DstType, Two, T);
    mlir::Value ThreeMinusTwoT =
        mlir::LLVM::FSubOp::create(Rewriter, Loc, DstType, Three, TwoT);
    mlir::Value TSquared =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, DstType, T, T);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::FMulOp>(Op, DstType, TSquared,
                                                    ThreeMinusTwoT);
    return mlir::success();
  }
};

/// Computes the dot product of two same-shaped scalar-or-vector float
/// operands, always returning a scalar result: a plain multiply for the
/// scalar case, or the same per-lane `llvm.intr.fmuladd` reduction chain
/// `DotConversionPattern` above uses for `spirv.Dot` (whose own operands
/// are always vectors), generalized here to also accept a plain scalar
/// operand pair. Needed by `spirv.GL.FaceForward`/`spirv.GL.Refract`
/// below (roadmap L87) since, unlike `spirv.Dot`, the GLSL.std.450 spec
/// allows either shape for these two ops' own vector-typed operands.
static mlir::Value
createScalarOrVectorDotProduct(mlir::ConversionPatternRewriter &Rewriter,
                               mlir::Location Loc, mlir::Value V1,
                               mlir::Value V2) {
  auto VectorTy = mlir::dyn_cast<mlir::VectorType>(V1.getType());
  if (!VectorTy)
    return mlir::LLVM::FMulOp::create(Rewriter, Loc, V1, V2);

  int64_t NumElements = VectorTy.getNumElements();
  auto ExtractElement = [&](mlir::Value Vector, int64_t Index) {
    mlir::Value IndexValue =
        mlir::LLVM::ConstantOp::create(Rewriter, Loc, Rewriter.getI64Type(),
                                       Rewriter.getI64IntegerAttr(Index));
    return mlir::LLVM::ExtractElementOp::create(Rewriter, Loc, Vector,
                                                IndexValue);
  };

  mlir::Value Result = mlir::LLVM::FMulOp::create(
      Rewriter, Loc, ExtractElement(V1, 0), ExtractElement(V2, 0));
  for (int64_t I = 1; I != NumElements; ++I)
    Result = mlir::LLVM::FMulAddOp::create(
        Rewriter, Loc, ExtractElement(V1, I), ExtractElement(V2, I), Result);
  return Result;
}

/// Broadcasts scalar \p Scalar to match \p Ty's shape via `broadcastScalar`
/// above if \p Ty is a vector type, or returns \p Scalar unchanged for a
/// plain scalar \p Ty. Lets a single scalar (e.g. `spirv.GL.Refract`'s own
/// `eta` operand, or an intermediate dot-product/comparison result shared
/// by `spirv.GL.FaceForward`/`spirv.GL.Refract` below) be combined
/// arithmetically with a scalar-or-vector operand of the same width
/// without a separate scalar/vector code path at each call site.
static mlir::Value
broadcastScalarToShapeOf(mlir::ConversionPatternRewriter &Rewriter,
                         mlir::Location Loc, mlir::Value Scalar,
                         mlir::Type Ty) {
  if (auto VecTy = mlir::dyn_cast<mlir::VectorType>(Ty))
    return broadcastScalar(Rewriter, Loc, Scalar, VecTy);
  return Scalar;
}

/// Converts `spirv.GL.FaceForward` (roadmap L87) into the GLSL.std.450
/// spec's own literal definition: `dot(Nref, I) < 0 ? N : -N`. `spirv.GL.
/// FaceForward`'s generic ternary `x`/`y`/`z` operand names (see
/// `SPIRV_GLTernaryArithmeticOp` in SPIRVGLOps.td, shared with `spirv.GL.
/// FClamp`/`spirv.GL.SmoothStep` et al.) map to `FaceForward`'s own
/// `N`/`I`/`Nref` positions in that order, mirroring dxc's own real
/// `OpExtInst %type %set FaceForward %N %I %Nref` operand order. Uses a
/// compare-then-select (`llvm.fcmp olt` against a zero constant, then
/// `llvm.select` between `N` and `llvm.fneg N`) rather than a branch,
/// matching every other `spirv.GL.*` pattern in this file.
class GLFaceForwardPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLFaceForwardOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLFaceForwardOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLFaceForwardOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value N = Adaptor.getX();
    mlir::Value I = Adaptor.getY();
    mlir::Value Nref = Adaptor.getZ();

    mlir::Value Dot = createScalarOrVectorDotProduct(Rewriter, Loc, Nref, I);
    mlir::Value Zero =
        createSameShapeFPConstant(Rewriter, Loc, Dot.getType(), 0.0);
    mlir::Value IsNegative = mlir::LLVM::FCmpOp::create(
        Rewriter, Loc, getBoolTypeLike(Dot.getType()),
        mlir::LLVM::FCmpPredicate::olt, Dot, Zero);
    mlir::Value IsNegativeLike =
        broadcastScalarToShapeOf(Rewriter, Loc, IsNegative,
                                 getBoolTypeLike(DstType));
    mlir::Value NegN = mlir::LLVM::FNegOp::create(Rewriter, Loc, DstType, N);

    Rewriter.replaceOpWithNewOp<mlir::LLVM::SelectOp>(Op, DstType,
                                                      IsNegativeLike, N, NegN);
    return mlir::success();
  }
};

/// Converts `spirv.GL.Refract` (roadmap L87) into the GLSL.std.450 spec's
/// own literal definition:
///
/// ```
/// k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I))
/// result = k < 0.0 ? genType(0) : eta * I - (eta * dot(N, I) + sqrt(k)) * N
/// ```
///
/// using a compare-then-select against a zero constant for the `k < 0.0`
/// case rather than a branch, matching every other `spirv.GL.*` pattern in
/// this file. `eta` (always a scalar, per `SPIRV_GLRefractOp`'s own
/// `SPIRV_Float:$eta` argument, unlike `i`/`n`/the result, which may be a
/// scalar or vector) is broadcast to `i`/`n`'s own shape via
/// `broadcastScalarToShapeOf` wherever it needs to combine arithmetically
/// with them.
class GLRefractPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLRefractOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLRefractOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLRefractOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value I = Adaptor.getI();
    mlir::Value N = Adaptor.getN();
    mlir::Value Eta = Adaptor.getEta();
    mlir::Type EtaType = Eta.getType();

    mlir::Value DotNI = createScalarOrVectorDotProduct(Rewriter, Loc, N, I);
    mlir::Value EtaSq = mlir::LLVM::FMulOp::create(Rewriter, Loc, Eta, Eta);
    mlir::Value DotNISq =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, DotNI, DotNI);
    mlir::Value OneScalar =
        createSameShapeFPConstant(Rewriter, Loc, EtaType, 1.0);
    mlir::Value OneMinusDotSq =
        mlir::LLVM::FSubOp::create(Rewriter, Loc, OneScalar, DotNISq);
    mlir::Value EtaSqTimesOneMinusDotSq =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, EtaSq, OneMinusDotSq);
    mlir::Value K = mlir::LLVM::FSubOp::create(Rewriter, Loc, OneScalar,
                                               EtaSqTimesOneMinusDotSq);

    mlir::Value Zero = createSameShapeFPConstant(Rewriter, Loc, DstType, 0.0);
    mlir::Value ZeroScalar =
        createSameShapeFPConstant(Rewriter, Loc, EtaType, 0.0);
    mlir::Value IsKNegative = mlir::LLVM::FCmpOp::create(
        Rewriter, Loc, getBoolTypeLike(EtaType),
        mlir::LLVM::FCmpPredicate::olt, K, ZeroScalar);
    mlir::Value IsKNegativeLike =
        broadcastScalarToShapeOf(Rewriter, Loc, IsKNegative,
                                 getBoolTypeLike(DstType));

    mlir::Value EtaLike = broadcastScalarToShapeOf(Rewriter, Loc, Eta, DstType);
    mlir::Value EtaI = mlir::LLVM::FMulOp::create(Rewriter, Loc, EtaLike, I);
    mlir::Value SqrtK = mlir::LLVM::SqrtOp::create(Rewriter, Loc, K);
    mlir::Value EtaDotNI = mlir::LLVM::FMulOp::create(Rewriter, Loc, Eta, DotNI);
    mlir::Value Sum = mlir::LLVM::FAddOp::create(Rewriter, Loc, EtaDotNI, SqrtK);
    mlir::Value SumLike = broadcastScalarToShapeOf(Rewriter, Loc, Sum, DstType);
    mlir::Value SumTimesN =
        mlir::LLVM::FMulOp::create(Rewriter, Loc, SumLike, N);
    mlir::Value ResultIfKNonNegative =
        mlir::LLVM::FSubOp::create(Rewriter, Loc, EtaI, SumTimesN);

    Rewriter.replaceOpWithNewOp<mlir::LLVM::SelectOp>(
        Op, DstType, IsKNegativeLike, Zero, ResultIfKNonNegative);
    return mlir::success();
  }
};

/// Converts `spirv.IsNan` (roadmap H124f): neither this op nor
/// `spirv.IsInf` below has an upstream MLIR SPIRVToLLVM conversion
/// pattern at all (only the reverse direction exists upstream --
/// `MathToSPIRV.cpp` builds `spirv.IsNan`/`IsInf` *from* `math.isnan`/
/// `math.isinf`, not the other way around). Lowered via the same
/// `llvm.fcmp uno` (unordered) predicate `FComparePattern<spirv::
/// UnorderedOp, ...>` already uses for the two-operand `spirv.Unordered`:
/// comparing any value against itself is unordered exactly when that
/// value is NaN (IEEE-754's own definition of "unordered"), needing no
/// separate NaN-specific predicate or intrinsic.
class IsNanPattern : public mlir::SPIRVToLLVMConversion<mlir::spirv::IsNanOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::IsNanOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::IsNanOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Value X = Adaptor.getOperand();
    Rewriter.replaceOpWithNewOp<mlir::LLVM::FCmpOp>(
        Op, DstType, mlir::LLVM::FCmpPredicate::uno, X, X);
    return mlir::success();
  }
};

/// Converts `spirv.IsInf` (roadmap H124f): see `IsNanPattern` above for why
/// this has no upstream pattern to reuse either. Lowered as
/// `llvm.intr.fabs(x) == +Inf` (`llvm.fcmp oeq`): folding the sign away
/// first with `llvm.intr.fabs` avoids needing two compares (one per
/// infinity sign) combined with an `or`.
class IsInfPattern : public mlir::SPIRVToLLVMConversion<mlir::spirv::IsInfOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::IsInfOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::IsInfOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value X = Adaptor.getOperand();
    mlir::Value AbsX =
        mlir::LLVM::FAbsOp::create(Rewriter, Loc, X.getType(), X);
    mlir::Value Inf = createSameShapeFPConstant(
        Rewriter, Loc, X.getType(), std::numeric_limits<double>::infinity());
    Rewriter.replaceOpWithNewOp<mlir::LLVM::FCmpOp>(
        Op, DstType, mlir::LLVM::FCmpPredicate::oeq, AbsX, Inf);
    return mlir::success();
  }
};

/// Converts `spirv.GL.Length` (roadmap H124f) into the GLSL.std.450 spec's
/// own definition, `sqrt(dot(x, x))`, reusing `createScalarOrVectorDotProduct`
/// above (the same helper `spirv.GL.FaceForward`/`spirv.GL.Refract` use).
/// This op has no upstream MLIR conversion pattern at all.
class GLLengthPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLLengthOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLLengthOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLLengthOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value X = Adaptor.getOperand();
    mlir::Value Dot = createScalarOrVectorDotProduct(Rewriter, Loc, X, X);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::SqrtOp>(Op, DstType, Dot);
    return mlir::success();
  }
};

/// Converts `spirv.GL.Normalize` (roadmap H124f) into the GLSL.std.450
/// spec's own definition, `x / Length(x)`, reusing the same `sqrt(dot(x,
/// x))` computation `GLLengthPattern` above uses and
/// `broadcastScalarToShapeOf` (needed since `Length`'s own result is
/// always a scalar, but `Normalize`'s result keeps `x`'s own scalar-or-
/// vector shape) to divide every lane by it. This op has no upstream MLIR
/// conversion pattern at all either.
class GLNormalizePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::GLNormalizeOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::GLNormalizeOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::GLNormalizeOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    mlir::Type DstType = getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value X = Adaptor.getOperand();
    mlir::Value Dot = createScalarOrVectorDotProduct(Rewriter, Loc, X, X);
    mlir::Value Len = mlir::LLVM::SqrtOp::create(Rewriter, Loc, Dot);
    mlir::Value LenLike = broadcastScalarToShapeOf(Rewriter, Loc, Len, DstType);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::FDivOp>(Op, DstType, X, LenLike);
    return mlir::success();
  }
};

/// Returns the rounding mode \p Op's own `fp_rounding_mode` decoration
/// (`VK_KHR_shader_float_controls2`'s per-instruction `FPRoundingMode`,
/// roadmap F15c) requests, or none if \p Op carries no such decoration.
/// MLIR's SPIR-V deserializer already attaches a decorated instruction's
/// `FPRoundingMode` straight onto the op it decorates as this attribute
/// (`Deserializer.cpp`'s `processDecoration`, keyed by
/// `llvm::convertToSnakeFromCamelCase` of the decoration's own name), so,
/// unlike `RoundingModeRTZ`/`DenormFlushToZero` (roadmap F15a/F15b), no
/// separate collection pass is needed: by the time this op's own pattern
/// runs, the decoration is already a plain attribute on it. Unlike those
/// two execution modes (which only ever ask for round-toward-zero), this
/// per-instruction decoration can name any of the four IEEE rounding
/// directions, including `RTE` explicitly overriding an enclosing entry
/// point's own `RoundingModeRTZ` back to round-to-nearest-even for one
/// instruction.
std::optional<mlir::LLVM::RoundingMode>
getRoundingModeDecoration(mlir::Operation *Op) {
  auto Attr =
      Op->getAttrOfType<mlir::spirv::FPRoundingModeAttr>("fp_rounding_mode");
  if (!Attr)
    return std::nullopt;
  switch (Attr.getValue()) {
  case mlir::spirv::FPRoundingMode::RTE:
    return mlir::LLVM::RoundingMode::NearestTiesToEven;
  case mlir::spirv::FPRoundingMode::RTZ:
    return mlir::LLVM::RoundingMode::TowardZero;
  case mlir::spirv::FPRoundingMode::RTP:
    return mlir::LLVM::RoundingMode::TowardPositive;
  case mlir::spirv::FPRoundingMode::RTN:
    return mlir::LLVM::RoundingMode::TowardNegative;
  }
  llvm_unreachable("unhandled spirv::FPRoundingMode");
}

/// Translates \p Mode, a decorated instruction's own `fp_fast_math_mode`
/// (roadmap F15c) or an entry point's per-type `FPFastMathDefault` (roadmap
/// F15d), to the LLVM fast-math flags it requests, mapping each bit
/// `float_controls2`'s core (non-vendor) profile can express to its LLVM
/// equivalent.
mlir::LLVM::FastmathFlags
translateFastMathMode(mlir::spirv::FPFastMathMode Mode) {
  auto Has = [&](mlir::spirv::FPFastMathMode Bit) {
    return (Mode & Bit) != mlir::spirv::FPFastMathMode::None;
  };
  mlir::LLVM::FastmathFlags Flags = mlir::LLVM::FastmathFlags::none;
  if (Has(mlir::spirv::FPFastMathMode::Fast))
    Flags = Flags | mlir::LLVM::FastmathFlags::fast;
  if (Has(mlir::spirv::FPFastMathMode::NotNaN))
    Flags = Flags | mlir::LLVM::FastmathFlags::nnan;
  if (Has(mlir::spirv::FPFastMathMode::NotInf))
    Flags = Flags | mlir::LLVM::FastmathFlags::ninf;
  if (Has(mlir::spirv::FPFastMathMode::NSZ))
    Flags = Flags | mlir::LLVM::FastmathFlags::nsz;
  if (Has(mlir::spirv::FPFastMathMode::AllowRecip))
    Flags = Flags | mlir::LLVM::FastmathFlags::arcp;
  // `AllowContractFastINTEL`/`AllowReassocINTEL` map both the `INTEL`
  // vendor pair and `VK_KHR_shader_float_controls2`'s own, non-vendor
  // `AllowContract`/`AllowReassoc` bits (roadmap F15d): the two extensions
  // share the same bit positions (see `SPIRVBase.td`'s own comment on
  // these enumerants), so a Vulkan shader setting either bit -- it can
  // only ever be this extension's own, since Vulkan shaders have no way
  // to declare `FPFastMathModeINTEL` -- decodes to these same enumerant
  // names.
  if (Has(mlir::spirv::FPFastMathMode::AllowContractFastINTEL))
    Flags = Flags | mlir::LLVM::FastmathFlags::contract;
  if (Has(mlir::spirv::FPFastMathMode::AllowReassocINTEL))
    Flags = Flags | mlir::LLVM::FastmathFlags::reassoc;
  // `AllowTransform` (roadmap F15d) is a superset of `AllowContract`/
  // `AllowReassoc` (the SPIR-V spec requires both bits set alongside it),
  // permitting arbitrary real-number-rule transformations rather than just
  // contraction/reassociation -- LLVM's `afn` ("approximate functions")
  // flag is the closest match for that broader license, rather than a
  // literal one-bit-to-one-flag correspondence.
  if (Has(mlir::spirv::FPFastMathMode::AllowTransform))
    Flags = Flags | mlir::LLVM::FastmathFlags::afn;
  return Flags;
}

/// Returns the LLVM fast-math flags \p Op's own `fp_fast_math_mode`
/// decoration (`FPFastMathMode`, roadmap F15c) requests. This is a
/// separate, additive mechanism from `FPRoundingMode` above -- ordinary
/// LLVM fast-math flags rather than another constrained-intrinsics
/// consumer -- applied only to the plain (round-to-nearest-even) op below:
/// LLVM's constrained intrinsics carry no fast-math flags of their own
/// (`LLVM_ConstrainedIntr`, `LLVMIntrinsicOps.td`, sets
/// `requiresFastmath=0`), so a shader that combines a non-default rounding
/// mode with fast-math on the very same instruction keeps the rounding
/// behavior and drops the fast-math request, an intentional scoping
/// decision rather than an oversight -- Vulkan shaders needing both would
/// need to request the rounding mode and fast-math flags on two different
/// instructions.
mlir::LLVM::FastmathFlags getFastMathFlagsDecoration(mlir::Operation *Op) {
  auto Attr =
      Op->getAttrOfType<mlir::spirv::FPFastMathModeAttr>("fp_fast_math_mode");
  if (!Attr)
    return mlir::LLVM::FastmathFlags::none;
  return translateFastMathMode(Attr.getValue());
}

/// Converts an arithmetic FP `spirv` op that would otherwise become MLIR's
/// plain, round-to-nearest-even, denormal-preserving `PlainOp` (upstream
/// `SPIRVToLLVM.cpp`'s `DirectConversionPattern`) into whichever of the
/// following \p Op's own bit width was declared for by the enclosing entry
/// point's `VK_KHR_shader_float_controls` execution modes, or \p Op's own
/// `VK_KHR_shader_float_controls2` per-instruction decorations, requests
/// (the concrete, "actually produces the requested code" half of these
/// modes this conversion previously could only diagnose, not honor -- see
/// roadmap F15a/F15b/F15c and (historically)
/// rejectUnhonoredFloatControls, ConvertSPIRVToLLVMPass.cpp):
///
/// - `RoundingModeRTZ` (F15a), a whole-entry-point execution mode, and
///   `FPRoundingMode` (F15c), a per-instruction decoration that overrides
///   it for the one instruction it decorates (including explicitly naming
///   `RTE` to opt an instruction back out of an entry point's own
///   `RoundingModeRTZ`): the operation itself becomes LLVM's constrained
///   `llvm.experimental.constrained.*` intrinsic (\p ConstrainedIntrOp)
///   with an explicit, non-default rounding mode, rather than \p PlainOp.
///   The exception behavior is always "ignore": Vulkan shaders have no
///   floating-point trap mechanism to honor a stricter one, and \p
///   PlainOp does not honor one either.
/// - `DenormFlushToZero` (F15b, a whole-entry-point execution mode only --
///   `VK_KHR_shader_float_controls2` does not add a per-instruction denorm
///   decoration at all, confirmed against the SPIR-V spec and LLVM's own
///   `SPIRVSymbolicOperands.td`): both operands, and the operation's
///   result, are each flushed to a same-signed zero first if subnormal
///   (see flushSubnormalToZero) -- LLVM has no constrained-intrinsics
///   equivalent for flush-to-zero, so this is an explicit software flush
///   around an ordinary operation rather than a different intrinsic, a
///   materially different lowering strategy from `RoundingModeRTZ`'s.
/// - `FPFastMathMode` (F15c, a per-instruction decoration): translated to
///   LLVM fast-math flags on \p PlainOp (see getFastMathFlagsDecoration);
///   dropped, rather than applied to \p ConstrainedIntrOp, if this
///   instruction also ends up with a non-default rounding mode, since
///   LLVM's constrained intrinsics carry no fast-math flags of their own.
/// - `FPFastMathDefault` (F15d, a whole-entry-point default, one per
///   floating-point type): translated the same way as an explicit
///   `FPFastMathMode` decoration above, but only for an arithmetic op of
///   the matching bit width that carries no such decoration of its own --
///   a decoration always overrides its own entry point's default for that
///   one instruction, the same precedence `FPRoundingMode` already has
///   over `RoundingModeRTZ`.
///
/// All three may apply to the same instruction at once (flushing subnormal
/// operands, rounding in a non-default direction, then flushing a
/// subnormal result -- fast-math flags aside, per above), independently of
/// each other. Any bit width neither whole-entry-point mode was declared
/// for, on an op with neither per-instruction decoration either, falls
/// through (a match failure, the same as any other pattern that does not
/// apply) to MLIR's own lower-benefit `DirectConversionPattern`, unchanged.
///
/// `spirv.FNegate` is deliberately not one of the ops this pattern (or any
/// sibling of it) handles: negation is an exact sign-bit flip with no
/// rounding behavior of its own, and can only ever produce a subnormal
/// result if its operand already was one (which the operand's own
/// producer, if any, already flushed), so it needs neither a constrained
/// form (LLVM has none) nor a software flush of its own.
template <typename SPIRVOp, typename PlainOp, typename ConstrainedIntrOp>
class FloatControlArithmeticPattern
    : public mlir::SPIRVToLLVMConversion<SPIRVOp> {
public:
  FloatControlArithmeticPattern(
      mlir::MLIRContext *Context, const mlir::LLVMTypeConverter &TypeConverter,
      mlir::PatternBenefit Benefit,
      const feme::spirv::FloatControlInfoMap &RoundingModeRTZWidths,
      const feme::spirv::FloatControlInfoMap &DenormFlushToZeroWidths,
      const feme::spirv::FastMathDefaultMap &FastMathDefaults)
      : mlir::SPIRVToLLVMConversion<SPIRVOp>(Context, TypeConverter, Benefit),
        RoundingModeRTZWidths(RoundingModeRTZWidths),
        DenormFlushToZeroWidths(DenormFlushToZeroWidths),
        FastMathDefaults(FastMathDefaults) {}

  mlir::LogicalResult
  matchAndRewrite(SPIRVOp Op, typename SPIRVOp::Adaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    // Looked up via `FunctionOpInterface` (rather than `mlir::spirv::FuncOp`
    // specifically) because by the time this op's own pattern runs, the
    // enclosing `spirv.func` may already have been legalized into an
    // `llvm.func` -- `populateSPIRVToLLVMFunctionConversionPatterns` moves
    // this op's region into the new function rather than waiting for every
    // op inside it to convert first -- and both implement the interface
    // under the same, conversion-preserved symbol name.
    auto Func = Op->template getParentOfType<mlir::FunctionOpInterface>();
    if (!Func)
      return Rewriter.notifyMatchFailure(Op, "not inside a function");

    unsigned Width =
        mlir::getElementTypeOrSelf(Op.getType()).getIntOrFloatBitWidth();
    bool FlushDenormals = declaresWidth(DenormFlushToZeroWidths, Func, Width);

    // A per-instruction `FPRoundingMode` decoration (F15c) overrides the
    // entry point's own whole-module `RoundingModeRTZ` (F15a) for this one
    // instruction; absent one, fall back to that entry-point-wide mode
    // (round-to-nearest-even, i.e. no override at all, if it did not
    // declare `RoundingModeRTZ` for this width either).
    std::optional<mlir::LLVM::RoundingMode> PerOpRounding =
        getRoundingModeDecoration(Op);
    std::optional<mlir::LLVM::RoundingMode> Rounding = PerOpRounding;
    if (!Rounding && declaresWidth(RoundingModeRTZWidths, Func, Width))
      Rounding = mlir::LLVM::RoundingMode::TowardZero;
    bool NeedsConstrainedOp =
        Rounding && *Rounding != mlir::LLVM::RoundingMode::NearestTiesToEven;

    mlir::LLVM::FastmathFlags FastMath = getFastMathFlagsDecoration(Op);
    bool HasFastMathDecoration = static_cast<bool>(
        Op->template getAttrOfType<mlir::spirv::FPFastMathModeAttr>(
            "fp_fast_math_mode"));
    // Absent its own decoration, an arithmetic op falls back to its entry
    // point's `FPFastMathDefault` for its own bit width (roadmap F15d), if
    // one was declared -- the same "decoration overrides entry-point-wide
    // default" precedence `FPRoundingMode` above already gets over
    // `RoundingModeRTZ`.
    if (!HasFastMathDecoration) {
      auto EntryIt = FastMathDefaults.find(Func.getName());
      if (EntryIt != FastMathDefaults.end()) {
        auto WidthIt = EntryIt->second.find(Width);
        if (WidthIt != EntryIt->second.end())
          FastMath = translateFastMathMode(WidthIt->second);
      }
    }
    // Even an `fp_rounding_mode` decoration that resolves to no override at
    // all (an explicit `RTE` with nothing else applying) still has to match
    // here rather than fall through: MLIR's own lower-benefit
    // `DirectConversionPattern` forwards every attribute an op carries
    // verbatim (`op->getAttrs()`, upstream `SPIRVToLLVM.cpp`), which would
    // otherwise leave the now-meaningless decoration attribute stuck on the
    // resulting `llvm.fadd`/etc. rather than actually being consumed.
    if (!NeedsConstrainedOp && !FlushDenormals &&
        FastMath == mlir::LLVM::FastmathFlags::none && !PerOpRounding &&
        !HasFastMathDecoration)
      return Rewriter.notifyMatchFailure(
          Op, "enclosing entry point declared neither RoundingModeRTZ, "
              "DenormFlushToZero, nor an FPFastMathDefault for this bit "
              "width, and this instruction has neither an FPRoundingMode "
              "nor an FPFastMathMode decoration of its own");

    mlir::Type DstType = this->getTypeConverter()->convertType(Op.getType());
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Lhs = Adaptor.getOperand1();
    mlir::Value Rhs = Adaptor.getOperand2();
    if (FlushDenormals) {
      Lhs = flushSubnormalToZero(Rewriter, Loc, Lhs);
      Rhs = flushSubnormalToZero(Rewriter, Loc, Rhs);
    }

    mlir::Value Result;
    if (NeedsConstrainedOp) {
      mlir::MLIRContext *Ctx = Rewriter.getContext();
      auto RoundingAttr = mlir::LLVM::RoundingModeAttr::get(Ctx, *Rounding);
      auto ExceptionBehavior = mlir::LLVM::FPExceptionBehaviorAttr::get(
          Ctx, mlir::LLVM::FPExceptionBehavior::Ignore);
      Result = ConstrainedIntrOp::create(Rewriter, Loc, DstType, Lhs, Rhs,
                                         RoundingAttr, ExceptionBehavior);
    } else if (FastMath != mlir::LLVM::FastmathFlags::none) {
      auto FastMathAttr =
          mlir::LLVM::FastmathFlagsAttr::get(Rewriter.getContext(), FastMath);
      Result = PlainOp::create(Rewriter, Loc, DstType, Lhs, Rhs, FastMathAttr);
    } else {
      Result = PlainOp::create(Rewriter, Loc, DstType, Lhs, Rhs);
    }
    if (FlushDenormals)
      Result = flushSubnormalToZero(Rewriter, Loc, Result);

    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }

private:
  /// Returns whether \p Widths declares \p Width for \p Func.
  static bool declaresWidth(const feme::spirv::FloatControlInfoMap &Widths,
                             mlir::FunctionOpInterface Func, unsigned Width) {
    auto It = Widths.find(Func.getName());
    return It != Widths.end() && llvm::is_contained(It->second, Width);
  }

  const feme::spirv::FloatControlInfoMap &RoundingModeRTZWidths;
  const feme::spirv::FloatControlInfoMap &DenormFlushToZeroWidths;
  const feme::spirv::FastMathDefaultMap &FastMathDefaults;
};

/// (roadmap E4) Drops `spirv.ExecutionModeId` (`VK_KHR_maintenance4`'s
/// `LocalSizeId`, among others): like plain `spirv.ExecutionMode` above,
/// `GroupSize.cpp`'s `resolveComputeGroupSize` already reads its operands
/// from the raw SPIR-V word stream before this pass ever runs, and
/// `Pipeline.cpp`'s `compileComputePipeline` stamps the resolved group
/// size onto the entry point itself -- upstream MLIR has no conversion
/// pattern for this op at all (only plain `ExecutionMode`), so leaving it
/// in place would otherwise fail legalization for every `LocalSizeId`
/// shader, the exact opposite of `maintenance4`'s own "add support for
/// LocalSizeId" intent.
class ExecutionModeIdPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ExecutionModeIdOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ExecutionModeIdOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ExecutionModeIdOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// (roadmap E4) Drops `spirv.SpecConstant`: upstream MLIR has no
/// conversion pattern for it at all, so a `LocalSizeId` shader's
/// supporting specialization constants (there is no other way to spell
/// `LocalSizeId`'s three operands) would otherwise fail legalization even
/// though nothing in the entry point's own body ever references them --
/// `ExecutionModeIdPattern` above already erased their only reference. A
/// specialization constant genuinely read by the shader body (via
/// `spirv.mlir.referenceof`) is resolved directly to its own compile-time
/// value by `ReferenceOfConversionPattern` below (roadmap L7j) before this
/// pattern's own erasure ever runs, so this pattern's job is unconditional
/// declaration cleanup either way, not a "no real reference exists" bet.
class SpecConstantErasurePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::SpecConstantOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::SpecConstantOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::SpecConstantOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Drops `spirv.SpecConstantComposite` (roadmap L7j), mirroring
/// `SpecConstantErasurePattern` above for the composite case: any real
/// reference to it has already been resolved directly to a compile-time
/// value by `ReferenceOfConversionPattern` below, via the same
/// `feme::spirv::SpecConstantValueMap` `prepareSpecConstants` collected
/// before this conversion ran (by the time this declaration's own use is
/// legalized, the value it would resolve to has to already be in hand --
/// see prepareSpecConstants's own comment for why), so this declaration's
/// own removal is unconditional cleanup, never a silent gap.
class SpecConstantCompositeErasurePattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::SpecConstantCompositeOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::SpecConstantCompositeOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::SpecConstantCompositeOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    Rewriter.eraseOp(Op);
    return mlir::success();
  }
};

/// Returns \p Type's own element type if it (or, for a vector, its element
/// type) is a *signed* or *unsigned* (not signless) `IntegerType` -- the
/// same SPIR-V-vs-LLVM integer-signedness mismatch upstream's own
/// `ConstantScalarAndVectorPattern` (`spirv.Constant`) already has to
/// retype away, needed again here since a specialization constant's own
/// declared value carries the identical mismatch. A null result means no
/// such retyping is needed (a signless integer, a float, or a bool -- all
/// already representable as-is).
static mlir::IntegerType
getSignedOrUnsignedIntElementType(mlir::Type Type) {
  mlir::Type ElementType = Type;
  if (auto VecType = mlir::dyn_cast<mlir::VectorType>(Type))
    ElementType = VecType.getElementType();
  auto IntType = mlir::dyn_cast<mlir::IntegerType>(ElementType);
  if (IntType && !IntType.isSignless())
    return IntType;
  return {};
}

/// Converts `spirv.mlir.referenceof` (roadmap L7j) by materializing the
/// specialization constant it names directly as an `llvm.mlir.constant` of
/// its own resolved compile-time value (`feme::spirv::SpecConstantValueMap`,
/// collected by `prepareSpecConstants` before this conversion ran): this
/// ICD has no runtime `VkSpecializationInfo` override mechanism threaded
/// through `Pipeline.cpp`'s own pipeline-creation path at all, so a spec
/// constant's own declared default value is the only value it could ever
/// actually take, making this fold always correct rather than merely an
/// approximation. Declines (rather than approximates) any symbol
/// `prepareSpecConstants` itself already declined to resolve -- a
/// `struct`/nested-`array`-shaped `spirv.SpecConstantComposite`, unreached
/// by any known real HLSL/CTS source today (see its own comment) -- with a
/// precise diagnostic naming the actual gap, rather than the generic
/// "unresolved symbol" one a bare `SymbolTable` lookup failure would give.
class ReferenceOfConversionPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ReferenceOfOp> {
public:
  ReferenceOfConversionPattern(
      mlir::MLIRContext *Context, const mlir::LLVMTypeConverter &TypeConverter,
      mlir::PatternBenefit Benefit,
      const feme::spirv::SpecConstantValueMap &SpecConstants)
      : mlir::SPIRVToLLVMConversion<mlir::spirv::ReferenceOfOp>(
            Context, TypeConverter, Benefit),
        SpecConstants(SpecConstants) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ReferenceOfOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    auto It = SpecConstants.find(Op.getSpecConst());
    if (It == SpecConstants.end())
      return Rewriter.notifyMatchFailure(
          Op, "referenced specialization constant has no resolvable "
              "compile-time value (e.g. a struct/nested-array-shaped "
              "composite)");

    mlir::Type SrcType = It->second.getType();
    mlir::Type DstType = getTypeConverter()->convertType(SrcType);
    if (!DstType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    if (mlir::IntegerType SrcIntType =
            getSignedOrUnsignedIntElementType(SrcType)) {
      mlir::IntegerType SignlessType =
          Rewriter.getIntegerType(SrcIntType.getWidth());
      if (mlir::isa<mlir::VectorType>(SrcType)) {
        auto SrcElements = mlir::cast<mlir::DenseIntElementsAttr>(It->second);
        Rewriter.replaceOpWithNewOp<mlir::LLVM::ConstantOp>(
            Op, DstType,
            SrcElements.mapValues(
                SignlessType,
                [](const llvm::APInt &Value) { return Value; }));
        return mlir::success();
      }
      auto SrcAttr = mlir::cast<mlir::IntegerAttr>(It->second);
      Rewriter.replaceOpWithNewOp<mlir::LLVM::ConstantOp>(
          Op, DstType,
          Rewriter.getIntegerAttr(SignlessType, SrcAttr.getValue()));
      return mlir::success();
    }

    Rewriter.replaceOpWithNewOp<mlir::LLVM::ConstantOp>(Op, DstType,
                                                        It->second);
    return mlir::success();
  }

private:
  const feme::spirv::SpecConstantValueMap &SpecConstants;
};

/// Converts a uniform buffer array's own content (see BlockElement's own
/// comment for the `layout(std140) uniform Input { uint data[16]; }` shape
/// roadmap F12a covers) to the `!llvm.array<0 x ElemTy>` marker a storage
/// buffer's own runtime array wrapper already uses (see
/// convertBufferBlockType's own comment) -- the real element count plays no
/// role in `feme::cpu::SPIRVResourceLoweringPass`'s own classification,
/// which reads the array's element type only -- returning \p Array's own
/// declared `ArrayStride` alongside it, or `std::nullopt` if \p Array's
/// element type itself does not convert.
///
/// The stride cannot be recovered by converting \p Array through the
/// ordinary `TypeConverter` the way `convertBufferBlockType`/every other
/// `BlockElement::Content` shape is (see MLIR's own `convertArrayType` in
/// SPIRVToLLVM.cpp, which refuses to convert an array whose stride does
/// not equal its element's own natural size at all): a std140 array's
/// stride need not equal that natural size the way a std430 storage
/// buffer array's always does (e.g. this scalar `uint` case's 4-byte size
/// against its own 16-byte stride). It is instead carried explicitly as
/// `spirv.VulkanBuffer`'s own third integer parameter (see
/// `classifyVulkanBufferHandle`'s own comment in SPIRVResourceLowering.cpp),
/// read back by `feme::cpu::SPIRVResourceLoweringPass` alongside the
/// ordinary two every other `spirv.VulkanBuffer` handle carries.
std::pair<mlir::Type, uint32_t>
convertUniformArrayContent(mlir::spirv::ArrayType Array,
                           mlir::Type ElementType) {
  return {mlir::LLVM::LLVMArrayType::get(ElementType, 0),
         Array.getArrayStride()};
}

/// Converts a storage buffer block pointer to the `spirv.VulkanBuffer`
/// handle type LLVM's SPIRV backend materializes it from, mirroring
/// convertImageTypeAs's role for image/sampler resources: the type
/// parameter is the block's content (see getBufferBlockElement -- a 0-sized
/// `!llvm.array` for the wrapper shape, or the block's own struct,
/// including any trailing array member, for the shape glslang emits
/// directly), and the two integer parameters are the storage class
/// (forwarded unchanged, like an image type's parameters) and whether the
/// buffer is writable (`RWStructuredBuffer<T>`/a GLSL `buffer` block) or
/// not (`StructuredBuffer<T>`/a GLSL `readonly buffer` block).
mlir::Type
convertBufferBlockType(mlir::spirv::PointerType Type,
                       const mlir::LLVMTypeConverter &TypeConverter) {
  std::optional<BlockElement> Element = getBufferBlockElement(Type);
  if (!Element)
    return nullptr;
  mlir::Type ContentType = TypeConverter.convertType(Element->Content);
  if (!ContentType)
    return nullptr;

  auto Struct = mlir::cast<mlir::spirv::StructType>(Type.getPointeeType());
  bool Writable = true;
  if (Element->HasWrapper)
    Writable = isBufferBlockWritable(Struct, 0);
  else if (std::optional<unsigned> ArrayMember =
               getTrailingRuntimeArrayMember(Struct))
    Writable = isBufferBlockWritable(Struct, *ArrayMember);

  return mlir::LLVM::LLVMTargetExtType::get(
      Type.getContext(), "spirv.VulkanBuffer", {ContentType},
      {static_cast<unsigned>(Type.getStorageClass()), Writable ? 1u : 0u});
}

/// Converts a uniform buffer block pointer to the same `spirv.VulkanBuffer`
/// handle type convertBufferBlockType produces for a storage buffer block
/// -- its type parameter is the block's content (see getUniformBlockElement
/// -- the block's own field struct for the wrapper shape, that struct
/// directly for the shape glslang emits (including any sized-array or
/// matrix member), or -- roadmap F12a -- a sole fixed-size array member's
/// own content, converted through convertUniformArrayContent above instead
/// since it needs its own explicit third integer parameter). Vulkan
/// disallows writing `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, so the
/// writability integer parameter is always 0, unlike a storage buffer
/// block's.
mlir::Type
convertUniformBlockType(mlir::spirv::PointerType Type,
                        const mlir::LLVMTypeConverter &TypeConverter) {
  std::optional<BlockElement> Element = getUniformBlockElement(Type);
  if (!Element)
    return nullptr;

  if (auto Array = mlir::dyn_cast<mlir::spirv::ArrayType>(Element->Content)) {
    mlir::Type ElementType = TypeConverter.convertType(Array.getElementType());
    if (!ElementType)
      return nullptr;
    auto [ContentType, Stride] = convertUniformArrayContent(Array, ElementType);
    return mlir::LLVM::LLVMTargetExtType::get(
        Type.getContext(), "spirv.VulkanBuffer", {ContentType},
        {static_cast<unsigned>(Type.getStorageClass()), /*Writable=*/0u,
         Stride});
  }

  mlir::Type ContentType = TypeConverter.convertType(Element->Content);
  if (!ContentType)
    return nullptr;

  return mlir::LLVM::LLVMTargetExtType::get(
      Type.getContext(), "spirv.VulkanBuffer", {ContentType},
      {static_cast<unsigned>(Type.getStorageClass()), /*Writable=*/0u});
}

} // namespace

void feme::spirv::populateSPIRVToLLVMTargetTypeConversions(
    mlir::LLVMTypeConverter &TypeConverter) {
  // Registered after MLIR's conversions so it is tried before them: a
  // resource handle, like a builtin `Input` variable
  // (BuiltInAddressOfPattern), is a value LLVM's SPIRV backend materializes
  // on demand rather than memory, so the pointer SPIR-V reads either through
  // has nothing to convert to but the value itself. A non-builtin, non-array
  // `Input` variable (an ordinary stage-IO variable FeMe has no way to
  // distinguish from a builtin one by type alone, since e.g. both can be a
  // plain `i32`) shares this same conversion -- StageIOAddressOfPattern
  // accordingly reads it eagerly at the `spirv.mlir.addressof` site too
  // (rather than converting to a real pointer the way a non-builtin
  // `Output` variable does just below, which no builtin ever is), so that
  // this conversion's answer for every scalar/vector `Input` pointer type
  // stays exactly one thing regardless of which kind of variable it is.
  //
  // (Roadmap H7y/H87/H82) A *composite*-typed `Input` pointer (e.g.
  // `gl_in[]`, a geometry/tessellation entry's own per-vertex input; the
  // standalone `gl_ClipDistance`/`gl_CullDistance` builtin array; a mesh
  // shader's own per-primitive/per-vertex flat-array interface block,
  // which SPIR-V's `Block` decoration forces to be a single-member struct
  // wrapping the array rather than a bare array; or an ordinary
  // multi-member `Input` interface block with no array at all, e.g. a
  // fragment stage's own per-primitive-EXT varying block -- see
  // isCompositeStageIOType) is the one shape this "always a value" answer
  // cannot fully support: LLVM's `extractvalue` can only select a
  // compile-time-constant index out of a value, so a genuinely dynamic
  // (loop-carried or per-primitive) index into an array has no
  // representation as a value-typed read whatsoever, and even a
  // constant-indexed struct member read needs a real pointer base since
  // MLIR's own generic `AccessChainPattern` (see below) unconditionally
  // builds a `getelementptr` regardless of member count.
  // StageIOAddressOfPattern accordingly keeps a composite `Input` variable
  // a real pointer instead (see its own comment) -- this conversion has to
  // answer consistently, or a later pattern that calls back into it (e.g.
  // MLIR's own generic `AccessChainPattern`, which decides whether its
  // base operand is a real pointer by re-converting the *original* SPIR-V
  // pointer type rather than inspecting the already-converted operand's
  // actual type) would compute the wrong shape and build an ill-typed
  // `getelementptr`.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Input &&
        !isResourcePointer(Type))
      return std::nullopt;
    if (Type.getStorageClass() == mlir::spirv::StorageClass::Input &&
        isCompositeStageIOType(Type.getPointeeType())) {
      if (!TypeConverter.convertType(Type.getPointeeType()))
        return std::nullopt;
      return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                              /*addressSpace=*/7);
    }
    return TypeConverter.convertType(Type.getPointeeType());
  });

  // Supersedes MLIR's own `spirv::StructType` conversion (see
  // `convertOffsetStructTypeIgnoringDecorations`'s comment for why: a
  // `Block`-decorated struct with explicit member offsets -- every real
  // uniform/push-constant block -- is otherwise spuriously rejected).
  // `std::nullopt` (not a struct this can lay out) falls through to
  // MLIR's own conversion, which will also fail identically, so no
  // real coverage is lost by preferring this one.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::StructType Type)
                                  -> std::optional<mlir::Type> {
    if (mlir::Type Converted =
            convertOffsetStructTypeIgnoringDecorations(Type, TypeConverter))
      return Converted;
    return std::nullopt;
  });

  // A non-builtin `Output` variable (a stage-IO variable: a vertex shader's
  // output, a fragment shader's render target, and so on) is real memory,
  // unlike an `Input` variable (which, builtin or not, is read through the
  // conversion just above instead) -- StageIOGlobalVariablePattern converts
  // its declaration to an ordinary `llvm.mlir.global`, so its pointer
  // converts to an ordinary pointer too, in the address space (8) LLVM's
  // SPIRV backend expects that storage class to use (see
  // `storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`)
  // rather than MLIR's own Vulkan-client default of address space 0. No
  // builtin variable is ever `Output`, so this has no equivalent ambiguity
  // to resolve.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Output)
      return std::nullopt;
    if (!TypeConverter.convertType(Type.getPointeeType()))
      return std::nullopt;
    return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                            /*addressSpace=*/8);
  });

  // A storage buffer block's own pointer converts to the handle LLVM's
  // SPIRV backend materializes it from; any other `StorageBuffer` pointer --
  // an access chain result reaching into the buffer's contents -- is
  // ordinary memory, addressed the way that backend expects a storage
  // buffer access to be (address space 11, see `storageClassToAddressSpace`
  // in `llvm/lib/Target/SPIRV/SPIRVUtils.h`) rather than MLIR's own
  // Vulkan-client default of address space 0.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::StorageBuffer)
      return std::nullopt;
    if (mlir::Type Handle = convertBufferBlockType(Type, TypeConverter))
      return Handle;
    return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                            /*addressSpace=*/11);
  });

  // A uniform buffer block's own pointer converts to the `spirv.VulkanBuffer`
  // handle representation too (see convertUniformBlockType above); a
  // pre-SPIR-V-1.3 SSBO shares the `Uniform` storage class but is a storage
  // buffer block in every other respect (see isBufferBlockStorage), so it is
  // tried first. Any other `Uniform` pointer -- an access chain result
  // reaching one of the block's own fields -- is ordinary memory, in address
  // space 12, the same way a storage buffer's own non-block pointer
  // converts to address space 11 above (see `storageClassToAddressSpace` in
  // `llvm/lib/Target/SPIRV/SPIRVUtils.h`).
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Uniform)
      return std::nullopt;
    if (mlir::Type Handle = convertBufferBlockType(Type, TypeConverter))
      return Handle;
    if (mlir::Type Handle = convertUniformBlockType(Type, TypeConverter))
      return Handle;
    return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                            /*addressSpace=*/12);
  });

  // A push constant pointer is ordinary memory too, in the address space
  // (13) `feme::spirv::PushConstantGlobalVariablePattern`'s global lives in
  // -- LLVM's own `SPIRVPushConstantAccess` pass finds it there and rewrites
  // it (and every use) into the `spirv.PushConstant` handle representation
  // itself, so nothing further is needed on FeMe's side.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::PushConstant)
      return std::nullopt;
    if (!TypeConverter.convertType(Type.getPointeeType()))
      return std::nullopt;
    return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                            /*addressSpace=*/13);
  });

  // A `Workgroup` pointer -- `WorkgroupGlobalVariablePattern`'s own global,
  // or an access chain result reaching into it -- is ordinary memory too, in
  // address space 3: the same convention Clang's own HLSL `groupshared`
  // codegen uses (`LangAS::hlsl_groupshared`), rather than MLIR's own
  // Vulkan-client default of address space 0 (roadmap milestone E13).
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Workgroup)
      return std::nullopt;
    if (!TypeConverter.convertType(Type.getPointeeType()))
      return std::nullopt;
    return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                            /*addressSpace=*/3);
  });

  // A `TaskPayloadWorkgroupEXT` pointer --
  // `TaskPayloadGlobalVariablePattern`'s own global, or an access chain
  // result reaching into it -- is ordinary memory too, in address space 14:
  // a FeMe-only convention, since LLVM's own SPIRV backend
  // (`storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`)
  // has no mapping at all for this storage class (SPIR-V enum 5402,
  // roadmap H6h).
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() !=
        mlir::spirv::StorageClass::TaskPayloadWorkgroupEXT)
      return std::nullopt;
    if (!TypeConverter.convertType(Type.getPointeeType()))
      return std::nullopt;
    return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                            /*addressSpace=*/14);
  });

  // MLIR's own runtime array conversion refuses one with an `ArrayStride`
  // decoration (see `convertRuntimeArrayType` in MLIR's `SPIRVToLLVM.cpp`),
  // which every runtime array nested in a real (Vulkan-valid) storage
  // buffer block carries -- the stride is otherwise unused here, since the
  // resulting `!llvm.array<0 x T>`'s layout comes from `T` itself.
  TypeConverter.addConversion(
      [&TypeConverter](
          mlir::spirv::RuntimeArrayType Type) -> std::optional<mlir::Type> {
        mlir::Type ElementType =
            TypeConverter.convertType(Type.getElementType());
        if (!ElementType)
          return std::nullopt;
        return mlir::LLVM::LLVMArrayType::get(ElementType, 0);
      });

  // Supersedes MLIR's own `spirv::ArrayType` conversion (see
  // `convertArrayTypeIgnoringDecorations`'s comment for why: an array of an
  // identified struct is otherwise always spuriously rejected, regardless
  // of whether its declared stride is actually representable). Returning
  // null (not an array this can lay out) falls through to MLIR's own
  // conversion, which will also fail identically, so no real coverage is
  // lost by preferring this one.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::ArrayType Type)
                                  -> std::optional<mlir::Type> {
    if (mlir::Type Converted =
            convertArrayTypeIgnoringDecorations(Type, TypeConverter))
      return Converted;
    return std::nullopt;
  });

  // MLIR upstream has no `spirv.MatrixType` conversion at all -- SPIR-V's
  // OpTypeMatrix has no runner equivalent to convert to. LLVM's SPIRV
  // backend expects the natural (column-major) representation, an array of
  // column vectors (see
  // `llvm/test/CodeGen/SPIRV/pointers/load-store-matrix-in-struct.ll`); a
  // `RowMajor`-decorated member or a `MatrixStride` mismatched with that
  // natural layout is rejected once the matrix appears inside a struct (see
  // isMatrixMemberLayoutRepresentable), since that information -- like
  // `Offset` -- is only ever attached to a *member*, not to the matrix type
  // itself.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::MatrixType Type)
                                  -> std::optional<mlir::Type> {
    mlir::Type ColumnType = TypeConverter.convertType(Type.getColumnType());
    if (!ColumnType)
      return std::nullopt;
    return mlir::LLVM::LLVMArrayType::get(ColumnType, Type.getNumColumns());
  });

  // Registered after (so tried before) MLIR's own `spirv.sampled_image`
  // conversion, which folds the image and sampler into one combined target
  // extension type for the SPIR-V runner. LLVM's SPIRV backend sampling
  // intrinsics take separate image and sampler handle arguments instead
  // (see ImageSampleImplicitLodPattern), so the two handles just need a
  // vehicle to travel together through the dialect conversion until then;
  // a two-element struct is the simplest one.
  TypeConverter.addConversion(
      [&TypeConverter](
          mlir::spirv::SampledImageType Type) -> std::optional<mlir::Type> {
        mlir::Type ImageHandle = TypeConverter.convertType(Type.getImageType());
        if (!ImageHandle)
          return std::nullopt;
        mlir::Type SamplerHandle = mlir::LLVM::LLVMTargetExtType::get(
            Type.getContext(), "spirv.Sampler", /*typeParams=*/{},
            /*intParams=*/{});
        return mlir::LLVM::LLVMStructType::getLiteral(
            Type.getContext(), {ImageHandle, SamplerHandle});
      });

  // (Roadmap H7y) A scalar/vector `Input` pointer's own leaf, reached at
  // the end of a `spirv.AccessChain` into an array-typed `Input` variable
  // (StageIOArrayAccessChainPattern), is the one case where the ordinary
  // "an `Input` pointer converts to its pointee's own value" answer (the
  // very first conversion registered above) and the real value that
  // pattern actually produces (a genuine pointer, since the array it
  // indexes into is itself a real pointer, not a value) disagree -- there
  // being no way to tell a genuinely standalone scalar `Input` variable's
  // own address apart from such an access chain's leaf by type alone.
  // Rather than accepting that mismatch (which would otherwise surface as
  // a stray, unresolved `builtin.unrealized_conversion_cast` wherever the
  // mismatched value is consumed), this materialization resolves it
  // directly: whenever the framework needs a value of the "expected"
  // (eagerly-loaded) type but the only value on hand is a real pointer in
  // the `Input` address space (7), it reads through that pointer with an
  // ordinary `llvm.load` -- exactly the value a genuine eager load would
  // have produced at that same site to begin with.
  TypeConverter.addTargetMaterialization(
      [](mlir::OpBuilder &Builder, mlir::Type ResultType,
         mlir::ValueRange Inputs, mlir::Location Loc) -> mlir::Value {
        if (Inputs.size() != 1)
          return nullptr;
        auto PointerTy =
            mlir::dyn_cast<mlir::LLVM::LLVMPointerType>(Inputs[0].getType());
        if (!PointerTy || PointerTy.getAddressSpace() != 7)
          return nullptr;
        return mlir::LLVM::LoadOp::create(Builder, Loc, ResultType,
                                          Inputs[0]);
      });
}

feme::spirv::ResourceInfoMap
feme::spirv::prepareResourceVariables(mlir::spirv::ModuleOp Module) {
  ResourceInfoMap Resources;
  mlir::SymbolTable Table(Module);
  mlir::OpBuilder Builder(Module.getContext());
  Builder.setInsertionPointToStart(Module.getBody());

  // Some producers (observed from DXC for `ResourceDescriptorHeap`/
  // `SamplerDescriptorHeap` bindless-heap accesses) emit more than one
  // `spirv.GlobalVariableOp` for what is really the same descriptor-heap
  // binding: every such duplicate shares the same `OpName` (so MLIR's own
  // SPIR-V deserializer can hand back more than one op with an identical,
  // non-unique `sym_name`) and the same `(DescriptorSet, Binding)` pair.
  // Track the name-global already created for each `(Set, Binding)` pair so
  // a second occurrence reuses it instead of trying to define another LLVM
  // global with the same (now colliding) name -- `Table` only reflects the
  // SPIR-V module's symbols as of its construction above, so it cannot see
  // the LLVM globals this loop itself creates.
  llvm::DenseMap<std::pair<uint32_t, uint32_t>, std::string> HeapNameGlobals;

  for (auto Global : Module.getOps<mlir::spirv::GlobalVariableOp>()) {
    auto PointerType =
        mlir::dyn_cast<mlir::spirv::PointerType>(Global.getType());
    if (!PointerType)
      continue;
    uint32_t Count = 1;
    if (!isResourcePointer(PointerType) && !isBufferBlockPointer(PointerType) &&
        !isUniformBlockPointer(PointerType)) {
      std::optional<uint32_t> ArrayedCount = getArrayedBlockCount(PointerType);
      if (!ArrayedCount)
        ArrayedCount = getArrayedResourceCount(PointerType);
      if (!ArrayedCount)
        continue;
      Count = *ArrayedCount;
    }
    std::optional<uint32_t> Set = Global.getDescriptorSet();
    std::optional<uint32_t> Binding = Global.getBinding();
    if (!Set || !Binding)
      continue;

    llvm::StringRef SymName = Global.getSymName();

    auto HeapKey = std::make_pair(*Set, *Binding);
    auto HeapIt = HeapNameGlobals.find(HeapKey);
    if (HeapIt != HeapNameGlobals.end()) {
      // A duplicate declaration of the same descriptor-heap binding: reuse
      // the name-global already created for it instead of defining another
      // one under a colliding name.
      Resources[SymName] = {*Set, *Binding, HeapIt->second, Count};
      continue;
    }

    std::string NameSymbol = (SymName + ".str").str();
    for (unsigned Suffix = 0; Table.lookup(NameSymbol); ++Suffix)
      NameSymbol = (SymName + ".str." + llvm::Twine(Suffix)).str();

    // The backend reads the name through the pointer it is handed, so it has
    // to be NUL terminated the way C strings are.
    std::string Contents = (SymName + llvm::Twine('\0')).str();
    mlir::LLVM::GlobalOp::create(
        Builder, Global.getLoc(),
        mlir::LLVM::LLVMArrayType::get(Builder.getI8Type(), Contents.size()),
        /*isConstant=*/true, mlir::LLVM::Linkage::Private, NameSymbol,
        Builder.getStringAttr(Contents));
    HeapNameGlobals[HeapKey] = NameSymbol;
    Resources[SymName] = {*Set, *Binding, NameSymbol, Count};
  }
  return Resources;
}

feme::spirv::StageIOInfoMap
feme::spirv::prepareStageIOVariables(mlir::spirv::ModuleOp Module) {
  StageIOInfoMap StageIOVariables;
  for (auto Global : Module.getOps<mlir::spirv::GlobalVariableOp>())
    if (std::optional<unsigned> AddrSpace = getStageIOAddressSpace(Global))
      StageIOVariables[Global.getSymName()] = *AddrSpace;
  return StageIOVariables;
}

feme::spirv::SpecConstantValueMap
feme::spirv::prepareSpecConstants(mlir::spirv::ModuleOp Module) {
  SpecConstantValueMap Values;
  for (mlir::Operation &Op : Module.getBody()->getOperations()) {
    if (auto ScalarConst = mlir::dyn_cast<mlir::spirv::SpecConstantOp>(Op)) {
      Values[ScalarConst.getSymName()] = ScalarConst.getDefaultValue();
      continue;
    }

    auto CompositeConst =
        mlir::dyn_cast<mlir::spirv::SpecConstantCompositeOp>(Op);
    if (!CompositeConst)
      continue;

    // Only a vector-shaped composite (e.g. `gl_WorkGroupSize`'s own
    // `vector<3xi32>` `LocalSizeId` composite, the shape any known real
    // HLSL/CTS source this ICD's frontend surface reaches actually needs)
    // is resolved here; a `spirv.struct`/nested-`spirv.array`-shaped
    // composite is left unresolved (simply omitted from the map, rather
    // than approximated) -- `ReferenceOfConversionPattern` below declines
    // any reference this omission leaves unresolved, with its own precise
    // diagnostic.
    auto VecType = mlir::dyn_cast<mlir::VectorType>(CompositeConst.getType());
    if (!VecType)
      continue;

    llvm::SmallVector<mlir::Attribute> Elements;
    Elements.reserve(CompositeConst.getConstituents().size());
    bool AllResolved = true;
    for (mlir::Attribute Constituent : CompositeConst.getConstituents()) {
      mlir::TypedAttr ElementAttr;
      if (auto SymRef = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(Constituent)) {
        // A reference to another (already-declared, per SPIR-V's own
        // declare-before-use symbol rule) specialization constant.
        auto It = Values.find(SymRef.getValue());
        if (It == Values.end()) {
          AllResolved = false;
          break;
        }
        ElementAttr = It->second;
      } else {
        // An inline (non-specialization) constant constituent.
        ElementAttr = mlir::dyn_cast<mlir::TypedAttr>(Constituent);
      }
      if (!ElementAttr || ElementAttr.getType() != VecType.getElementType()) {
        AllResolved = false;
        break;
      }
      Elements.push_back(ElementAttr);
    }
    if (!AllResolved ||
        static_cast<int64_t>(Elements.size()) != VecType.getNumElements())
      continue;

    Values[CompositeConst.getSymName()] = mlir::cast<mlir::TypedAttr>(
        mlir::DenseElementsAttr::get(VecType, Elements));
  }
  return Values;
}

void feme::spirv::inlineSpecConstantOperations(mlir::spirv::ModuleOp Module) {
  // Collect first, then rewrite: `SpecConstantOperationOp::verifyRegions`
  // guarantees every operand referenced by the wrapped op is defined by a
  // `spirv.Constant`/`spirv.mlir.referenceof`/another
  // `spirv.SpecConstantOperation` that dominates it in the ordinary SSA
  // sense (a sibling earlier in the same block, never nested inside this
  // op's own region), so `replaceAllUsesWith` below updates every use of
  // a since-inlined op's result module-wide regardless of which order
  // this list is processed in -- no dependency ordering is required.
  llvm::SmallVector<mlir::spirv::SpecConstantOperationOp> Ops;
  Module.walk(
      [&](mlir::spirv::SpecConstantOperationOp Op) { Ops.push_back(Op); });
  for (mlir::spirv::SpecConstantOperationOp Op : Ops) {
    mlir::Block &Body = Op.getBody().front();
    mlir::Operation *Inner = &Body.front();
    Inner->moveBefore(Op);
    Op.replaceAllUsesWith(Inner->getResult(0));
    Op.erase();
  }
}

void feme::spirv::populateSPIRVToLLVMTargetPatterns(
    const mlir::LLVMTypeConverter &TypeConverter,
    mlir::RewritePatternSet &Patterns, const ResourceInfoMap &Resources,
    const StageIOInfoMap &StageIOVariables,
    const SpecConstantValueMap &SpecConstants,
    const FloatControlInfoMap &RoundingModeRTZWidths,
    const FloatControlInfoMap &DenormFlushToZeroWidths,
    const FastMathDefaultMap &FastMathDefaults) {
  Patterns.add<
      AggregateInitializedVariablePattern,
      ArrayConstantPattern, AssumeTrueConversionPattern,
      AtomicCompareExchangePattern,
      AtomicRMWPattern<mlir::spirv::AtomicIAddOp, mlir::LLVM::AtomicBinOp::add>,
      AtomicRMWPattern<mlir::spirv::AtomicISubOp, mlir::LLVM::AtomicBinOp::sub>,
      AtomicRMWPattern<mlir::spirv::AtomicAndOp, mlir::LLVM::AtomicBinOp::_and>,
      AtomicRMWPattern<mlir::spirv::AtomicOrOp, mlir::LLVM::AtomicBinOp::_or>,
      AtomicRMWPattern<mlir::spirv::AtomicXorOp, mlir::LLVM::AtomicBinOp::_xor>,
      AtomicRMWPattern<mlir::spirv::AtomicSMaxOp, mlir::LLVM::AtomicBinOp::max>,
      AtomicRMWPattern<mlir::spirv::AtomicSMinOp, mlir::LLVM::AtomicBinOp::min>,
      AtomicRMWPattern<mlir::spirv::AtomicUMaxOp,
                       mlir::LLVM::AtomicBinOp::umax>,
      AtomicRMWPattern<mlir::spirv::AtomicUMinOp,
                       mlir::LLVM::AtomicBinOp::umin>,
      AtomicRMWPattern<mlir::spirv::AtomicExchangeOp,
                       mlir::LLVM::AtomicBinOp::xchg>,
      BitFieldInsertPattern, BitFieldSExtractPattern, BitFieldUExtractPattern,
      BranchConditionalPattern, BuiltInAddressOfPattern,
      BuiltInAccessChainPattern, BuiltInGlobalVariablePattern,
      BlockAccessChainPattern, CompositeConstructPattern,
      ControlBarrierConversionPattern, MemoryBarrierConversionPattern,
      CopyObjectConversionPattern,
      DemoteToHelperInvocationConversionPattern, DotConversionPattern,
      ElectConversionPattern, AllEqualConversionPattern,
      VoteConversionPattern<mlir::spirv::GroupNonUniformAllOp>,
      VoteConversionPattern<mlir::spirv::GroupNonUniformAnyOp>,
      BroadcastConversionPattern, BroadcastFirstConversionPattern,
      ShuffleConversionPattern, ShuffleXorConversionPattern,
      BallotConversionPattern, InverseBallotConversionPattern,
      BallotBitExtractConversionPattern, BallotBitCountConversionPattern,
      BallotFindLSBConversionPattern, BallotFindMSBConversionPattern,
      EmitVertexConversionPattern, EndPrimitiveConversionPattern,
      ExecutionModePattern, ExecutionModeIdPattern, ExpectConversionPattern,
      ImageDrefGatherPattern, ImageFetchPattern, ImageFetchLodPattern,
      ImageGatherPattern, ImagePattern, ImageQueryLodPattern, ImageSampleDrefExplicitLodPattern,
      ImageSampleDrefGradPattern, ImageSampleDrefImplicitLodPattern,
      ImageSampleExplicitLodPattern, ImageSampleGradPattern,
      ImageSampleImplicitLodPattern, ImageQuerySizePattern, ImageReadPattern,
      ImageTexelPointerPattern, ImageWritePattern, KillConversionPattern,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformIAddOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformFAddOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformIMulOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformFMulOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformSMinOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformFMinOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformUMinOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformSMaxOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformFMaxOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformUMaxOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformBitwiseAndOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformBitwiseOrOp>,
      GroupNonUniformReducePattern<mlir::spirv::GroupNonUniformBitwiseXorOp>,
      GroupNonUniformScanPattern<mlir::spirv::GroupNonUniformIAddOp>,
      GroupNonUniformScanPattern<mlir::spirv::GroupNonUniformFAddOp>,
      GroupNonUniformScanPattern<mlir::spirv::GroupNonUniformIMulOp>,
      GroupNonUniformScanPattern<mlir::spirv::GroupNonUniformFMulOp>,
      LoadValuePattern, MatrixCompositeExtractPattern,
      MatrixCompositeInsertPattern, MatrixTimesVectorPattern,
      VectorTimesMatrixPattern, MatrixTimesMatrixPattern,
      MatrixTimesScalarPattern, TransposePattern, RowMajorMatrixStorePattern,
      RowMajorMatrixLoadPattern, MatrixColumnLoadPattern,
      MatrixColumnStorePattern, OffsetStructMemberReorderAccessChainPattern,
      PushConstantGlobalVariablePattern, RotateConversionPattern,
      SampledImagePattern, SDotConversionPattern, UDotConversionPattern,
      SUDotConversionPattern, SDotAccSatConversionPattern,
      UDotAccSatConversionPattern, SUDotAccSatConversionPattern,
      SetMeshOutputsEXTConversionPattern, EmitMeshTasksEXTConversionPattern,
      SpecConstantErasurePattern, SpecConstantCompositeErasurePattern,
      StageIOGlobalVariablePattern,
      SwitchConversionPattern, TaskPayloadGlobalVariablePattern,
      TerminateInvocationConversionPattern, WorkgroupGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit);
  Patterns.add<ArrayedBlockAccessChainPattern, ResourceArrayAccessChainPattern,
               ResourceAddressOfPattern, ResourceGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit, Resources);
  // Higher benefit than the `FeMeBenefit`-registered `ImageReadPattern`
  // above, so this wins for a `Dim::SubpassData` image read (roadmap F8a);
  // `ImageReadPattern` still handles every other dimension.
  Patterns.add<SubpassLoadPattern>(Patterns.getContext(), TypeConverter,
                                   FeMeBenefit + 1);
  Patterns.add<StageIOAddressOfPattern>(Patterns.getContext(), TypeConverter,
                                        FeMeBenefit, StageIOVariables);
  Patterns.add<ReferenceOfConversionPattern>(Patterns.getContext(),
                                             TypeConverter, FeMeBenefit,
                                             SpecConstants);
  Patterns.add<StageIOArrayAccessChainPattern>(Patterns.getContext(),
                                               TypeConverter, FeMeBenefit);
  Patterns.add<
      FloatControlArithmeticPattern<mlir::spirv::FAddOp, mlir::LLVM::FAddOp,
                                    mlir::LLVM::ConstrainedFAddIntr>,
      FloatControlArithmeticPattern<mlir::spirv::FSubOp, mlir::LLVM::FSubOp,
                                    mlir::LLVM::ConstrainedFSubIntr>,
      FloatControlArithmeticPattern<mlir::spirv::FMulOp, mlir::LLVM::FMulOp,
                                    mlir::LLVM::ConstrainedFMulIntr>,
      FloatControlArithmeticPattern<mlir::spirv::FDivOp, mlir::LLVM::FDivOp,
                                    mlir::LLVM::ConstrainedFDivIntr>,
      FloatControlArithmeticPattern<mlir::spirv::FRemOp, mlir::LLVM::FRemOp,
                                    mlir::LLVM::ConstrainedFRemIntr>>(
      Patterns.getContext(), TypeConverter, FeMeBenefit, RoundingModeRTZWidths,
      DenormFlushToZeroWidths, FastMathDefaults);
  // Overrides upstream's own unconditional `DirectConversionPattern`/
  // `InverseSqrtPattern`/`ScalePattern` for these six GLSL.std.450 ops with
  // an unconditional subnormal-input flush (roadmap H6m), modeling a real
  // GPU's own special-function-unit hardware behavior; see
  // `TranscendentalFlushInputPattern`'s own comment above for why only
  // these ops (and not every GLSL.std.450 op) need it.
  Patterns.add<
      TranscendentalFlushInputPattern<mlir::spirv::GLLogOp, mlir::LLVM::LogOp>,
      TranscendentalFlushInputPattern<mlir::spirv::GLLog2Op,
                                      mlir::LLVM::Log2Op>,
      TranscendentalFlushInputPattern<mlir::spirv::GLSqrtOp,
                                      mlir::LLVM::SqrtOp>,
      TranscendentalFlushInputPattern<mlir::spirv::GLSinhOp,
                                      mlir::LLVM::SinhOp>>(
      Patterns.getContext(), TypeConverter, FeMeBenefit);
  Patterns.add<FlushedInverseSqrtPattern>(Patterns.getContext(), TypeConverter,
                                          FeMeBenefit);
  // pi / 180
  Patterns.add<FlushedScalePattern<mlir::spirv::GLRadiansOp>>(
      0.017453292519943295, Patterns.getContext(), TypeConverter,
      FeMeBenefit);
  // 180 / pi
  Patterns.add<FlushedScalePattern<mlir::spirv::GLDegreesOp>>(
      57.29577951308232, Patterns.getContext(), TypeConverter, FeMeBenefit);
  // Roadmap L7c: `spirv.GL.Atan2`/`Step`/`SmoothStep` had no conversion
  // pattern at all before this fix (neither here nor in upstream's own
  // `populateSPIRVToLLVMConversionPatterns`, confirmed by grepping both),
  // failing every one of these HLSL-derived shapes' pipeline creation with
  // "failed to legalize operation ... that was explicitly marked illegal".
  Patterns.add<GLAtan2Pattern, GLStepPattern, GLSmoothStepPattern,
               GLFaceForwardPattern, GLRefractPattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit);
  // Roadmap H124f: `spirv.IsNan`/`spirv.IsInf`/`spirv.GL.Normalize`/
  // `spirv.GL.Length` had no conversion pattern at all before this fix
  // (neither here nor in upstream's own
  // `populateSPIRVToLLVMConversionPatterns`, confirmed by grepping both),
  // failing every one of these HLSL-derived shapes' pipeline creation
  // with "failed to legalize operation ... that was explicitly marked
  // illegal".
  Patterns.add<IsNanPattern, IsInfPattern, GLLengthPattern, GLNormalizePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit);
}

