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
    if (!PointerType || !isResourcePointer(PointerType))
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
  Patterns.add<BuiltInAddressOfPattern, BuiltInGlobalVariablePattern,
               ExecutionModePattern, ImageQuerySizePattern, ImageReadPattern,
               ImageWritePattern, LoadValuePattern>(Patterns.getContext(),
                                                    TypeConverter, FeMeBenefit);
  Patterns.add<ResourceAddressOfPattern, ResourceGlobalVariablePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit, Resources);
}
