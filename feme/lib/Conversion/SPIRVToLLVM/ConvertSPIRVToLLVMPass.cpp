//===- ConvertSPIRVToLLVMPass.cpp - spirv dialect -> llvm dialect --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Graphics/Geometry.h"
#include "feme/Graphics/Mesh.h"
#include "feme/Graphics/Tessellation.h"

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
/// rather than from module-level operations the way SPIR-V itself does. The
/// same stage is also recorded as FeMe's own source-independent
/// `feme.shader.stage` enumeration (see feme/include/feme/Core/ShaderStage.h).
struct EntryPointInfo {
  feme::ShaderStage Stage;
  std::string LocalSize;
  std::optional<feme::graphics::TessellatorDomain> TessDomain;
  std::optional<feme::graphics::TessPartitioning> TessPartitioning;
  std::optional<feme::graphics::TessOutputPrimitive> TessOutputPrimitive;
  std::optional<uint32_t> TessOutputControlPointCount;
  /// (Roadmap H5a) A geometry entry point's declared shape: its input/
  /// output primitive classes (SPIR-V's `InputPoints`/.../
  /// `InputTrianglesAdjacency` and `OutputPoints`/`OutputLineStrip`/
  /// `OutputTriangleStrip` execution modes) and maximum emitted vertex
  /// count (`OutputVertices` -- shared, at the SPIR-V enumerant level,
  /// with the hull stage's own output control point count, disambiguated
  /// below by this entry's `Stage`).
  std::optional<feme::graphics::GeometryInputPrimitive> GeometryInput;
  std::optional<feme::graphics::GeometryOutputPrimitive> GeometryOutput;
  std::optional<uint32_t> GeometryMaxOutputVertices;
  /// SPIR-V's `Invocations` execution mode (roadmap H5a): defaults to 1
  /// (`GeometryState::Invocations`'s own comment) when a geometry entry
  /// point's module never declares it explicitly.
  uint32_t GeometryInvocations = 1;
  /// (Roadmap H6a) A mesh entry point's declared shape: its output
  /// topology (SPIR-V's `OutputPoints`/`OutputLinesEXT`/
  /// `OutputTrianglesEXT` execution modes -- `OutputPoints` shared, at the
  /// SPIR-V enumerant level, with a geometry entry's own point-output
  /// mode), maximum emitted vertex count (`OutputVertices` -- shared with
  /// both a hull entry's output control point count and a geometry
  /// entry's own maximum emitted vertex count) and maximum emitted
  /// primitive count (`OutputPrimitivesEXT`).
  std::optional<feme::graphics::MeshOutputTopology> MeshTopology;
  std::optional<uint32_t> MeshMaxOutputVertices;
  std::optional<uint32_t> MeshMaxOutputPrimitives;
  /// The bit widths (16/32/64) `VK_KHR_shader_float_controls`'s
  /// `RoundingModeRTZ` execution mode was declared for (roadmap F15a): each
  /// arithmetic FP op conversion pattern of that width in this entry point
  /// routes through a `llvm.experimental.constrained.*` intrinsic with an
  /// explicit round-toward-zero rounding mode instead of the plain,
  /// round-to-nearest-even op MLIR's own pattern would otherwise produce
  /// (see feme::spirv::populateSPIRVToLLVMTargetPatterns's
  /// ConstrainedRoundTowardZeroPattern, SPIRVToLLVMPatterns.cpp). Empty for
  /// an entry point that never declared the mode.
  llvm::SmallVector<unsigned, 3> RoundingModeRTZWidths;
  /// The bit widths (16/32/64) `VK_KHR_shader_float_controls`'s
  /// `DenormFlushToZero` execution mode was declared for (roadmap F15b):
  /// each arithmetic FP op conversion pattern of that width in this entry
  /// point flushes any subnormal operand or result to a same-signed zero
  /// (see feme::spirv::populateSPIRVToLLVMTargetPatterns's
  /// FloatControlArithmeticPattern, SPIRVToLLVMPatterns.cpp) rather than
  /// preserving it. Empty for an entry point that never declared the mode.
  llvm::SmallVector<unsigned, 3> DenormFlushToZeroWidths;
  /// Whether this entry point's body contains an arithmetic FP op carrying
  /// a non-default `fp_rounding_mode` decoration
  /// (`VK_KHR_shader_float_controls2`'s per-instruction `FPRoundingMode`,
  /// roadmap F15c): like `RoundingModeRTZWidths` above, this also needs
  /// `strictfp` on the entry point, since `FloatControlArithmeticPattern`
  /// (SPIRVToLLVMPatterns.cpp) honors this decoration the same way --
  /// routing that one instruction through a
  /// `llvm.experimental.constrained.*` intrinsic -- independent of whether
  /// the entry point declared `RoundingModeRTZ` itself.
  bool NeedsStrictFPForPerInstructionRounding = false;
  /// `VK_KHR_shader_float_controls2`'s own `FPFastMathDefault` execution
  /// mode (roadmap F15d): the per-type default `FPFastMathMode`
  /// `FloatControlArithmeticPattern` (SPIRVToLLVMPatterns.cpp) applies to
  /// an arithmetic op of the matching bit width that carries no
  /// `fp_fast_math_mode` decoration of its own. Keyed by bit width
  /// (16/32/64); absent entirely for a width the entry point declared no
  /// default for.
  llvm::DenseMap<unsigned, mlir::spirv::FPFastMathMode> FastMathDefaults;
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

/// Returns `RoundingModeRTZ`'s or `DenormFlushToZero`'s single literal
/// operand: the bit width (16, 32, or 64) the entry point asks for
/// truncating-rounding or flushed-denormal arithmetic at, respectively.
unsigned getFloatControlWidth(mlir::spirv::ExecutionModeOp Mode) {
  return static_cast<unsigned>(
      mlir::cast<mlir::IntegerAttr>(Mode.getValues()[0]).getInt());
}

std::string formatTessellationOutputPrimitive(
    feme::graphics::TessOutputPrimitive Primitive) {
  switch (Primitive) {
  case feme::graphics::TessOutputPrimitive::Point:
    return "point";
  case feme::graphics::TessOutputPrimitive::Line:
    return "line";
  case feme::graphics::TessOutputPrimitive::TriangleCw:
    return "triangle_cw";
  case feme::graphics::TessOutputPrimitive::TriangleCcw:
    return "triangle_ccw";
  }
  llvm_unreachable("unhandled TessOutputPrimitive");
}

std::string formatTessellatorDomain(feme::graphics::TessellatorDomain Domain) {
  switch (Domain) {
  case feme::graphics::TessellatorDomain::Isoline:
    return "isoline";
  case feme::graphics::TessellatorDomain::Triangle:
    return "triangle";
  case feme::graphics::TessellatorDomain::Quad:
    return "quad";
  }
  llvm_unreachable("unhandled TessellatorDomain");
}

std::string
formatTessPartitioning(feme::graphics::TessPartitioning Partitioning) {
  switch (Partitioning) {
  case feme::graphics::TessPartitioning::Integer:
    return "integer";
  case feme::graphics::TessPartitioning::Pow2:
    return "pow2";
  case feme::graphics::TessPartitioning::FractionalOdd:
    return "fractional_odd";
  case feme::graphics::TessPartitioning::FractionalEven:
    return "fractional_even";
  }
  llvm_unreachable("unhandled TessPartitioning");
}

std::string
formatGeometryInputPrimitive(feme::graphics::GeometryInputPrimitive Primitive) {
  switch (Primitive) {
  case feme::graphics::GeometryInputPrimitive::Points:
    return "points";
  case feme::graphics::GeometryInputPrimitive::Lines:
    return "lines";
  case feme::graphics::GeometryInputPrimitive::LinesAdjacency:
    return "lines_adjacency";
  case feme::graphics::GeometryInputPrimitive::Triangles:
    return "triangles";
  case feme::graphics::GeometryInputPrimitive::TrianglesAdjacency:
    return "triangles_adjacency";
  }
  llvm_unreachable("unhandled GeometryInputPrimitive");
}

std::string formatGeometryOutputPrimitive(
    feme::graphics::GeometryOutputPrimitive Primitive) {
  switch (Primitive) {
  case feme::graphics::GeometryOutputPrimitive::Points:
    return "points";
  case feme::graphics::GeometryOutputPrimitive::LineStrip:
    return "line_strip";
  case feme::graphics::GeometryOutputPrimitive::TriangleStrip:
    return "triangle_strip";
  }
  llvm_unreachable("unhandled GeometryOutputPrimitive");
}

std::string
formatMeshOutputTopology(feme::graphics::MeshOutputTopology Topology) {
  switch (Topology) {
  case feme::graphics::MeshOutputTopology::Points:
    return "points";
  case feme::graphics::MeshOutputTopology::Lines:
    return "lines";
  case feme::graphics::MeshOutputTopology::Triangles:
    return "triangles";
  }
  llvm_unreachable("unhandled MeshOutputTopology");
}

/// Returns whether \p Func's body contains an arithmetic FP op
/// (`spirv.FAdd`/`FSub`/`FMul`/`FDiv`/`FRem`) carrying an `fp_rounding_mode`
/// decoration (`VK_KHR_shader_float_controls2`'s per-instruction
/// `FPRoundingMode`, roadmap F15c) that requests anything other than
/// round-to-nearest-even: `FloatControlArithmeticPattern`
/// (SPIRVToLLVMPatterns.cpp) routes that one instruction through a
/// constrained `llvm.experimental.constrained.*` intrinsic for exactly
/// this reason, needing `strictfp` on the enclosing entry point just as
/// much as a whole-entry-point `RoundingModeRTZ` execution mode does (see
/// EntryPointInfo::RoundingModeRTZWidths).
bool hasNonDefaultPerInstructionRoundingMode(mlir::spirv::FuncOp Func) {
  bool Found = false;
  Func.walk([&](mlir::Operation *Op) {
    if (Found || !mlir::isa<mlir::spirv::FAddOp, mlir::spirv::FSubOp,
                            mlir::spirv::FMulOp, mlir::spirv::FDivOp,
                            mlir::spirv::FRemOp>(Op))
      return;
    auto Mode =
        Op->getAttrOfType<mlir::spirv::FPRoundingModeAttr>("fp_rounding_mode");
    if (Mode && Mode.getValue() != mlir::spirv::FPRoundingMode::RTE)
      Found = true;
  });
  return Found;
}

/// Collects \p Module's entry points into \p EntryPoints, keyed by the
/// function each names, and validates each one's stage against \p
/// TargetTriple -- the triple the converted module will carry, whose
/// environment names the stage the module as a whole claims. A module with
/// entry points of more than one stage has no single such environment, so it
/// is reported rather than converted with a triple that describes only its
/// first entry point ("Stage identity" in
/// feme/docs/FeMeGraphicsDesign.md).
mlir::LogicalResult
collectEntryPoints(mlir::spirv::ModuleOp Module, llvm::StringRef TargetTriple,
                   llvm::StringMap<EntryPointInfo> &EntryPoints) {
  llvm::Triple::EnvironmentType Env =
      llvm::Triple(TargetTriple).getEnvironment();
  for (auto EntryPoint : Module.getOps<mlir::spirv::EntryPointOp>()) {
    llvm::Triple::EnvironmentType StageEnv =
        getStageForExecutionModel(EntryPoint.getExecutionModel());
    if (StageEnv == llvm::Triple::UnknownEnvironment)
      continue;
    std::optional<feme::ShaderStage> Stage =
        feme::getShaderStageForEnvironment(StageEnv);
    assert(Stage && "every pipeline stage environment names a shader stage");
    if (!feme::isShaderStageCompatibleWithEnvironment(*Stage, Env))
      return EntryPoint.emitError()
             << "entry point '" << EntryPoint.getFn() << "' declares stage '"
             << feme::getShaderStageName(*Stage)
             << "', which disagrees with the module's target triple '"
             << TargetTriple << "'";
    EntryPoints[EntryPoint.getFn()].Stage = *Stage;
  }

  for (auto Mode : Module.getOps<mlir::spirv::ExecutionModeOp>()) {
    auto It = EntryPoints.find(Mode.getFn());
    if (It == EntryPoints.end())
      continue;
    if (Mode.getExecutionMode() ==
        mlir::spirv::ExecutionMode::RoundingModeRTZ) {
      It->second.RoundingModeRTZWidths.push_back(getFloatControlWidth(Mode));
      continue;
    }
    if (Mode.getExecutionMode() ==
        mlir::spirv::ExecutionMode::DenormFlushToZero) {
      It->second.DenormFlushToZeroWidths.push_back(getFloatControlWidth(Mode));
      continue;
    }
    switch (Mode.getExecutionMode()) {
    case mlir::spirv::ExecutionMode::LocalSize:
      It->second.LocalSize = formatLocalSize(Mode.getValues());
      break;
    case mlir::spirv::ExecutionMode::Triangles:
      // (Roadmap H5a) Shared, at the SPIR-V enumerant level, between a
      // tessellation-evaluation entry's domain shape and a geometry
      // entry's input primitive class; the two are never the same entry
      // point, so `Stage` disambiguates which field this mode fills.
      if (It->second.Stage == feme::ShaderStage::Geometry)
        It->second.GeometryInput =
            feme::graphics::GeometryInputPrimitive::Triangles;
      else
        It->second.TessDomain = feme::graphics::TessellatorDomain::Triangle;
      break;
    case mlir::spirv::ExecutionMode::Quads:
      It->second.TessDomain = feme::graphics::TessellatorDomain::Quad;
      break;
    case mlir::spirv::ExecutionMode::Isolines:
      It->second.TessDomain = feme::graphics::TessellatorDomain::Isoline;
      break;
    case mlir::spirv::ExecutionMode::SpacingEqual:
      It->second.TessPartitioning = feme::graphics::TessPartitioning::Integer;
      break;
    case mlir::spirv::ExecutionMode::SpacingFractionalOdd:
      It->second.TessPartitioning =
          feme::graphics::TessPartitioning::FractionalOdd;
      break;
    case mlir::spirv::ExecutionMode::SpacingFractionalEven:
      It->second.TessPartitioning =
          feme::graphics::TessPartitioning::FractionalEven;
      break;
    case mlir::spirv::ExecutionMode::PointMode:
      It->second.TessOutputPrimitive =
          feme::graphics::TessOutputPrimitive::Point;
      break;
    case mlir::spirv::ExecutionMode::VertexOrderCw:
      It->second.TessOutputPrimitive =
          feme::graphics::TessOutputPrimitive::TriangleCw;
      break;
    case mlir::spirv::ExecutionMode::VertexOrderCcw:
      It->second.TessOutputPrimitive =
          feme::graphics::TessOutputPrimitive::TriangleCcw;
      break;
    case mlir::spirv::ExecutionMode::OutputVertices:
      // (Roadmap H5a/H6a) Shared, at the SPIR-V enumerant level, between a
      // hull entry's output control point count, a geometry entry's
      // maximum emitted vertex count and a mesh entry's own maximum
      // emitted vertex count; `Stage` disambiguates, exactly as for
      // `Triangles` above.
      assert(Mode.getValues().size() == 1 &&
             "verified OutputVertices has one literal operand");
      if (It->second.Stage == feme::ShaderStage::Geometry)
        It->second.GeometryMaxOutputVertices = static_cast<uint32_t>(
            mlir::cast<mlir::IntegerAttr>(Mode.getValues()[0]).getInt());
      else if (It->second.Stage == feme::ShaderStage::Mesh)
        It->second.MeshMaxOutputVertices = static_cast<uint32_t>(
            mlir::cast<mlir::IntegerAttr>(Mode.getValues()[0]).getInt());
      else
        It->second.TessOutputControlPointCount = static_cast<uint32_t>(
            mlir::cast<mlir::IntegerAttr>(Mode.getValues()[0]).getInt());
      break;
    case mlir::spirv::ExecutionMode::InputPoints:
      It->second.GeometryInput = feme::graphics::GeometryInputPrimitive::Points;
      break;
    case mlir::spirv::ExecutionMode::InputLines:
      It->second.GeometryInput = feme::graphics::GeometryInputPrimitive::Lines;
      break;
    case mlir::spirv::ExecutionMode::InputLinesAdjacency:
      It->second.GeometryInput =
          feme::graphics::GeometryInputPrimitive::LinesAdjacency;
      break;
    case mlir::spirv::ExecutionMode::InputTrianglesAdjacency:
      It->second.GeometryInput =
          feme::graphics::GeometryInputPrimitive::TrianglesAdjacency;
      break;
    case mlir::spirv::ExecutionMode::OutputPoints:
      // (Roadmap H6a) Shared, at the SPIR-V enumerant level, between a
      // geometry entry's own point-output mode and a mesh entry's output
      // topology; `Stage` disambiguates, exactly as for `OutputVertices`
      // above.
      if (It->second.Stage == feme::ShaderStage::Mesh)
        It->second.MeshTopology = feme::graphics::MeshOutputTopology::Points;
      else
        It->second.GeometryOutput =
            feme::graphics::GeometryOutputPrimitive::Points;
      break;
    case mlir::spirv::ExecutionMode::OutputLineStrip:
      It->second.GeometryOutput =
          feme::graphics::GeometryOutputPrimitive::LineStrip;
      break;
    case mlir::spirv::ExecutionMode::OutputTriangleStrip:
      It->second.GeometryOutput =
          feme::graphics::GeometryOutputPrimitive::TriangleStrip;
      break;
    case mlir::spirv::ExecutionMode::OutputLinesEXT:
      It->second.MeshTopology = feme::graphics::MeshOutputTopology::Lines;
      break;
    case mlir::spirv::ExecutionMode::OutputTrianglesEXT:
      It->second.MeshTopology = feme::graphics::MeshOutputTopology::Triangles;
      break;
    case mlir::spirv::ExecutionMode::OutputPrimitivesEXT:
      assert(Mode.getValues().size() == 1 &&
             "verified OutputPrimitivesEXT has one literal operand");
      It->second.MeshMaxOutputPrimitives = static_cast<uint32_t>(
          mlir::cast<mlir::IntegerAttr>(Mode.getValues()[0]).getInt());
      break;
    case mlir::spirv::ExecutionMode::Invocations:
      assert(Mode.getValues().size() == 1 &&
             "verified Invocations has one literal operand");
      It->second.GeometryInvocations = static_cast<uint32_t>(
          mlir::cast<mlir::IntegerAttr>(Mode.getValues()[0]).getInt());
      break;
    default:
      break;
    }
  }

  for (auto &[Name, Info] : EntryPoints) {
    if (!Info.TessDomain || !Info.TessPartitioning)
      continue;
    if (!Info.TessOutputPrimitive) {
      if (*Info.TessDomain == feme::graphics::TessellatorDomain::Isoline)
        Info.TessOutputPrimitive = feme::graphics::TessOutputPrimitive::Line;
    }
  }

  // Unlike the whole-module maps above, `FPRoundingMode` (roadmap F15c) is
  // a per-instruction decoration read directly off the individual
  // arithmetic op it decorates (see FloatControlArithmeticPattern,
  // SPIRVToLLVMPatterns.cpp), so this pass need not collect its own widths
  // the way it does for `RoundingModeRTZ`/`DenormFlushToZero` above --
  // except for whether the entry point needs `strictfp`, which has to be
  // known before this function's body is legalized away.
  for (auto Func : Module.getOps<mlir::spirv::FuncOp>()) {
    auto It = EntryPoints.find(Func.getName());
    if (It != EntryPoints.end() &&
        hasNonDefaultPerInstructionRoundingMode(Func))
      It->second.NeedsStrictFPForPerInstructionRounding = true;
  }

  // `FPFastMathDefault` (roadmap F15d) is a whole-entry-point default, one
  // `spirv.ExecutionModeId` per floating-point type, so -- like
  // `RoundingModeRTZ`/`DenormFlushToZero` above, and unlike the
  // per-instruction decorations F15c reads straight off their own op -- it
  // does need collecting here, before the conversion drops it.
  for (auto Mode : Module.getOps<mlir::spirv::ExecutionModeIdOp>()) {
    if (Mode.getExecutionMode() !=
        mlir::spirv::ExecutionMode::FPFastMathDefault)
      continue;
    auto It = EntryPoints.find(Mode.getFn());
    if (It == EntryPoints.end())
      continue;
    llvm::ArrayRef<mlir::Attribute> Values = Mode.getValues().getValue();
    assert(Values.size() == 2 &&
           "verified FPFastMathDefault has a target type and a fast-math "
           "mode operand");
    unsigned Width = mlir::cast<mlir::TypeAttr>(Values[0])
                         .getValue()
                         .getIntOrFloatBitWidth();
    auto Mask = static_cast<uint32_t>(
        mlir::cast<mlir::IntegerAttr>(Values[1]).getInt());
    It->second.FastMathDefaults[Width] =
        static_cast<mlir::spirv::FPFastMathMode>(Mask);
  }
  return mlir::success();
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

/// Adds the value-less function attribute \p Attr to \p Func's passthrough
/// list, the same way addPassthroughAttribute does for a `key = value` one.
void addBarePassthroughAttribute(mlir::LLVM::LLVMFuncOp Func,
                                 llvm::StringRef Attr) {
  mlir::MLIRContext *Ctx = Func.getContext();
  llvm::SmallVector<mlir::Attribute> Passthrough;
  if (mlir::ArrayAttr Existing = Func.getPassthroughAttr())
    llvm::append_range(Passthrough, Existing);
  Passthrough.push_back(mlir::StringAttr::get(Ctx, Attr));
  Func.setPassthroughAttr(mlir::ArrayAttr::get(Ctx, Passthrough));
}

/// Re-attaches \p EntryPoints to the converted functions they describe.
void applyEntryPointAttributes(
    mlir::ModuleOp Module, const llvm::StringMap<EntryPointInfo> &EntryPoints) {
  Module.walk([&](mlir::LLVM::LLVMFuncOp Func) {
    auto It = EntryPoints.find(Func.getSymName());
    if (It == EntryPoints.end())
      return;
    addPassthroughAttribute(
        Func, "hlsl.shader",
        llvm::Triple::getEnvironmentTypeName(
            feme::getEnvironmentForShaderStage(It->second.Stage)));
    addPassthroughAttribute(Func, feme::getShaderStageAttrName(),
                            feme::getShaderStageName(It->second.Stage));
    if (!It->second.LocalSize.empty())
      addPassthroughAttribute(Func, "hlsl.numthreads", It->second.LocalSize);
    // A tessellation-evaluation entry point's `Domain`/`Partitioning`/
    // `OutputPrimitive` (SPIR-V's `Triangles`/`Quads`/`Isolines`,
    // `SpacingEqual`/`SpacingFractionalEven`/`SpacingFractionalOdd`, and
    // `VertexOrderCw`/`VertexOrderCcw`/`PointMode` execution modes) always
    // arrive together -- the SPIR-V spec requires exactly one domain and
    // one spacing mode, and (per the fixup loop above) an output primitive
    // is always derived, explicitly or by the isoline default. A
    // tessellation-control entry point's `OutputVertices` is a wholly
    // separate execution mode, attached independently below, so that
    // `feme::graphics::getTessellationState` can read it off that stage's
    // own entry point rather than only ever seeing it alongside fields no
    // control-stage entry point ever declares.
    if (It->second.TessDomain && It->second.TessPartitioning &&
        It->second.TessOutputPrimitive) {
      addPassthroughAttribute(Func,
                              feme::graphics::getTessellationDomainAttrName(),
                              formatTessellatorDomain(*It->second.TessDomain));
      addPassthroughAttribute(
          Func, feme::graphics::getTessellationPartitioningAttrName(),
          formatTessPartitioning(*It->second.TessPartitioning));
      addPassthroughAttribute(
          Func, feme::graphics::getTessellationOutputPrimitiveAttrName(),
          formatTessellationOutputPrimitive(*It->second.TessOutputPrimitive));
    }
    if (It->second.TessOutputControlPointCount)
      addPassthroughAttribute(
          Func,
          feme::graphics::getTessellationOutputControlPointCountAttrName(),
          std::to_string(*It->second.TessOutputControlPointCount));
    // (Roadmap H5a) A geometry entry point's input/output primitive class
    // and maximum output vertex count always arrive together -- the SPIR-V
    // spec requires exactly one of each per geometry entry point -- so
    // `feme::graphics::getGeometryState` can rely on seeing all three or
    // none. `Invocations` is attached independently: unlike the other
    // three, it has a valid default (1) an entry point need not declare
    // explicitly, so its own attribute's absence is meaningful (see
    // `EntryPointInfo::GeometryInvocations`'s own comment) rather than a
    // sign of a malformed module.
    if (It->second.GeometryInput && It->second.GeometryOutput &&
        It->second.GeometryMaxOutputVertices) {
      addPassthroughAttribute(
          Func, feme::graphics::getGeometryInputPrimitiveAttrName(),
          formatGeometryInputPrimitive(*It->second.GeometryInput));
      addPassthroughAttribute(
          Func, feme::graphics::getGeometryOutputPrimitiveAttrName(),
          formatGeometryOutputPrimitive(*It->second.GeometryOutput));
      addPassthroughAttribute(
          Func, feme::graphics::getGeometryMaxOutputVerticesAttrName(),
          std::to_string(*It->second.GeometryMaxOutputVertices));
      addPassthroughAttribute(Func,
                              feme::graphics::getGeometryInvocationsAttrName(),
                              std::to_string(It->second.GeometryInvocations));
    }
    // (Roadmap H6a) A mesh entry point's output topology, maximum
    // emitted vertex count and maximum emitted primitive count always
    // arrive together -- the SPIR-V spec requires exactly one of each per
    // mesh entry point -- so `feme::graphics::getMeshState` can rely on
    // seeing all three or none.
    if (It->second.MeshTopology && It->second.MeshMaxOutputVertices &&
        It->second.MeshMaxOutputPrimitives) {
      addPassthroughAttribute(
          Func, feme::graphics::getMeshOutputTopologyAttrName(),
          formatMeshOutputTopology(*It->second.MeshTopology));
      addPassthroughAttribute(
          Func, feme::graphics::getMeshMaxOutputVerticesAttrName(),
          std::to_string(*It->second.MeshMaxOutputVertices));
      addPassthroughAttribute(
          Func, feme::graphics::getMeshMaxOutputPrimitivesAttrName(),
          std::to_string(*It->second.MeshMaxOutputPrimitives));
    }
    // A function containing any `llvm.experimental.constrained.*` intrinsic
    // call -- which `FloatControlArithmeticPattern`
    // (SPIRVToLLVMPatterns.cpp) emits for this entry point's
    // `RoundingModeRTZ`-width arithmetic (roadmap F15a) or for one
    // instruction's own non-default `FPRoundingMode` decoration (roadmap
    // F15c) -- must itself carry `strictfp`, or LLVM's verifier rejects the
    // module.
    if (!It->second.RoundingModeRTZWidths.empty() ||
        It->second.NeedsStrictFPForPerInstructionRounding)
      addBarePassthroughAttribute(Func, "strictfp");
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
  feme::spirv::ResourceInfoMap Resources;
  feme::spirv::StageIOInfoMap StageIOVariables;
  feme::spirv::FloatControlInfoMap RoundingModeRTZWidths;
  feme::spirv::FloatControlInfoMap DenormFlushToZeroWidths;
  feme::spirv::FastMathDefaultMap FastMathDefaults;
  unsigned Index = 0;
  for (mlir::Operation &Op :
       llvm::make_early_inc_range(Module.getBody()->getOperations())) {
    if (auto SPIRVModule = mlir::dyn_cast<mlir::spirv::ModuleOp>(Op)) {
      SPIRVModuleInfo Info{
          Index, feme::spirv::getTargetTriple(SPIRVModule), {}};
      if (mlir::failed(collectEntryPoints(SPIRVModule, Info.TargetTriple,
                                          Info.EntryPoints)))
        return signalPassFailure();
      // The conversion patterns below key their own lookup by function
      // symbol, which collectEntryPoints has already recovered per entry
      // point; merge it into one map across every `spirv.module` up front
      // rather than re-deriving it from the converted `llvm` dialect module
      // later on.
      for (auto &EntryPoint : Info.EntryPoints) {
        if (!EntryPoint.second.RoundingModeRTZWidths.empty())
          RoundingModeRTZWidths[EntryPoint.getKey()] =
              EntryPoint.second.RoundingModeRTZWidths;
        if (!EntryPoint.second.DenormFlushToZeroWidths.empty())
          DenormFlushToZeroWidths[EntryPoint.getKey()] =
              EntryPoint.second.DenormFlushToZeroWidths;
        if (!EntryPoint.second.FastMathDefaults.empty())
          FastMathDefaults[EntryPoint.getKey()] =
              EntryPoint.second.FastMathDefaults;
      }
      Modules.push_back(std::move(Info));
      // Materializes the resource name strings the handle intrinsics refer
      // to, so it has to run before the conversion drops the declarations
      // those names come from.
      for (auto &Resource : feme::spirv::prepareResourceVariables(SPIRVModule))
        Resources[Resource.getKey()] = Resource.getValue();
      // Recovers each stage-IO variable's address space before the
      // conversion may have already dropped the declaration this reads by
      // the time its own uses are legalized (see
      // feme::spirv::prepareStageIOVariables).
      for (auto &StageIOVar : feme::spirv::prepareStageIOVariables(SPIRVModule))
        StageIOVariables[StageIOVar.getKey()] = StageIOVar.getValue();
    }
    ++Index;
  }

  mlir::LowerToLLVMOptions Options(Ctx);
  mlir::LLVMTypeConverter TypeConverter(Ctx, Options);
  mlir::populateSPIRVToLLVMTypeConversion(TypeConverter);
  feme::spirv::populateSPIRVToLLVMTargetTypeConversions(TypeConverter);

  mlir::RewritePatternSet Patterns(Ctx);
  mlir::populateSPIRVToLLVMModuleConversionPatterns(TypeConverter, Patterns);
  mlir::populateSPIRVToLLVMConversionPatterns(TypeConverter, Patterns);
  mlir::populateSPIRVToLLVMFunctionConversionPatterns(TypeConverter, Patterns);
  feme::spirv::populateSPIRVToLLVMTargetPatterns(
      TypeConverter, Patterns, Resources, StageIOVariables,
      RoundingModeRTZWidths, DenormFlushToZeroWidths, FastMathDefaults);

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
