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
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Transforms/DialectConversion.h"
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
/// -- see BlockElement's own comment for the two shapes covered. A
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
    if (auto Field =
            mlir::dyn_cast<mlir::spirv::StructType>(Struct.getElementType(0)))
      return BlockElement{Field, /*HasWrapper=*/true};
  }
  return BlockElement{Struct, /*HasWrapper=*/false};
}

/// Returns true if \p Type is a pointer to a uniform buffer block -- see
/// getUniformBlockElement.
bool isUniformBlockPointer(mlir::spirv::PointerType Type) {
  return static_cast<bool>(getUniformBlockElement(Type));
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
/// For an `Input` variable, whose pointer converts to its pointee type
/// instead (the same conversion a builtin `Input` variable's pointer uses,
/// there being no way to tell the two apart by type alone), this instead
/// loads the global eagerly right here, producing that pointee-typed value
/// directly: LoadValuePattern then collapses the real `spirv.Load` reading
/// it into the identity, exactly as it already does for a builtin's
/// `llvm.spv.*` intrinsic result.
/// Replaces `spirv.mlir.addressof` of a non-builtin stage-IO variable.
///
/// For an `Output` variable this produces `llvm.mlir.addressof` of the
/// `llvm.mlir.global` StageIOGlobalVariablePattern converts its declaration
/// to, in the matching address space (8) -- see
/// feme::spirv::populateSPIRVToLLVMTargetTypeConversions.
///
/// For an `Input` variable, whose pointer converts to its pointee type
/// instead (the same conversion a builtin `Input` variable's pointer uses,
/// there being no way to tell the two apart by type alone), this instead
/// loads the global eagerly right here, producing that pointee-typed value
/// directly: LoadValuePattern then collapses the real `spirv.Load` reading
/// it into the identity, exactly as it already does for a builtin's
/// `llvm.spv.*` intrinsic result.
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
    Rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(Op, ValueType, Address);
    return mlir::success();
  }

private:
  const feme::spirv::StageIOInfoMap &StageIOVariables;
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

/// Replaces `spirv.mlir.addressof` of a resource variable with the
/// `llvm.spv.resource.handlefrombinding` call producing its handle. As for
/// builtin variables, there is no LLVM global to address: LLVM's SPIRV
/// backend emits the `OpVariable` and its `DescriptorSet`/`Binding`
/// decorations from the intrinsic, so `!spirv.ptr<image, UniformConstant>`
/// converts to the handle type itself.
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
    // A `spirv.GlobalVariable` of image type declares exactly one resource,
    // not an array of them, so the binding holds a single descriptor and the
    // index into it is always zero.
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
/// `llvm/test/CodeGen/SPIRV/pointers/structured-buffer-access.ll`).
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

    mlir::Type ResultType =
        getTypeConverter()->convertType(Op.getComponentPtr().getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value ElementPtr = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.resource.getpointer", ResultType,
        {Adaptor.getBasePtr(), Indices[Selector]});
    if (Indices.size() == Selector + 1) {
      Rewriter.replaceOp(Op, ElementPtr);
      return mlir::success();
    }

    // Further indices navigate what `llvm.spv.resource.getpointer` just
    // selected -- an array element's own fields, in the wrapper shape
    // (whose sole content is always that homogeneous array); one of the
    // direct shape's own members, itself possibly an array, a matrix, or a
    // nested struct -- so its own SPIR-V type has to be recovered to
    // convert the right one. The leading 0 dereferences through the
    // pointer `llvm.spv.resource.getpointer` returned, exactly as an
    // ordinary GEP into a pointer operand would.
    mlir::Type SelectedType;
    if (Element->HasWrapper) {
      SelectedType =
          mlir::cast<mlir::spirv::RuntimeArrayType>(
              mlir::cast<mlir::spirv::StructType>(PointerType.getPointeeType())
                  .getElementType(0))
              .getElementType();
    } else {
      std::optional<uint64_t> MemberIndex =
          getConstantMemberIndex(Op.getIndices()[Selector]);
      if (!MemberIndex)
        return Rewriter.notifyMatchFailure(Op,
                                           "member selector is not a constant");
      SelectedType = mlir::cast<mlir::spirv::StructType>(Element->Content)
                         .getElementType(*MemberIndex);
    }
    mlir::Type ElementType = getTypeConverter()->convertType(SelectedType);
    if (!ElementType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
    GEPIndices.push_back(0);
    llvm::append_range(GEPIndices, Indices.drop_front(Selector + 1));
    Rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
        Op, ResultType, ElementType, ElementPtr, GEPIndices,
        mlir::LLVM::GEPNoWrapFlags::inbounds);
    return mlir::success();
  }
};

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
/// reproduce, or an unconvertible member type).
mlir::Type convertOffsetStructTypeIgnoringDecorations(
    mlir::spirv::StructType Type, const mlir::TypeConverter &Converter) {
  llvm::SmallVector<mlir::Type, 8> Members;
  for (unsigned I = 0, E = Type.getNumElements(); I != E; ++I) {
    mlir::Type MemberTy = Converter.convertType(Type.getElementType(I));
    if (!MemberTy)
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

/// Returns true if \p ImageOperands names any actual modifier (e.g. `Lod`,
/// `Bias`) rather than being absent or the empty `None` bit-enum value --
/// real `dxc`-compiled SPIR-V spells "no modifiers" as an explicit
/// `#spirv.image_operands<None>` attribute rather than omitting the
/// (optional) attribute entirely, so a presence check alone rejects every
/// image access real SPIR-V input produces.
bool hasImageOperands(std::optional<mlir::spirv::ImageOperands> ImageOperands) {
  return ImageOperands && *ImageOperands != mlir::spirv::ImageOperands::None;
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
/// Converts a `spirv.ImageSampleExplicitLod` with exactly the `Lod` image
/// operand (not `Grad`, and not combined with any other modifier) into the
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
    if (Op.getImageOperands() != mlir::spirv::ImageOperands::Lod ||
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

/// Drops `spirv.ExecutionMode`, whose contents FeMe instead reads before
/// conversion and re-emits as function attributes on the entry point (see
/// feme::spirv::createConvertSPIRVToLLVMPass). MLIR's own pattern turns it
/// into a `__spv__<entry>_execution_mode_info_<mode>` global describing the
/// mode to the SPIR-V *runner*, which LLVM's SPIRV backend has no notion of.
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
/// -- the block's own field struct for the wrapper shape, or that struct
/// directly for the shape glslang emits, including any sized-array or
/// matrix member). Vulkan disallows writing
/// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, so the writability integer parameter is
/// always 0, unlike a storage buffer block's.
mlir::Type
convertUniformBlockType(mlir::spirv::PointerType Type,
                        const mlir::LLVMTypeConverter &TypeConverter) {
  std::optional<BlockElement> Element = getUniformBlockElement(Type);
  if (!Element)
    return nullptr;
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
  // has nothing to convert to but the value itself. A non-builtin `Input`
  // variable (an ordinary stage-IO variable FeMe has no way to distinguish
  // from a builtin one by type alone, since e.g. both can be a plain `i32`)
  // shares this same conversion -- StageIOAddressOfPattern accordingly reads
  // it eagerly at the `spirv.mlir.addressof` site too (rather than
  // converting to a real pointer the way a non-builtin `Output` variable
  // does just below, which no builtin ever is), so that this conversion's
  // answer for every `Input` pointer type stays exactly one thing regardless
  // of which kind of variable it is.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Input &&
        !isResourcePointer(Type))
      return std::nullopt;
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
    if (!PointerType || (!isResourcePointer(PointerType) &&
                         !isBufferBlockPointer(PointerType) &&
                         !isUniformBlockPointer(PointerType)))
      continue;
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
    Resources[SymName] = {*Set, *Binding, NameSymbol};
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
    const StageIOInfoMap &StageIOVariables) {
  Patterns
      .add<ArrayConstantPattern, BuiltInAddressOfPattern,
           BuiltInGlobalVariablePattern, BlockAccessChainPattern,
           CompositeConstructPattern, ExecutionModePattern, ImageFetchPattern,
           ImageSampleExplicitLodPattern, ImageSampleImplicitLodPattern,
           ImageQuerySizePattern, ImageReadPattern, ImageWritePattern,
           LoadValuePattern, PushConstantGlobalVariablePattern,
           SampledImagePattern, StageIOGlobalVariablePattern>(
          Patterns.getContext(), TypeConverter, FeMeBenefit);
  Patterns.add<ResourceAddressOfPattern, ResourceGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit, Resources);
  Patterns.add<StageIOAddressOfPattern>(Patterns.getContext(), TypeConverter,
                                        FeMeBenefit, StageIOVariables);
}
