//===- ConvertSPIRVToLLVMPass.cpp - spirv dialect -> llvm dialect --------===//
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
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVEnums.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/TargetParser/Triple.h"

namespace {

/// Returns the shader pipeline stage a SPIR-V execution model names, in the
/// spelling LLVM's triples use for it, or `UnknownEnvironment` for the
/// execution models that are not a graphics pipeline stage at all
/// (`Kernel`), which the caller handles separately.
llvm::Triple::EnvironmentType
getStageForExecutionModel(mlir::spirv::ExecutionModel Model) {
  switch (Model) {
  case mlir::spirv::ExecutionModel::Vertex:
    return llvm::Triple::Vertex;
  case mlir::spirv::ExecutionModel::TessellationControl:
    return llvm::Triple::Hull;
  case mlir::spirv::ExecutionModel::TessellationEvaluation:
    return llvm::Triple::Domain;
  case mlir::spirv::ExecutionModel::Geometry:
    return llvm::Triple::Geometry;
  case mlir::spirv::ExecutionModel::Fragment:
    return llvm::Triple::Pixel;
  case mlir::spirv::ExecutionModel::GLCompute:
    return llvm::Triple::Compute;
  case mlir::spirv::ExecutionModel::RayGenerationKHR:
    return llvm::Triple::RayGeneration;
  case mlir::spirv::ExecutionModel::IntersectionKHR:
    return llvm::Triple::Intersection;
  case mlir::spirv::ExecutionModel::AnyHitKHR:
    return llvm::Triple::AnyHit;
  case mlir::spirv::ExecutionModel::ClosestHitKHR:
    return llvm::Triple::ClosestHit;
  case mlir::spirv::ExecutionModel::MissKHR:
    return llvm::Triple::Miss;
  case mlir::spirv::ExecutionModel::CallableKHR:
    return llvm::Triple::Callable;
  case mlir::spirv::ExecutionModel::TaskEXT:
    return llvm::Triple::Amplification;
  case mlir::spirv::ExecutionModel::MeshEXT:
    return llvm::Triple::Mesh;
  default:
    return llvm::Triple::UnknownEnvironment;
  }
}

/// Sets \p Module's `llvm.target_triple` and `llvm.data_layout` attributes to
/// \p TargetTriple and the data layout that triple implies, which
/// `mlir::translateModuleToLLVMIR` copies onto the `llvm::Module` it
/// produces.
void setTargetAttributes(mlir::ModuleOp Module, llvm::StringRef TargetTriple) {
  mlir::MLIRContext *Ctx = Module.getContext();
  Module->setAttr(mlir::LLVM::LLVMDialect::getTargetTripleAttrName(),
                  mlir::StringAttr::get(Ctx, TargetTriple));
  Module->setAttr(
      mlir::LLVM::LLVMDialect::getDataLayoutAttrName(),
      mlir::StringAttr::get(
          Ctx, llvm::Triple(TargetTriple.str()).computeDataLayout()));
}

/// FeMe's `spirv` dialect -> `llvm` dialect conversion; see
/// feme/include/feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h.
class ConvertSPIRVToLLVMPass
    : public mlir::PassWrapper<ConvertSPIRVToLLVMPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConvertSPIRVToLLVMPass)

  llvm::StringRef getArgument() const override {
    return feme::spirv::getConvertSPIRVToLLVMPassArgument();
  }

  llvm::StringRef getDescription() const override {
    return "Convert the spirv dialect to the llvm dialect, targeting LLVM's "
           "SPIRV backend";
  }

  void getDependentDialects(mlir::DialectRegistry &Registry) const override {
    Registry.insert<mlir::LLVM::LLVMDialect>();
  }

  void runOnOperation() override;
};

void ConvertSPIRVToLLVMPass::runOnOperation() {
  mlir::ModuleOp Module = getOperation();
  mlir::MLIRContext *Ctx = &getContext();

  // The conversion replaces each `spirv.module` in place with the
  // `builtin.module` holding its converted body, so remember which of the
  // outer module's children each one is: that is what the target triple
  // recovered from it has to be attached to once it no longer exists.
  llvm::SmallVector<std::pair<unsigned, std::string>> Triples;
  unsigned Index = 0;
  for (mlir::Operation &Op : Module.getBody()->getOperations()) {
    if (auto SPIRVModule = mlir::dyn_cast<mlir::spirv::ModuleOp>(Op))
      Triples.emplace_back(Index, feme::spirv::getTargetTriple(SPIRVModule));
    ++Index;
  }

  mlir::LowerToLLVMOptions Options(Ctx);
  mlir::LLVMTypeConverter TypeConverter(Ctx, Options);
  mlir::populateSPIRVToLLVMTypeConversion(TypeConverter);

  mlir::RewritePatternSet Patterns(Ctx);
  mlir::populateSPIRVToLLVMModuleConversionPatterns(TypeConverter, Patterns);
  mlir::populateSPIRVToLLVMConversionPatterns(TypeConverter, Patterns);
  mlir::populateSPIRVToLLVMFunctionConversionPatterns(TypeConverter, Patterns);

  mlir::ConversionTarget Target(*Ctx);
  Target.addIllegalDialect<mlir::spirv::SPIRVDialect>();
  Target.addLegalDialect<mlir::LLVM::LLVMDialect>();
  Target.addLegalOp<mlir::ModuleOp>();

  if (mlir::failed(
          mlir::applyPartialConversion(Module, Target, std::move(Patterns))))
    return signalPassFailure();

  mlir::Block::OpListType &Converted = Module.getBody()->getOperations();
  for (const std::pair<unsigned, std::string> &Entry : Triples) {
    auto It = std::next(Converted.begin(), Entry.first);
    if (It == Converted.end())
      continue;
    if (auto Inner = mlir::dyn_cast<mlir::ModuleOp>(*It))
      setTargetAttributes(Inner, Entry.second);
  }
}

} // namespace

std::string feme::spirv::getTargetTriple(mlir::spirv::ModuleOp Module) {
  // A `Kernel` entry point is an OpenCL compute kernel rather than a
  // graphics pipeline stage, so it gets the OpenCL flavored triple LLVM's
  // SPIRV backend keys its `Kernel` environment off, sized by the module's
  // addressing model. Everything else is a Vulkan shader stage.
  bool IsKernel = false;
  llvm::Triple::EnvironmentType Stage = llvm::Triple::UnknownEnvironment;
  for (auto EntryPoint : Module.getOps<mlir::spirv::EntryPointOp>()) {
    if (EntryPoint.getExecutionModel() == mlir::spirv::ExecutionModel::Kernel) {
      IsKernel = true;
      break;
    }
    if (Stage == llvm::Triple::UnknownEnvironment)
      Stage = getStageForExecutionModel(EntryPoint.getExecutionModel());
  }

  if (IsKernel ||
      Module.getAddressingModel() != mlir::spirv::AddressingModel::Logical) {
    return Module.getAddressingModel() ==
                   mlir::spirv::AddressingModel::Physical32
               ? "spirv32-unknown-unknown"
               : "spirv64-unknown-unknown";
  }

  if (Stage == llvm::Triple::UnknownEnvironment)
    return "spirv-unknown-vulkan";
  return ("spirv-unknown-vulkan-" + llvm::Triple::getEnvironmentTypeName(Stage))
      .str();
}

std::unique_ptr<mlir::Pass> feme::spirv::createConvertSPIRVToLLVMPass() {
  return std::make_unique<ConvertSPIRVToLLVMPass>();
}

llvm::StringRef feme::spirv::getConvertSPIRVToLLVMPassArgument() {
  return "feme-convert-spirv-to-llvm";
}
