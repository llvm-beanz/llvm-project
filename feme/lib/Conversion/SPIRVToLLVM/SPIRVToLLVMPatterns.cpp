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
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

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

/// A SPIR-V storage/uniform buffer block is always spelled as a single
/// SPIR-V `Block`-decorated struct with exactly one member (the HLSL ->
/// SPIR-V compilation FeMe targets never emits more than one, and neither
/// does the `spirv.VulkanBuffer` representation this converts to -- see
/// https://github.com/llvm/wg-hlsl/blob/main/proposals/0018-spirv-resource-representation.md).
/// Returns that sole member, the runtime array `RWStructuredBuffer<T>`/
/// `StructuredBuffer<T>` compile down to, or a null type for any other shape
/// (which does not convert -- see the "Known gap" note in the SPIR-V section
/// of feme/docs/Design.md for what that leaves out, notably `cbuffer`/
/// `ConstantBuffer<T>`, whose `Uniform` storage class this does not match).
mlir::spirv::RuntimeArrayType
getBufferBlockElementArray(mlir::spirv::PointerType Type) {
  if (Type.getStorageClass() != mlir::spirv::StorageClass::StorageBuffer)
    return nullptr;
  auto Struct = mlir::dyn_cast<mlir::spirv::StructType>(Type.getPointeeType());
  if (!Struct || Struct.getNumElements() != 1)
    return nullptr;
  return mlir::dyn_cast<mlir::spirv::RuntimeArrayType>(
      Struct.getElementType(0));
}

/// Returns true if \p Type is a pointer to a storage buffer block -- see
/// getBufferBlockElementArray.
bool isBufferBlockPointer(mlir::spirv::PointerType Type) {
  return static_cast<bool>(getBufferBlockElementArray(Type));
}

/// Returns false if \p Struct's sole member -- the runtime array a storage
/// buffer block wraps -- carries a `NonWritable` decoration, i.e. the buffer
/// is a `StructuredBuffer<T>` (SRV) rather than a `RWStructuredBuffer<T>`
/// (UAV); true otherwise.
bool isBufferBlockWritable(mlir::spirv::StructType Struct) {
  llvm::SmallVector<mlir::spirv::StructType::MemberDecorationInfo, 1>
      Decorations;
  Struct.getMemberDecorations(0, Decorations);
  for (const auto &Decoration : Decorations)
    if (Decoration.decoration == mlir::spirv::Decoration::NonWritable)
      return false;
  return true;
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
/// builtin variable has in SPIR-V is modeled as the value it holds -- which
/// is why `!spirv.ptr<T, Input>` converts to `T`, see
/// feme::spirv::populateSPIRVToLLVMTargetTypeConversions.
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

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
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

/// Converts `spirv.AccessChain` into a storage buffer whose base pointer
/// converted to a `spirv.VulkanBuffer` handle rather than an ordinary LLVM
/// pointer (see feme::spirv::getBufferBlockElementArray), which MLIR's own
/// `AccessChainPattern` cannot handle since it assumes its base pointer
/// converts to `!llvm.ptr`. SPIR-V spells a storage buffer access as an
/// access chain into the wrapping `Block` struct's sole member (its runtime
/// array) and then into that array's element, so the leading index -- the
/// member selector, always 0 -- is dropped, and the second -- the array
/// element -- becomes `llvm.spv.resource.getpointer`'s index; any further
/// indices navigate the element type's own fields with an ordinary
/// `llvm.getelementptr`, matching how real `dxc`-compiled SPIR-V is
/// expected to lower on LLVM's SPIRV backend (see
/// `llvm/test/CodeGen/SPIRV/pointers/structured-buffer-access.ll`).
class StorageBufferAccessChainPattern
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
      return Rewriter.notifyMatchFailure(Op, "not a storage buffer access");

    mlir::ValueRange Indices = Adaptor.getIndices();
    if (Indices.size() < 2)
      return Rewriter.notifyMatchFailure(
          Op, "expected a member selector and an array index");

    mlir::Type ResultType =
        getTypeConverter()->convertType(Op.getComponentPtr().getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value ElementPtr = createIntrinsicCall(
        Rewriter, Loc, "llvm.spv.resource.getpointer", ResultType,
        {Adaptor.getBasePtr(), Indices[1]});
    if (Indices.size() == 2) {
      Rewriter.replaceOp(Op, ElementPtr);
      return mlir::success();
    }

    // Further indices navigate the array element's own fields; the leading
    // 0 dereferences through the pointer `llvm.spv.resource.getpointer`
    // returned, exactly as an ordinary GEP into a pointer operand would.
    auto ElementType = mlir::cast<mlir::LLVM::LLVMArrayType>(
                            HandleType.getTypeParams().front())
                            .getElementType();
    llvm::SmallVector<mlir::LLVM::GEPArg> GEPIndices;
    GEPIndices.push_back(0);
    llvm::append_range(GEPIndices, Indices.drop_front(2));
    Rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
        Op, ResultType, ElementType, ElementPtr, GEPIndices,
        mlir::LLVM::GEPNoWrapFlags::inbounds);
    return mlir::success();
  }
};

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
/// supports -- `PushConstant` is just not one of them.
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

/// Converts `spirv.ImageRead` into a load through the read location.
class ImageReadPattern
    : public mlir::SPIRVToLLVMConversion<mlir::spirv::ImageReadOp> {
public:
  using mlir::SPIRVToLLVMConversion<
      mlir::spirv::ImageReadOp>::SPIRVToLLVMConversion;

  mlir::LogicalResult
  matchAndRewrite(mlir::spirv::ImageReadOp Op, OpAdaptor Adaptor,
                  mlir::ConversionPatternRewriter &Rewriter) const override {
    if (hasImageOperands(Op.getImageOperands()))
      return Rewriter.notifyMatchFailure(Op, "image operands are unsupported");

    mlir::Type ResultType = getTypeConverter()->convertType(Op.getType());
    if (!ResultType)
      return Rewriter.notifyMatchFailure(Op, "type conversion failed");

    mlir::Value Pointer = createResourcePointer(
        Rewriter, Op.getLoc(), Adaptor.getImage(), Adaptor.getCoordinate());
    Rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(Op, ResultType, Pointer);
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

/// Appends \p Value's scalar leaves, in element order, to \p Out. A SPIR-V
/// array constant's constituents are either a further nested array (itself
/// an `ArrayAttr`, for a multi-dimensional array or an array of vectors), or
/// a vector leaf (a `DenseElementsAttr`); this descends through both so the
/// result is the same flat scalar sequence regardless of how deeply the
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
/// convertImageTypeAs's role for image/sampler resources: the element type
/// parameter is the buffer's runtime array, converted to a 0-sized
/// `!llvm.array`, and the two integer parameters are the storage class
/// (forwarded unchanged, like an image type's parameters -- see
/// getBufferBlockElementArray) and whether the buffer is writable
/// (`RWStructuredBuffer<T>`) or not (`StructuredBuffer<T>`).
mlir::Type convertBufferBlockType(mlir::spirv::PointerType Type,
                                  const mlir::LLVMTypeConverter &TypeConverter) {
  mlir::spirv::RuntimeArrayType Array = getBufferBlockElementArray(Type);
  if (!Array)
    return nullptr;
  mlir::Type ElementType = TypeConverter.convertType(Array.getElementType());
  if (!ElementType)
    return nullptr;

  bool Writable = isBufferBlockWritable(
      mlir::cast<mlir::spirv::StructType>(Type.getPointeeType()));
  return mlir::LLVM::LLVMTargetExtType::get(
      Type.getContext(), "spirv.VulkanBuffer",
      {mlir::LLVM::LLVMArrayType::get(ElementType, 0)},
      {static_cast<unsigned>(Type.getStorageClass()), Writable ? 1u : 0u});
}

} // namespace

void feme::spirv::populateSPIRVToLLVMTargetTypeConversions(
    mlir::LLVMTypeConverter &TypeConverter) {
  // Registered after MLIR's conversions so it is tried before them: a builtin
  // input variable, like a resource handle, is a value LLVM's SPIRV backend
  // materializes on demand rather than memory, so the pointer SPIR-V reads it
  // through has nothing to convert to but the value itself. Non-builtin
  // `Input` variables (stage inputs) have no LLVM equivalent either way, and
  // now fail to legalize with a diagnostic rather than converting to a
  // pointer nothing can produce.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Input &&
        !isResourcePointer(Type))
      return std::nullopt;
    return TypeConverter.convertType(Type.getPointeeType());
  });

  // A storage buffer block's own pointer converts to the handle LLVM's
  // SPIRV backend materializes it from; any other `StorageBuffer` pointer --
  // an access chain result reaching into the buffer's contents -- is
  // ordinary memory, addressed the way that backend expects a storage
  // buffer access to be (address space 11, see `storageClassToAddressSpace`
  // in `llvm/lib/Target/SPIRV/SPIRVUtils.h`) rather than MLIR's own
  // Vulkan-client default of address space 0.
  TypeConverter.addConversion(
      [&TypeConverter](
          mlir::spirv::PointerType Type) -> std::optional<mlir::Type> {
        if (Type.getStorageClass() != mlir::spirv::StorageClass::StorageBuffer)
          return std::nullopt;
        if (mlir::Type Handle = convertBufferBlockType(Type, TypeConverter))
          return Handle;
        return mlir::LLVM::LLVMPointerType::get(Type.getContext(),
                                                /*addressSpace=*/11);
      });

  // A push constant pointer is ordinary memory too, in the address space
  // (13) `feme::spirv::PushConstantGlobalVariablePattern`'s global lives in
  // -- LLVM's own `SPIRVPushConstantAccess` pass finds it there and rewrites
  // it (and every use) into the `spirv.PushConstant` handle representation
  // itself, so nothing further is needed on FeMe's side.
  TypeConverter.addConversion(
      [&TypeConverter](
          mlir::spirv::PointerType Type) -> std::optional<mlir::Type> {
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
    if (!PointerType ||
        (!isResourcePointer(PointerType) && !isBufferBlockPointer(PointerType)))
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

void feme::spirv::populateSPIRVToLLVMTargetPatterns(
    const mlir::LLVMTypeConverter &TypeConverter,
    mlir::RewritePatternSet &Patterns, const ResourceInfoMap &Resources) {
  Patterns.add<ArrayConstantPattern, BuiltInAddressOfPattern,
               BuiltInGlobalVariablePattern, CompositeConstructPattern,
               ExecutionModePattern, ImageQuerySizePattern, ImageReadPattern,
               ImageWritePattern, LoadValuePattern,
               PushConstantGlobalVariablePattern,
               StorageBufferAccessChainPattern>(Patterns.getContext(),
                                                TypeConverter, FeMeBenefit);
  Patterns.add<ResourceAddressOfPattern, ResourceGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit, Resources);
}
