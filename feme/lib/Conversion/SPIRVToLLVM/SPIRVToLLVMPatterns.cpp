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
  // input variable is a value LLVM's SPIRV backend materializes on demand,
  // not memory, so the pointer SPIR-V reads it through has nothing to convert
  // to but the value itself. Non-builtin `Input` variables (stage inputs)
  // have no LLVM equivalent either way, and now fail to legalize with a
  // diagnostic rather than converting to a pointer nothing can produce.
  TypeConverter.addConversion([&TypeConverter](mlir::spirv::PointerType Type)
                                  -> std::optional<mlir::Type> {
    if (Type.getStorageClass() != mlir::spirv::StorageClass::Input)
      return std::nullopt;
    return TypeConverter.convertType(Type.getPointeeType());
  });
}

void feme::spirv::populateSPIRVToLLVMTargetPatterns(
    const mlir::LLVMTypeConverter &TypeConverter,
    mlir::RewritePatternSet &Patterns) {
  Patterns.add<BuiltInAddressOfPattern, BuiltInGlobalVariablePattern,
               ExecutionModePattern, LoadValuePattern>(
      Patterns.getContext(), TypeConverter, FeMeBenefit);
}
