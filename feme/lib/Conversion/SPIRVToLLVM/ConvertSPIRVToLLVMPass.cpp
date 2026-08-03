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
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"
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

/// What a `spirv.module` says about one of its entry points, in the spelling
/// LLVM's SPIRV backend expects to find it in: the backend reads the pipeline
/// stage from an `hlsl.shader` function attribute and the compute workgroup
/// dimensions from an `hlsl.numthreads` one (see
/// `llvm/lib/Target/SPIRV/SPIRVCallLowering.cpp` and `SPIRVAsmPrinter.cpp`),
/// rather than from module-level operations the way SPIR-V itself does.
struct EntryPointInfo {
  std::string Stage;
  std::string LocalSize;
};

/// Everything about a `spirv.module` that the conversion consumes but does
/// not preserve, collected before it runs so it can be re-attached to the
/// `llvm` dialect module the conversion leaves in its place.
struct SPIRVModuleInfo {
  /// Position of the `spirv.module` among the outer module's children; the
  /// converted module takes the same position.
  unsigned Index;
  std::string TargetTriple;
  llvm::StringMap<EntryPointInfo> EntryPoints;
};

/// Returns \p Values, an execution mode's operands, as the comma separated
/// list `hlsl.numthreads` spells workgroup dimensions with.
std::string formatLocalSize(mlir::ArrayAttr Values) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  llvm::interleave(
      Values, OS,
      [&](mlir::Attribute Value) {
        OS << mlir::cast<mlir::IntegerAttr>(Value).getInt();
      },
      ",");
  return Result;
}

/// Collects \p Module's entry points, keyed by the function each names.
llvm::StringMap<EntryPointInfo>
collectEntryPoints(mlir::spirv::ModuleOp Module) {
  llvm::StringMap<EntryPointInfo> EntryPoints;
  for (auto EntryPoint : Module.getOps<mlir::spirv::EntryPointOp>()) {
    llvm::Triple::EnvironmentType Stage =
        getStageForExecutionModel(EntryPoint.getExecutionModel());
    if (Stage == llvm::Triple::UnknownEnvironment)
      continue;
    EntryPoints[EntryPoint.getFn()].Stage =
        llvm::Triple::getEnvironmentTypeName(Stage).str();
  }

  for (auto Mode : Module.getOps<mlir::spirv::ExecutionModeOp>()) {
    if (Mode.getExecutionMode() != mlir::spirv::ExecutionMode::LocalSize)
      continue;
    auto It = EntryPoints.find(Mode.getFn());
    if (It != EntryPoints.end())
      It->second.LocalSize = formatLocalSize(Mode.getValues());
  }
  return EntryPoints;
}

/// Adds the `key = value` function attribute \p Key/\p Value to \p Func's
/// passthrough list, which `mlir::translateModuleToLLVMIR` turns into a
/// string attribute on the `llvm::Function`.
void addPassthroughAttribute(mlir::LLVM::LLVMFuncOp Func, llvm::StringRef Key,
                             llvm::StringRef Value) {
  mlir::MLIRContext *Ctx = Func.getContext();
  llvm::SmallVector<mlir::Attribute> Passthrough;
  if (mlir::ArrayAttr Existing = Func.getPassthroughAttr())
    llvm::append_range(Passthrough, Existing);
  Passthrough.push_back(
      mlir::ArrayAttr::get(Ctx, {mlir::StringAttr::get(Ctx, Key),
                                 mlir::StringAttr::get(Ctx, Value)}));
  Func.setPassthroughAttr(mlir::ArrayAttr::get(Ctx, Passthrough));
}

/// Re-attaches \p EntryPoints to the converted functions they describe.
void applyEntryPointAttributes(
    mlir::ModuleOp Module, const llvm::StringMap<EntryPointInfo> &EntryPoints) {
  Module.walk([&](mlir::LLVM::LLVMFuncOp Func) {
    auto It = EntryPoints.find(Func.getSymName());
    if (It == EntryPoints.end())
      return;
    addPassthroughAttribute(Func, "hlsl.shader", It->second.Stage);
    if (!It->second.LocalSize.empty())
      addPassthroughAttribute(Func, "hlsl.numthreads", It->second.LocalSize);
  });
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
  // outer module's children each one is: that is what the module-level
  // information recovered from it has to be attached to once it no longer
  // exists.
  llvm::SmallVector<SPIRVModuleInfo> Modules;
  unsigned Index = 0;
  for (mlir::Operation &Op : Module.getBody()->getOperations()) {
    if (auto SPIRVModule = mlir::dyn_cast<mlir::spirv::ModuleOp>(Op))
      Modules.push_back({Index, feme::spirv::getTargetTriple(SPIRVModule),
                         collectEntryPoints(SPIRVModule)});
    ++Index;
  }

  mlir::LowerToLLVMOptions Options(Ctx);
  mlir::LLVMTypeConverter TypeConverter(Ctx, Options);
  mlir::populateSPIRVToLLVMTypeConversion(TypeConverter);

  mlir::RewritePatternSet Patterns(Ctx);
  mlir::populateSPIRVToLLVMModuleConversionPatterns(TypeConverter, Patterns);
  mlir::populateSPIRVToLLVMConversionPatterns(TypeConverter, Patterns);
  mlir::populateSPIRVToLLVMFunctionConversionPatterns(TypeConverter, Patterns);
  feme::spirv::populateSPIRVToLLVMTargetPatterns(TypeConverter, Patterns);

  mlir::ConversionTarget Target(*Ctx);
  Target.addIllegalDialect<mlir::spirv::SPIRVDialect>();
  Target.addLegalDialect<mlir::LLVM::LLVMDialect>();
  Target.addLegalOp<mlir::ModuleOp>();

  if (mlir::failed(
          mlir::applyPartialConversion(Module, Target, std::move(Patterns))))
    return signalPassFailure();

  mlir::Block::OpListType &Converted = Module.getBody()->getOperations();
  for (const SPIRVModuleInfo &Info : Modules) {
    auto It = std::next(Converted.begin(), Info.Index);
    if (It == Converted.end())
      continue;
    auto Inner = mlir::dyn_cast<mlir::ModuleOp>(*It);
    if (!Inner)
      continue;
    setTargetAttributes(Inner, Info.TargetTriple);
    applyEntryPointAttributes(Inner, Info.EntryPoints);
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
