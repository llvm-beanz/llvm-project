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
    return true;
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
    if (!Mapping)
      return Rewriter.notifyMatchFailure(
          Op, "not a builtin variable with an LLVM equivalent");

    auto PointerTy = mlir::cast<mlir::spirv::PointerType>(Op.getType());
    mlir::Type ResultType =
        getTypeConverter()->convertType(PointerTy.getPointeeType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
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
    if (!getBuiltInMapping(Op))
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
/// `BuiltIn`/`Location`/`Component`/`Index` and boolean interpolation/
/// per-primitive/per-patch attributes (`StageIOFlagDecorations`), or a null
/// attribute if it carries none of them.
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
/// interface block's own member can carry (i.e. not `Offset`/`MatrixStride`/
/// `ColMajor`/`RowMajor`, an ordinary UBO/SSBO struct's own layout
/// decorations, which a stage-IO struct never carries in practice, but
/// filtered defensively all the same).
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
    return Builder.getArrayAttr(
        {Builder.getI32IntegerAttr(static_cast<int32_t>(Info.decoration))});
  default:
    return nullptr;
  }
}

/// Builds the getStageIOMemberDecorationsAttrName() attribute for \p Struct
/// -- a builtin interface block's own field struct (e.g. `gl_PerVertex`'s
/// `{Position, PointSize, ClipDistance, CullDistance}`) -- from its members'
/// own `OpMemberDecorate`d decorations
/// (mlir::spirv::StructType::getMemberDecorations, already used by
/// isBufferBlockWritable above for a storage-buffer block's `NonWritable`
/// member decoration), or a null attribute if no member carries a
/// recognized one (roadmap H2c: SPIR-V decorates a `BuiltIn` interface
/// block's members individually rather than the block variable itself, so
/// buildStageIODecorationsAttr's whole-variable read never sees them).
/// Each entry is `(memberIndex, tuples)`, where `tuples` is an `ArrayAttr`
/// of buildMemberDecorationTuple's own per-decoration shape.
mlir::ArrayAttr buildMemberDecorationsAttr(mlir::spirv::StructType Struct) {
  mlir::Builder Builder(Struct.getContext());
  llvm::SmallVector<mlir::Attribute> Members;
  for (unsigned Index = 0, End = Struct.getNumElements(); Index != End;
       ++Index) {
    llvm::SmallVector<mlir::spirv::StructType::MemberDecorationInfo, 2>
        Decorations;
    Struct.getMemberDecorations(Index, Decorations);

    llvm::SmallVector<mlir::Attribute> Tuples;
    for (const auto &Decoration : Decorations)
      if (mlir::Attribute Tuple =
              buildMemberDecorationTuple(Builder, Decoration))
        Tuples.push_back(Tuple);
    if (Tuples.empty())
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
  if (getBuiltInMapping(Op))
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

    // (Roadmap H7y) An array-typed `Input` variable stays a real pointer
    // instead of an eagerly-loaded value -- see this class's own comment.
    if (mlir::isa<mlir::LLVM::LLVMArrayType>(ValueType)) {
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
/// pointer to an array (see StageIOAddressOfPattern's own comment for why
/// such a variable stays a real pointer, and StageIOArrayAccessChainPattern
/// below for what this identifies it for).
bool isInputArrayAccessChain(mlir::spirv::AccessChainOp Op) {
  auto BaseType =
      mlir::dyn_cast<mlir::spirv::PointerType>(Op.getBasePtr().getType());
  return BaseType &&
         BaseType.getStorageClass() == mlir::spirv::StorageClass::Input &&
         mlir::isa<mlir::spirv::ArrayType>(BaseType.getPointeeType());
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
/// An array-of-blocks variable's own address is simply erased instead: its
/// handle needs which descriptor to bind, only known at its own access
/// chain's leading (array) index -- see ArrayedBlockAccessChainPattern,
/// which builds that handle itself and is the only legal use of such a
/// variable's address (a whole descriptor array is never itself loaded
/// or stored as a value).
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
    if (It->second.Count > 1) {
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
mlir::LogicalResult rewriteBlockAccess(
    mlir::spirv::AccessChainOp Op, mlir::ConversionPatternRewriter &Rewriter,
    const mlir::TypeConverter &TypeConverter, const BlockElement &Element,
    mlir::Value Handle, mlir::ValueRange AllIndices, unsigned Selector) {
  mlir::Type ResultType =
      TypeConverter.convertType(Op.getComponentPtr().getType());
  if (!ResultType)
    return Rewriter.notifyMatchFailure(Op, "type conversion failed");

  mlir::Location Loc = Op.getLoc();
  mlir::Value ElementPtr =
      createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.getpointer",
                          ResultType, {Handle, AllIndices[Selector]});
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
  }
  mlir::Type ElementType = TypeConverter.convertType(SelectedType);
  if (!ElementType)
    return Rewriter.notifyMatchFailure(Op, "type conversion failed");

  llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
  GEPIndices.push_back(0);
  llvm::append_range(GEPIndices, AllIndices.drop_front(Selector + 1));
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

    return rewriteBlockAccess(Op, Rewriter, *getTypeConverter(), *Element,
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
    if (It == Resources.end() || It->second.Count <= 1)
      return Rewriter.notifyMatchFailure(Op, "not an arrayed block");

    auto PointerType = mlir::cast<mlir::spirv::PointerType>(AddrOf.getType());
    auto Array =
        mlir::cast<mlir::spirv::ArrayType>(PointerType.getPointeeType());
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

    return rewriteBlockAccess(Op, Rewriter, *getTypeConverter(), *Element,
                              Handle, Indices, Selector);
  }

private:
  const feme::spirv::ResourceInfoMap &Resources;
};

/// Returns false if \p Struct's member \p Index is a matrix decorated
/// `RowMajor` -- a physical layout transposed from the logical column-major
/// type LLVM's own natural array-of-column-vectors representation always
/// uses (see the `spirv.MatrixType` conversion in
/// populateSPIRVToLLVMTargetTypeConversions), which reinterpreting the same
/// bytes cannot reproduce -- or decorated `MatrixStride` with a value other
/// than \p ConvertedMember's own natural per-column stride (the size of one
/// column, since LLVM array elements pack with no interior padding); true
/// for every other member, including one that is not a matrix at all.
bool isMatrixMemberLayoutRepresentable(mlir::spirv::StructType Struct,
                                       unsigned Index,
                                       mlir::Type ConvertedMember) {
  if (!mlir::isa<mlir::spirv::MatrixType>(Struct.getElementType(Index)))
    return true;

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
/// rather than the shared upstream one. Returns null for a struct this
/// cannot lay out (a declared offset naturally-aligned layout cannot
/// reproduce, a matrix member's declared layout is not representable --
/// see isMatrixMemberLayoutRepresentable -- or an unconvertible member
/// type).
mlir::Type convertOffsetStructTypeIgnoringDecorations(
    mlir::spirv::StructType Type, const mlir::TypeConverter &Converter) {
  llvm::SmallVector<mlir::Type, 8> Members;
  for (unsigned I = 0, E = Type.getNumElements(); I != E; ++I) {
    mlir::Type MemberTy = Converter.convertType(Type.getElementType(I));
    if (!MemberTy || !isMatrixMemberLayoutRepresentable(Type, I, MemberTy))
      return nullptr;
    Members.push_back(MemberTy);
  }
  if (!Type.hasOffset())
    return mlir::LLVM::LLVMStructType::getLiteral(Type.getContext(), Members,
                                                  /*isPacked=*/false);

  mlir::DataLayout DL;
  uint64_t Cursor = 0;
  for (unsigned I = 0, E = Members.size(); I != E; ++I) {
    Cursor = llvm::alignTo(Cursor, DL.getTypeABIAlignment(Members[I]));
    if (Cursor != Type.getMemberOffset(I))
      return nullptr; // Declared offset doesn't match the natural layout.
    Cursor += DL.getTypeSize(Members[I]);
  }
  return mlir::LLVM::LLVMStructType::getLiteral(Type.getContext(), Members,
                                                /*isPacked=*/false);
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

/// SPIR-V 1.6's `Nontemporal` image-operand bit is a cache hint with no
/// correctness effect (see the SPIR-V spec's Image Operands table); this
/// converter has no caching model to honor it with, so every pattern below
/// accepts and discards it rather than rejecting it like an unmodeled
/// modifier or threading it through as if it changed the access performed.
constexpr mlir::spirv::ImageOperands NontemporalBit =
    mlir::spirv::ImageOperands::Nontemporal;

/// Returns true if \p ImageOperands names any actual modifier (e.g. `Lod`,
/// `Bias`) rather than being absent, the empty `None` bit-enum value, or the
/// discarded `Nontemporal` cache hint -- real `dxc`-compiled SPIR-V spells
/// "no modifiers" as an explicit `#spirv.image_operands<None>` attribute
/// rather than omitting the (optional) attribute entirely, so a presence
/// check alone rejects every image access real SPIR-V input produces.
bool hasImageOperands(std::optional<mlir::spirv::ImageOperands> ImageOperands) {
  if (!ImageOperands)
    return false;
  return mlir::spirv::bitEnumClear(*ImageOperands, NontemporalBit) !=
         mlir::spirv::ImageOperands::None;
}

/// Returns true if \p ImageOperands is exactly \p Required, optionally
/// combined with the discarded `Nontemporal` cache hint (see above).
bool hasExactImageOperands(
    std::optional<mlir::spirv::ImageOperands> ImageOperands,
    mlir::spirv::ImageOperands Required) {
  if (!ImageOperands)
    return false;
  return mlir::spirv::bitEnumClear(*ImageOperands, NontemporalBit) == Required;
}

/// Converts `spirv.ImageRead` or `spirv.ImageFetch` into a load through the
/// read location. The two ops are otherwise handled identically here: LLVM's
/// SPIRV backend picks `OpImageRead` vs `OpImageFetch` itself, from whether
/// the handle's underlying image type has its `Sampled` operand set to 1
/// (`spirv.ImageFetch`'s only legal operand, per its verifier) rather than
/// from which intrinsic produced the load -- see `generateImageReadOrFetch`
/// in `llvm/lib/Target/SPIRV/SPIRVInstructionSelector.cpp`.
template <typename ImageOpTy>
class ImageLoadPattern : public mlir::SPIRVToLLVMConversion<ImageOpTy> {
public:
  using mlir::SPIRVToLLVMConversion<ImageOpTy>::SPIRVToLLVMConversion;
  using OpAdaptor = typename mlir::SPIRVToLLVMConversion<ImageOpTy>::OpAdaptor;

  mlir::LogicalResult
  matchAndRewrite(ImageOpTy Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (hasImageOperands(Op.getImageOperands()))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    mlir::Type ResultType = this->getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Value Pointer = createResourcePointer(
        Rewriter, Op.getLoc(), Adaptor.getImage(), Adaptor.getCoordinate());
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

/// Converts a `spirv.ImageFetch` with the `Lod` image operand (optionally
/// combined with the discarded `Nontemporal` bit, see `hasImageOperands`
/// above) into the `llvm.spv.resource.load.level` intrinsic call, mirroring
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
    if (!hasExactImageOperands(Op.getImageOperands(),
                               mlir::spirv::ImageOperands::Lod) ||
        Adaptor.getOperandArguments().size() != 1)
      return Rewriter.notifyMatchFailure(
          Op, "only a lone Lod image operand is supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Coordinate = Adaptor.getCoordinate();
    mlir::Value Lod = Adaptor.getOperandArguments()[0];

    // `llvm.spv.resource.load.level` always takes a texel offset, unlike
    // `spirv.ImageFetch`, which has none here (the `Lod`-only match above
    // already ruled out a `ConstOffset`/`Offset` modifier); pass zero.
    auto CoordVecTy = mlir::dyn_cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        CoordVecTy ? mlir::cast<mlir::Type>(mlir::VectorType::get(
                         CoordVecTy.getShape(), Rewriter.getI32Type()))
                   : mlir::cast<mlir::Type>(Rewriter.getI32Type());
    mlir::Value Offset = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, OffsetType, Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.load.level",
                                ResultType,
                                {Adaptor.getImage(), Coordinate, Lod, Offset}));
    return mlir::success();
  }
};

/// Converts `spirv.ImageWrite` into a store through the written location.
class ImageWritePattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageWriteOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageWriteOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageWriteOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (hasImageOperands(Op.getImageOperands()))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    mlir::Value Pointer = createResourcePointer(
        Rewriter, Op.getLoc(), Adaptor.getImage(), Adaptor.getCoordinate());
    Rewriter.replaceOpWithNewOp<mlir::LLVM::StoreOp>(Op, Adaptor.getTexel(),
                                                     Pointer);
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

/// Converts a `spirv.ImageSampleImplicitLod` with no modifiers into the
/// `llvm.spv.resource.sample` intrinsic call LLVM's SPIRV backend selects
/// `OpSampledImage`+`OpImageSampleImplicitLod` from -- see
/// `llvm/test/CodeGen/SPIRV/hlsl-resources/Sample.ll`. Bias/gradient/LOD-
/// clamped/comparison/gather variants (which need additional operands this
/// pattern does not supply) are not yet covered -- see the "Known gap" note
/// in the SPIR-V section of feme/docs/Design.md.
class ImageSampleImplicitLodPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleImplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleImplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleImplicitLodOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (hasImageOperands(Op.getImageOperands()))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

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
    auto CoordVecTy = mlir::dyn_cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        CoordVecTy ? mlir::cast<mlir::Type>(mlir::VectorType::get(
                         CoordVecTy.getShape(), Rewriter.getI32Type()))
                   : mlir::cast<mlir::Type>(Rewriter.getI32Type());
    mlir::Value Offset = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, OffsetType, Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op,
        createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.sample",
                            ResultType, {Image, Sampler, Coordinate, Offset}));
    return mlir::success();
  }
};
/// Converts a `spirv.ImageSampleExplicitLod` with the `Lod` image operand
/// (not `Grad`, and not combined with any other modifier besides the
/// discarded `Nontemporal` bit, see `hasImageOperands` above) into the
/// `llvm.spv.resource.samplelevel` intrinsic call, mirroring
/// `ImageSampleImplicitLodPattern` above but threading the explicit LOD
/// operand through instead of defaulting it (see roadmap R30, "SPIR-V
/// (including Design.md's §1.2 sampling variants)"). A `Grad` (gradient)
/// operand -- `ImageSampleExplicitLod`'s other legal modifier -- is not yet
/// covered, since it needs a `llvm.spv.resource.samplegrad` call with two
/// additional operands this pattern does not build.
class ImageSampleExplicitLodPattern
    : public mlir::SPIRVToLLVMConversion<
          mlir::spirv::ImageSampleExplicitLodOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageSampleExplicitLodOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageSampleExplicitLodOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!hasExactImageOperands(Op.getImageOperands(),
                               mlir::spirv::ImageOperands::Lod) ||
        Adaptor.getOperandArguments().size() != 1)
      return Rewriter.notifyMatchFailure(
          Op, "only a lone Lod image operand is supported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value SampledImage = Adaptor.getSampledImage();
    mlir::Value Image = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{0});
    mlir::Value Sampler = mlir::LLVM::ExtractValueOp::create(
        Rewriter, Loc, SampledImage, llvm::ArrayRef<int64_t>{1});
    mlir::Value Lod = Adaptor.getOperandArguments()[0];

    mlir::Value Coordinate = Adaptor.getCoordinate();
    auto CoordVecTy = mlir::dyn_cast<mlir::VectorType>(Coordinate.getType());
    mlir::Type OffsetType =
        CoordVecTy ? mlir::cast<mlir::Type>(mlir::VectorType::get(
                         CoordVecTy.getShape(), Rewriter.getI32Type()))
                   : mlir::cast<mlir::Type>(Rewriter.getI32Type());
    mlir::Value Offset = mlir::LLVM::ConstantOp::create(
        Rewriter, Loc, OffsetType, Rewriter.getZeroAttr(OffsetType));

    Rewriter.replaceOp(
        Op, createIntrinsicCall(Rewriter, Loc, "llvm.spv.resource.samplelevel",
                                ResultType,
                                {Image, Sampler, Coordinate, Lod, Offset}));
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

/// Converts SPIR-V `ConstantOp` with `spirv.array` type -- MLIR's own
/// `ConstantScalarAndVectorPattern` only matches a scalar or vector `spirv.
/// Constant` (see its `srcType` check), leaving an array constant illegal,
/// which is exactly the shape a `const static` HLSL array (e.g. a palette of
/// `float3`s) compiles down to. `llvm.mlir.constant` has no such
/// restriction: it accepts one flat `DenseElementsAttr` for a whole
/// `!llvm.array<... x vector<...>>` so long as its element count and scalar
/// element type match (see `LLVM::ConstantOp::verify`'s `ElementsAttr`
/// case), whatever the array's rank or whether its leaves are vectors or
/// scalars -- so this pattern only has to flatten the SPIR-V constant's
/// (possibly nested) constituents to match, rather than reproduce its
/// nesting as `llvm.mlir.constant`'s alternative, structurally-nested
/// `ArrayAttr` encoding.
class ArrayConstantPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ConstantOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ConstantOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ConstantOp Op, OpAdaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (!mlir::isa<mlir::spirv::ArrayType>(Op.getType()))
      return Rewriter.notifyMatchFailure(Op, "not an array constant");

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

    auto ShapeType = mlir::RankedTensorType::get(
        static_cast<int64_t>(Elements.size()), LeafType);
    auto FlatAttr = mlir::DenseElementsAttr::get(ShapeType, Elements);
    Rewriter.replaceOpWithNewOp<mlir::LLVM::ConstantOp>(Op, DstType, FlatAttr);
    return mlir::success();
  }
};

/// Converts `spirv.CompositeConstruct` building a 1-D vector out of scalar
/// and/or shorter-vector constituents (e.g. HLSL's `float3(x, x, x)`, which
/// SPIR-V spells as a `CompositeConstruct` of three scalar constituents, or
/// a `.xxx` splat's `CompositeConstruct` of the same scalar three times).
/// MLIR has no pattern for this op at all, for any of the composite kinds
/// (vector, array, struct, matrix) it can build; only the vector case is
/// implemented here; it lowers to an `llvm.mlir.poison` seed with one
/// `llvm.insertelement` per resulting lane -- each lane's value either the
/// scalar constituent supplying it directly, or one `llvm.extractelement`
/// out of the vector constituent supplying a contiguous run of lanes, per
/// this op's "contiguous subset of scalars" semantics.
class CompositeConstructPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::CompositeConstructOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::CompositeConstructOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::CompositeConstructOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
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
/// `ExecutionModeIdPattern` above already erased their only reference.
/// A specialization constant genuinely read by the shader body (via
/// `spirv.mlir.referenceof`, not an execution mode) is a distinct,
/// still-unimplemented feature: erasing its declaration here does not
/// paper over that gap, since `spirv.mlir.referenceof` itself has no
/// conversion pattern either and fails legalization on its own, now with
/// a more precise "unresolved symbol" diagnostic instead of a spurious one
/// pointing at the declaration.
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
  // (Roadmap H7y) An *array*-typed `Input` pointer (e.g. `gl_in[]`, a
  // geometry/tessellation entry's own per-vertex input, or the standalone
  // `gl_ClipDistance`/`gl_CullDistance` builtin array) is the one shape this
  // "always a value" answer cannot support at all: LLVM's `extractvalue`
  // can only select a compile-time-constant index out of a value, so a
  // genuinely dynamic (loop-carried) index into such an array has no
  // representation as a value-typed read whatsoever. StageIOAddressOfPattern
  // accordingly keeps an array-typed `Input` variable a real pointer instead
  // (see its own comment) -- this conversion has to answer consistently, or
  // a later pattern that calls back into it (e.g. MLIR's own generic
  // `AccessChainPattern`, which decides whether its base operand is a real
  // pointer by re-converting the *original* SPIR-V pointer type rather than
  // inspecting the already-converted operand's actual type) would compute
  // the wrong shape and build an ill-typed `getelementptr`.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Input &&
        !isResourcePointer(Type))
      return std::nullopt;
    if (Type.getStorageClass() == mlir::spirv::StorageClass::Input &&
        mlir::isa<mlir::spirv::ArrayType>(Type.getPointeeType())) {
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
        continue;
      Count = *ArrayedCount;
    }
    std::optional<uint32_t> Set = Global.getDescriptorSet();
    std::optional<uint32_t> Binding = Global.getBinding();
    if (!Set || !Binding)
      continue;

    llvm::StringRef SymName = Global.getSymName();
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

void feme::spirv::populateSPIRVToLLVMTargetPatterns(
    const mlir::LLVMTypeConverter &TypeConverter,
    mlir::RewritePatternSet &Patterns, const ResourceInfoMap &Resources,
    const StageIOInfoMap &StageIOVariables,
    const FloatControlInfoMap &RoundingModeRTZWidths,
    const FloatControlInfoMap &DenormFlushToZeroWidths,
    const FastMathDefaultMap &FastMathDefaults) {
  Patterns.add<
      ArrayConstantPattern, AssumeTrueConversionPattern,
      BuiltInAddressOfPattern, BuiltInAccessChainPattern,
      BuiltInGlobalVariablePattern, BlockAccessChainPattern,
      CompositeConstructPattern, DemoteToHelperInvocationConversionPattern,
      DotConversionPattern, EmitVertexConversionPattern,
      EndPrimitiveConversionPattern, ExecutionModePattern,
      ExecutionModeIdPattern, ExpectConversionPattern, ImageFetchPattern,
      ImageFetchLodPattern, ImageSampleExplicitLodPattern,
      ImageSampleImplicitLodPattern, ImageQuerySizePattern, ImageReadPattern,
      ImageWritePattern, LoadValuePattern, MatrixCompositeExtractPattern,
      MatrixCompositeInsertPattern, PushConstantGlobalVariablePattern,
      RotateConversionPattern, SampledImagePattern, SDotConversionPattern,
      UDotConversionPattern, SUDotConversionPattern,
      SDotAccSatConversionPattern, UDotAccSatConversionPattern,
      SUDotAccSatConversionPattern, SetMeshOutputsEXTConversionPattern,
      SpecConstantErasurePattern, StageIOGlobalVariablePattern,
      SwitchConversionPattern, TaskPayloadGlobalVariablePattern,
      TerminateInvocationConversionPattern, WorkgroupGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit);
  Patterns.add<ArrayedBlockAccessChainPattern, ResourceAddressOfPattern,
               ResourceGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit, Resources);
  // Higher benefit than the `FeMeBenefit`-registered `ImageReadPattern`
  // above, so this wins for a `Dim::SubpassData` image read (roadmap F8a);
  // `ImageReadPattern` still handles every other dimension.
  Patterns.add<SubpassLoadPattern>(Patterns.getContext(), TypeConverter,
                                   FeMeBenefit + 1);
  Patterns.add<StageIOAddressOfPattern>(Patterns.getContext(), TypeConverter,
                                        FeMeBenefit, StageIOVariables);
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
}

