//===- DXSAOps.td - DXSA operations definition -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef DXSA_OPS
#define DXSA_OPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"
include "feme/Dialect/DXSA/IR/DXSATypes.td"
include "feme/Dialect/DXSA/IR/DXSAFPArithOps.td"
include "feme/Dialect/DXSA/IR/DXSAIntArithOps.td"
include "feme/Dialect/DXSA/IR/DXSADoubleArithOps.td"
include "feme/Dialect/DXSA/IR/DXSAMoveOps.td"
include "feme/Dialect/DXSA/IR/DXSAConditionOps.td"
include "feme/Dialect/DXSA/IR/DXSAControlFlowOps.td"
include "feme/Dialect/DXSA/IR/DXSABitwiseOps.td"
include "feme/Dialect/DXSA/IR/DXSATopologyOps.td"
include "feme/Dialect/DXSA/IR/DXSATypeConversionOps.td"
include "feme/Dialect/DXSA/IR/DXSAAtomicOps.td"
include "feme/Dialect/DXSA/IR/DXSAResourceOps.td"
include "feme/Dialect/DXSA/IR/DXSARasterOps.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributeInterfaces.td"
include "mlir/IR/EnumAttr.td"

//===----------------------------------------------------------------------===//
// DXSA module - top-level container op for a DXBC tokenized program
//===----------------------------------------------------------------------===//

def DXSA_ProgramType_PixelShader         : I32EnumAttrCase<"pixel_shader",         0>;
def DXSA_ProgramType_VertexShader        : I32EnumAttrCase<"vertex_shader",        1>;
def DXSA_ProgramType_GeometryShader      : I32EnumAttrCase<"geometry_shader",      2>;
def DXSA_ProgramType_HullShader          : I32EnumAttrCase<"hull_shader",          3>;
def DXSA_ProgramType_DomainShader        : I32EnumAttrCase<"domain_shader",        4>;
def DXSA_ProgramType_ComputeShader       : I32EnumAttrCase<"compute_shader",       5>;
def DXSA_ProgramType_MeshShader          : I32EnumAttrCase<"mesh_shader",         13>;
def DXSA_ProgramType_AmplificationShader : I32EnumAttrCase<"amplification_shader",14>;

def DXSA_ProgramType : I32EnumAttr<
    "ProgramType", "DXBC tokenized program type", [
      DXSA_ProgramType_PixelShader,
      DXSA_ProgramType_VertexShader,
      DXSA_ProgramType_GeometryShader,
      DXSA_ProgramType_HullShader,
      DXSA_ProgramType_DomainShader,
      DXSA_ProgramType_ComputeShader,
      DXSA_ProgramType_MeshShader,
      DXSA_ProgramType_AmplificationShader
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_ProgramTypeAttr :
    EnumAttr<DXSADialect, DXSA_ProgramType, "program_type"> {
  let assemblyFormat = "$value";
}

def DXSA_ShaderVersionComponent :
    ConfinedAttr<I32Attr, [IntMinValue<0>, IntMaxValue<255>]>;

def DXSA_ModuleOp : DXSA_Op<"module", [
    IsolatedFromAbove, NoRegionArguments, NoTerminator, SingleBlock]> {
  let summary = "the top-level container for a shader program";
  let description = [{
    The `dxsa.module` operation models the top-level container of a single
    shader tokenized program (one SHEX section of the DXBC binary).

    The optional attributes are the shader program type and the major
    and minor version components. They are either all present (real
    binary with a SHEX header) or all absent (header-less raw token
    streams).

    Example:

    ```mlir
    // Binary content with a SHEX header
    dxsa.module pixel_shader 5 0 {
      dxsa.dcl_global_flags <refactoringAllowed>
    }

    // Binary content without a SHEX header
    dxsa.module {
      dxsa.dcl_global_flags <refactoringAllowed>
    }
    ```
  }];

  let arguments = (ins
    OptionalAttr<DXSA_ProgramTypeAttr>:$program_type,
    OptionalAttr<DXSA_ShaderVersionComponent>:$major_version,
    OptionalAttr<DXSA_ShaderVersionComponent>:$minor_version);
  let regions = (region SizedRegion<1>:$body);

  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;

  let extraClassDeclaration = [{
    ::mlir::Block *getBodyBlock() { return &getBody().front(); }
  }];
}

//===----------------------------------------------------------------------===//
// DXSA enum definitions
//===----------------------------------------------------------------------===//

// Global shader flags for 'dcl_globalFlags' instruction.
def DXSA_GlobalFlags_RefactoringAllowed            : I32BitEnumAttrCaseBit<"refactoringAllowed",            0>;
def DXSA_GlobalFlags_EnableDoublePrecisionFloatOps : I32BitEnumAttrCaseBit<"enableDoublePrecisionFloatOps", 1>;
def DXSA_GlobalFlags_ForceEarlyDepthStencil        : I32BitEnumAttrCaseBit<"forceEarlyDepthStencil",        2>;
def DXSA_GlobalFlags_EnableRawAndStructuredBuffers : I32BitEnumAttrCaseBit<"enableRawAndStructuredBuffers", 3>;
def DXSA_GlobalFlags_SkipOptimization              : I32BitEnumAttrCaseBit<"skipOptimization",              4>;
def DXSA_GlobalFlags_EnableMinimumPrecision        : I32BitEnumAttrCaseBit<"enableMinimumPrecision",        5>;
def DXSA_GlobalFlags_EnableDoubleExtensions        : I32BitEnumAttrCaseBit<"enableDoubleExtensions",        6>;
def DXSA_GlobalFlags_EnableShaderExtensions        : I32BitEnumAttrCaseBit<"enableShaderExtensions",        7>;
def DXSA_GlobalFlags_AllResourcesBound             : I32BitEnumAttrCaseBit<"allResourcesBound",             8>;

def DXSA_GlobalFlags : I32BitEnumAttr<
    "GlobalFlags", "global shader flags", [
      DXSA_GlobalFlags_RefactoringAllowed,
      DXSA_GlobalFlags_EnableDoublePrecisionFloatOps,
      DXSA_GlobalFlags_ForceEarlyDepthStencil,
      DXSA_GlobalFlags_EnableRawAndStructuredBuffers,
      DXSA_GlobalFlags_SkipOptimization,
      DXSA_GlobalFlags_EnableMinimumPrecision,
      DXSA_GlobalFlags_EnableDoubleExtensions,
      DXSA_GlobalFlags_EnableShaderExtensions,
      DXSA_GlobalFlags_AllResourcesBound
    ]> {
  let separator = "|";
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
  let printBitEnumPrimaryGroups = 1;
}

def DXSA_GlobalFlagsAttr :
    EnumAttr<DXSADialect, DXSA_GlobalFlags, "global_flags"> {
  let assemblyFormat = "`<` $value `>`";
}

def DXSA_TessellatorDomain_Isoline : I32EnumAttrCase<"domain_isoline", 1>;
def DXSA_TessellatorDomain_Tri     : I32EnumAttrCase<"domain_tri",     2>;
def DXSA_TessellatorDomain_Quad    : I32EnumAttrCase<"domain_quad",    3>;

def DXSA_TessellatorDomain : I32EnumAttr<
    "TessellatorDomain", "tessellator domain", [
      DXSA_TessellatorDomain_Isoline,
      DXSA_TessellatorDomain_Tri,
      DXSA_TessellatorDomain_Quad
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_TessellatorDomainAttr :
    EnumAttr<DXSADialect, DXSA_TessellatorDomain, "tessellator_domain"> {
  let assemblyFormat = "$value";
}

def DXSA_TessellatorOutputPrimitiveType_OutputPoint       : I32EnumAttrCase<"output_point",        1>;
def DXSA_TessellatorOutputPrimitiveType_OutputLine        : I32EnumAttrCase<"output_line",         2>;
def DXSA_TessellatorOutputPrimitiveType_OutputTriangleCw  : I32EnumAttrCase<"output_triangle_cw",  3>;
def DXSA_TessellatorOutputPrimitiveType_OutputTriangleCcw : I32EnumAttrCase<"output_triangle_ccw", 4>;

def DXSA_TessellatorOutputPrimitiveType : I32EnumAttr<
    "TessellatorOutputPrimitiveType", "tessellator output primitive type", [
      DXSA_TessellatorOutputPrimitiveType_OutputPoint,
      DXSA_TessellatorOutputPrimitiveType_OutputLine,
      DXSA_TessellatorOutputPrimitiveType_OutputTriangleCw,
      DXSA_TessellatorOutputPrimitiveType_OutputTriangleCcw
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_TessellatorOutputPrimitiveTypeAttr :
    EnumAttr<DXSADialect, DXSA_TessellatorOutputPrimitiveType,
             "tessellator_output_primitive_type"> {
  let assemblyFormat = "$value";
}

def DXSA_OutputPrimitiveTopology_PointList     : I32EnumAttrCase<"pointlist",     1>;
def DXSA_OutputPrimitiveTopology_LineStrip     : I32EnumAttrCase<"linestrip",     3>;
def DXSA_OutputPrimitiveTopology_TriangleStrip : I32EnumAttrCase<"trianglestrip", 5>;

def DXSA_OutputPrimitiveTopology : I32EnumAttr<
    "OutputPrimitiveTopology", "shader output primitive topology", [
      DXSA_OutputPrimitiveTopology_PointList,
      DXSA_OutputPrimitiveTopology_LineStrip,
      DXSA_OutputPrimitiveTopology_TriangleStrip
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_OutputPrimitiveTopologyAttr :
    EnumAttr<DXSADialect, DXSA_OutputPrimitiveTopology,
             "output_primitive_topology"> {
  let assemblyFormat = "$value";
}

def DXSA_TessellatorPartitioningMode_Integer        : I32EnumAttrCase<"partitioning_integer",         1>;
def DXSA_TessellatorPartitioningMode_Pow2           : I32EnumAttrCase<"partitioning_pow2",            2>;
def DXSA_TessellatorPartitioningMode_FractionalOdd  : I32EnumAttrCase<"partitioning_fractional_odd",  3>;
def DXSA_TessellatorPartitioningMode_FractionalEven : I32EnumAttrCase<"partitioning_fractional_even", 4>;

def DXSA_TessellatorPartitioningMode : I32EnumAttr<
    "TessellatorPartitioningMode", "tessellator partitioning mode", [
      DXSA_TessellatorPartitioningMode_Integer,
      DXSA_TessellatorPartitioningMode_Pow2,
      DXSA_TessellatorPartitioningMode_FractionalOdd,
      DXSA_TessellatorPartitioningMode_FractionalEven
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_TessellatorPartitioningModeAttr :
    EnumAttr<DXSADialect, DXSA_TessellatorPartitioningMode,
             "tessellator_partitioning_mode"> {
  let assemblyFormat = "$value";
}

def DXSA_InputPrimitive_Point        : I32EnumAttrCase<"point",         1>;
def DXSA_InputPrimitive_Line         : I32EnumAttrCase<"line",          2>;
def DXSA_InputPrimitive_Triangle     : I32EnumAttrCase<"triangle",      3>;
def DXSA_InputPrimitive_LineAdj      : I32EnumAttrCase<"line_adj",      6>;
def DXSA_InputPrimitive_TriangleAdj  : I32EnumAttrCase<"triangle_adj",  7>;
foreach i = !range(1, 33) in {
  def DXSA_InputPrimitive_Patch#i :
      I32EnumAttrCase<"patch" # i, !add(i, 7)>;
}

def DXSA_InputPrimitive : I32EnumAttr<
    "InputPrimitive", "geometry shader input primitive type",
    !listconcat(
      [DXSA_InputPrimitive_Point,
       DXSA_InputPrimitive_Line,
       DXSA_InputPrimitive_Triangle,
       DXSA_InputPrimitive_LineAdj,
       DXSA_InputPrimitive_TriangleAdj],
      !foreach(i, !range(1, 33),
        !cast<I32EnumAttrCase>("DXSA_InputPrimitive_Patch" # i)))> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_InputPrimitiveAttr :
    EnumAttr<DXSADialect, DXSA_InputPrimitive, "input_primitive"> {
  let assemblyFormat = "$value";
}

def DXSA_InterpolationMode_Constant                    : I32EnumAttrCase<"constant",                    1>;
def DXSA_InterpolationMode_Linear                      : I32EnumAttrCase<"linear",                      2>;
def DXSA_InterpolationMode_LinearCentroid              : I32EnumAttrCase<"linearCentroid",              3>;
def DXSA_InterpolationMode_LinearNoPerspective         : I32EnumAttrCase<"linearNoPerspective",         4>;
def DXSA_InterpolationMode_LinearNoPerspectiveCentroid : I32EnumAttrCase<"linearNoPerspectiveCentroid", 5>;
def DXSA_InterpolationMode_LinearSample                : I32EnumAttrCase<"linearSample",                6>;
def DXSA_InterpolationMode_LinearNoPerspectiveSample   : I32EnumAttrCase<"linearNoPerspectiveSample",   7>;

def DXSA_InterpolationMode : I32EnumAttr<
    "InterpolationMode", "pixel shader interpolation mode", [
      DXSA_InterpolationMode_Constant,
      DXSA_InterpolationMode_Linear,
      DXSA_InterpolationMode_LinearCentroid,
      DXSA_InterpolationMode_LinearNoPerspective,
      DXSA_InterpolationMode_LinearNoPerspectiveCentroid,
      DXSA_InterpolationMode_LinearSample,
      DXSA_InterpolationMode_LinearNoPerspectiveSample
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_InterpolationModeAttr :
    EnumAttr<DXSADialect, DXSA_InterpolationMode, "interpolation_mode"> {
  let assemblyFormat = "$value";
}

def DXSA_SystemValueName_Position                       : I32EnumAttrCase<"position",                       1>;
def DXSA_SystemValueName_ClipDistance                   : I32EnumAttrCase<"clipDistance",                   2>;
def DXSA_SystemValueName_CullDistance                   : I32EnumAttrCase<"cullDistance",                   3>;
def DXSA_SystemValueName_RenderTargetArrayIndex         : I32EnumAttrCase<"renderTargetArrayIndex",         4>;
def DXSA_SystemValueName_ViewportArrayIndex             : I32EnumAttrCase<"viewportArrayIndex",             5>;
def DXSA_SystemValueName_VertexId                       : I32EnumAttrCase<"vertexID",                       6>;
def DXSA_SystemValueName_PrimitiveId                    : I32EnumAttrCase<"primitiveID",                    7>;
def DXSA_SystemValueName_InstanceId                     : I32EnumAttrCase<"instanceID",                     8>;
def DXSA_SystemValueName_IsFrontFace                    : I32EnumAttrCase<"isFrontFace",                    9>;
def DXSA_SystemValueName_SampleIndex                    : I32EnumAttrCase<"sampleIndex",                   10>;
def DXSA_SystemValueName_FinalQuadUeq0EdgeTessFactor    : I32EnumAttrCase<"finalQuadUeq0EdgeTessFactor",   11>;
def DXSA_SystemValueName_FinalQuadVeq0EdgeTessFactor    : I32EnumAttrCase<"finalQuadVeq0EdgeTessFactor",   12>;
def DXSA_SystemValueName_FinalQuadUeq1EdgeTessFactor    : I32EnumAttrCase<"finalQuadUeq1EdgeTessFactor",   13>;
def DXSA_SystemValueName_FinalQuadVeq1EdgeTessFactor    : I32EnumAttrCase<"finalQuadVeq1EdgeTessFactor",   14>;
def DXSA_SystemValueName_FinalQuadUInsideTessFactor     : I32EnumAttrCase<"finalQuadUInsideTessFactor",    15>;
def DXSA_SystemValueName_FinalQuadVInsideTessFactor     : I32EnumAttrCase<"finalQuadVInsideTessFactor",    16>;
def DXSA_SystemValueName_FinalTriUeq0EdgeTessFactor     : I32EnumAttrCase<"finalTriUeq0EdgeTessFactor",    17>;
def DXSA_SystemValueName_FinalTriVeq0EdgeTessFactor     : I32EnumAttrCase<"finalTriVeq0EdgeTessFactor",    18>;
def DXSA_SystemValueName_FinalTriWeq0EdgeTessFactor     : I32EnumAttrCase<"finalTriWeq0EdgeTessFactor",    19>;
def DXSA_SystemValueName_FinalTriInsideTessFactor       : I32EnumAttrCase<"finalTriInsideTessFactor",      20>;
def DXSA_SystemValueName_FinalLineDetailTessFactor      : I32EnumAttrCase<"finalLineDetailTessFactor",     21>;
def DXSA_SystemValueName_FinalLineDensityTessFactor     : I32EnumAttrCase<"finalLineDensityTessFactor",    22>;
def DXSA_SystemValueName_Barycentrics                   : I32EnumAttrCase<"barycentrics",                  23>;
def DXSA_SystemValueName_ShadingRate                    : I32EnumAttrCase<"shadingRate",                   24>;
def DXSA_SystemValueName_CullPrimitive                  : I32EnumAttrCase<"cullPrimitive",                 25>;

def DXSA_SystemValueName : I32EnumAttr<
    "SystemValueName", "DXBC system interpreted/generated value name", [
      DXSA_SystemValueName_Position,
      DXSA_SystemValueName_ClipDistance,
      DXSA_SystemValueName_CullDistance,
      DXSA_SystemValueName_RenderTargetArrayIndex,
      DXSA_SystemValueName_ViewportArrayIndex,
      DXSA_SystemValueName_VertexId,
      DXSA_SystemValueName_PrimitiveId,
      DXSA_SystemValueName_InstanceId,
      DXSA_SystemValueName_IsFrontFace,
      DXSA_SystemValueName_SampleIndex,
      DXSA_SystemValueName_FinalQuadUeq0EdgeTessFactor,
      DXSA_SystemValueName_FinalQuadVeq0EdgeTessFactor,
      DXSA_SystemValueName_FinalQuadUeq1EdgeTessFactor,
      DXSA_SystemValueName_FinalQuadVeq1EdgeTessFactor,
      DXSA_SystemValueName_FinalQuadUInsideTessFactor,
      DXSA_SystemValueName_FinalQuadVInsideTessFactor,
      DXSA_SystemValueName_FinalTriUeq0EdgeTessFactor,
      DXSA_SystemValueName_FinalTriVeq0EdgeTessFactor,
      DXSA_SystemValueName_FinalTriWeq0EdgeTessFactor,
      DXSA_SystemValueName_FinalTriInsideTessFactor,
      DXSA_SystemValueName_FinalLineDetailTessFactor,
      DXSA_SystemValueName_FinalLineDensityTessFactor,
      DXSA_SystemValueName_Barycentrics,
      DXSA_SystemValueName_ShadingRate,
      DXSA_SystemValueName_CullPrimitive
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_SystemValueNameAttr :
    EnumAttr<DXSADialect, DXSA_SystemValueName, "system_value_name"> {
  let assemblyFormat = "`<` $value `>`";
}

def DXSA_ResourceDimension_Buffer           : I32EnumAttrCase<"buffer",           1>;
def DXSA_ResourceDimension_Texture1D        : I32EnumAttrCase<"texture1d",        2>;
def DXSA_ResourceDimension_Texture2D        : I32EnumAttrCase<"texture2d",        3>;
def DXSA_ResourceDimension_Texture2DMS      : I32EnumAttrCase<"texture2dms",      4>;
def DXSA_ResourceDimension_Texture3D        : I32EnumAttrCase<"texture3d",        5>;
def DXSA_ResourceDimension_TextureCube      : I32EnumAttrCase<"texturecube",      6>;
def DXSA_ResourceDimension_Texture1DArray   : I32EnumAttrCase<"texture1darray",   7>;
def DXSA_ResourceDimension_Texture2DArray   : I32EnumAttrCase<"texture2darray",   8>;
def DXSA_ResourceDimension_Texture2DMSArray : I32EnumAttrCase<"texture2dmsarray", 9>;
def DXSA_ResourceDimension_TextureCubeArray : I32EnumAttrCase<"texturecubearray", 10>;

def DXSA_ResourceDimension : I32EnumAttr<
    "ResourceDimension", "input resource dimension", [
      DXSA_ResourceDimension_Buffer,
      DXSA_ResourceDimension_Texture1D,
      DXSA_ResourceDimension_Texture2D,
      DXSA_ResourceDimension_Texture2DMS,
      DXSA_ResourceDimension_Texture3D,
      DXSA_ResourceDimension_TextureCube,
      DXSA_ResourceDimension_Texture1DArray,
      DXSA_ResourceDimension_Texture2DArray,
      DXSA_ResourceDimension_Texture2DMSArray,
      DXSA_ResourceDimension_TextureCubeArray
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_ResourceDimensionAttr :
    EnumAttr<DXSADialect, DXSA_ResourceDimension, "resource_dimension"> {
  let assemblyFormat = "$value";
}

// `float` is a reserved C++ keyword, so the symbolic (C++ enum case) names use
// PascalCase while the printable asm form stays lowercase.
def DXSA_ResourceReturnType_Unorm : I32EnumAttrCase<"Unorm", 1, "unorm">;
def DXSA_ResourceReturnType_Snorm : I32EnumAttrCase<"Snorm", 2, "snorm">;
def DXSA_ResourceReturnType_Sint  : I32EnumAttrCase<"Sint",  3, "sint">;
def DXSA_ResourceReturnType_Uint  : I32EnumAttrCase<"Uint",  4, "uint">;
def DXSA_ResourceReturnType_Float : I32EnumAttrCase<"Float", 5, "float">;

def DXSA_ResourceReturnType : I32EnumAttr<
    "ResourceReturnType", "input resource per-component return type", [
      DXSA_ResourceReturnType_Unorm,
      DXSA_ResourceReturnType_Snorm,
      DXSA_ResourceReturnType_Sint,
      DXSA_ResourceReturnType_Uint,
      DXSA_ResourceReturnType_Float
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_ResourceReturnTypeAttr :
    EnumAttr<DXSADialect, DXSA_ResourceReturnType, "resource_return_type"> {
  let assemblyFormat = "$value";
}

def DXSA_UAVFlags_GloballyCoherent          : I32BitEnumAttrCaseBit<"globallyCoherent",          0>;
def DXSA_UAVFlags_RasterizerOrdered         : I32BitEnumAttrCaseBit<"rasterizerOrdered",         1>;
def DXSA_UAVFlags_HasOrderPreservingCounter : I32BitEnumAttrCaseBit<"hasOrderPreservingCounter", 2>;

def DXSA_UAVFlags : I32BitEnumAttr<
    "UAVFlags", "UAV access flags", [
      DXSA_UAVFlags_GloballyCoherent,
      DXSA_UAVFlags_RasterizerOrdered,
      DXSA_UAVFlags_HasOrderPreservingCounter
    ]> {
  let separator = "|";
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
  let printBitEnumPrimaryGroups = 1;
}

def DXSA_UAVFlagsAttr :
    EnumAttr<DXSADialect, DXSA_UAVFlags, "uav_flags"> {
  let assemblyFormat = "$value";
}

def DXSA_SyncFlags_UavGlobal : I32BitEnumAttrCaseBit<"uav_global", 0>;
def DXSA_SyncFlags_UavGroup  : I32BitEnumAttrCaseBit<"uav_group",  1>;
def DXSA_SyncFlags_Tgsm      : I32BitEnumAttrCaseBit<"tgsm",       2>;
def DXSA_SyncFlags_Threads   : I32BitEnumAttrCaseBit<"threads",    3>;

def DXSA_SyncFlags : I32BitEnumAttr<
    "SyncFlags", "thread group sync and memory barrier flags", [
      DXSA_SyncFlags_UavGlobal,
      DXSA_SyncFlags_UavGroup,
      DXSA_SyncFlags_Tgsm,
      DXSA_SyncFlags_Threads
    ]> {
  let separator = "|";
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
  let printBitEnumPrimaryGroups = 1;
}

def DXSA_SyncFlagsAttr :
    EnumAttr<DXSADialect, DXSA_SyncFlags, "sync_flags"> {
  let assemblyFormat = "`<` $value `>`";
}

def DXSA_ConstantBufferAccessPattern_ImmediateIndexed : I32EnumAttrCase<"immediateIndexed", 0>;
def DXSA_ConstantBufferAccessPattern_DynamicIndexed   : I32EnumAttrCase<"dynamicIndexed",   1>;

def DXSA_ConstantBufferAccessPattern : I32EnumAttr<
    "ConstantBufferAccessPattern", "constant buffer access pattern", [
      DXSA_ConstantBufferAccessPattern_ImmediateIndexed,
      DXSA_ConstantBufferAccessPattern_DynamicIndexed
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_ConstantBufferAccessPatternAttr :
    EnumAttr<DXSADialect, DXSA_ConstantBufferAccessPattern,
             "constant_buffer_access_pattern"> {
  let assemblyFormat = "`<` $value `>`";
}

// `default` is a reserved C++ keyword, so the symbolic (C++ enum case) names
// use PascalCase while the printable asm form stays lowercase.
def DXSA_SamplerMode_Default    : I32EnumAttrCase<"Default",    0, "default">;
def DXSA_SamplerMode_Comparison : I32EnumAttrCase<"Comparison", 1, "comparison">;
def DXSA_SamplerMode_Mono       : I32EnumAttrCase<"Mono",       2, "mono">;

def DXSA_SamplerMode : I32EnumAttr<
    "SamplerMode", "sampler mode", [
      DXSA_SamplerMode_Default,
      DXSA_SamplerMode_Comparison,
      DXSA_SamplerMode_Mono
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_SamplerModeAttr :
    EnumAttr<DXSADialect, DXSA_SamplerMode, "sampler_mode"> {
  let assemblyFormat = "$value";
}

//===----------------------------------------------------------------------===//
// DXSA attribute Constraints
//===----------------------------------------------------------------------===//

// This constraint does not treat the most significant bit as a sign bit,
// so it is safe to use on unsigned integer attributes.
def DXSA_UIntNonZero : AttrConstraint<
    CPred<"!::llvm::cast<::mlir::IntegerAttr>($_self).getValue().isZero()">,
    "whose value is non-zero">;

//===----------------------------------------------------------------------===//
// DXSA op definitions
//===----------------------------------------------------------------------===//

def DXSA_Operand : DXSA_Op<"operand"> {
  let summary = "defines an operand of an instruction";
  let description = [{
    TODO
  }];

  let arguments = (ins Variadic<DXSA_IndexType>:$operands,
                       I32Attr:$type,
                       I32Attr:$num_components,
		       OptionalAttr<I32Attr>:$one,
		       OptionalAttr<I32Attr>:$mask,
		       OptionalAttr<I32ElementsAttr>:$swizzle,
		       OptionalAttr<I32Attr>:$modifier,
		       OptionalAttr<I32Attr>:$min_precision,
		       OptionalAttr<I32Attr>:$non_uniform);
  let results = (outs DXSA_LegacyOperandType:$operand);
  let assemblyFormat = "$operands attr-dict";
}

def DXSA_OperandImm : DXSA_Op<"operand.imm"> {
  let summary = "defines an immediate operand of an instruction";
  let description = [{
    TODO
  }];

  let arguments = (ins AnyAttrOf<[I32ElementsAttr, I64ElementsAttr]>:$imm);
  let results = (outs DXSA_LegacyOperandType:$operand);
  let assemblyFormat = "attr-dict";
}

def DXSA_IndexImm : DXSA_Op<"index.imm"> {
  let summary = "defines an immediate index";
  let description = [{
    TODO
  }];

  let arguments = (ins AnyAttrOf<[I32Attr, I64Attr]>:$imm);
  let results = (outs DXSA_IndexType:$index);
  let assemblyFormat = "attr-dict";
}

def DXSA_IndexRel : DXSA_Op<"index.rel"> {
  let summary = "defines an operand index";
  let description = [{
    TODO
  }];

  let arguments = (ins DXSA_LegacyOperandType:$operand);
  let results = (outs DXSA_IndexType:$index);
  let assemblyFormat = "$operand attr-dict";
}

def DXSA_IndexRelImm : DXSA_Op<"index.rel.imm"> {
  let summary = "defines an operand index with an immediate attribute";
  let description = [{
    TODO
  }];

  let arguments = (ins DXSA_LegacyOperandType:$operand, StrAttr:$op, I32Attr:$imm);
  let results = (outs DXSA_IndexType:$index);
  let assemblyFormat = "$operand attr-dict";
}

def DXSA_Instruction : DXSA_Op<"instruction"> {
  let summary = "defines an instruction with zero or more operands";
  let description = [{
    TODO
  }];

  let arguments = (ins Variadic<DXSA_LegacyOperandType>:$operands, StrAttr:$mnemonic);
  let results = (outs);
  let assemblyFormat = "$mnemonic $operands attr-dict";
}

def DXSA_Unknown : DXSA_Op<"unknown"> {
  let summary = "raw tokens fallback for an undecodable instruction";
  let description = [{
    The `dxsa.unknown` operation represents one instruction whose raw
    tokens could not be decoded into a structured op - unknown opcode,
    undecodable payload, length mismatch, truncated tail, etc. It acts
    as a disassembler-style fallback so the surrounding well-formed
    instructions still appear in IR.

    Example:

    ```mlir
    dxsa.unknown <tokens = [0x030000FF, 0xDEADBEEF, 0x12345678]>
    ```
  }];

  let arguments = (ins DenseI32ArrayAttr:$tokens);
  let results = (outs);
  let hasVerifier = 1;
  let assemblyFormat = "custom<HexTokens>($tokens) attr-dict";
}

def DXSA_Sync : DXSA_Op<"sync"> {
  let summary = "thread group sync and memory barrier";
  let description = [{
    The `dxsa.sync` operation issues a thread group barrier and/or memory
    fence. The flags select which memories are made visible and whether the
    threads of the group are synchronized:

    - `uav_global`: fence unordered access view (UAV) memory across the
      whole device.
    - `uav_group`: fence unordered access view (UAV) memory within the
      thread group.
    - `tgsm`: fence thread group shared memory.
    - `threads`: barrier across the threads of the group.

    `uav_global` and `uav_group` are mutually exclusive.

    Example:

    ```mlir
    dxsa.sync <uav_global|threads>
    ```
  }];
  let arguments = (ins DXSA_SyncFlagsAttr:$flags);
  let assemblyFormat = "$flags attr-dict";
  let hasVerifier = 1;
}

def DXSA_DclGlobalFlags : DXSA_Op<"dcl_global_flags"> {
  let summary = "declares global shader flags";
  let description = [{
    The `dxsa.dcl_global_flags` operation declares a set of flags that
    affect the entire shader.

    Example:

    ```mlir
    dxsa.dcl_global_flags <refactoringAllowed|forceEarlyDepthStencil>
    ```
  }];
  let arguments = (ins DXSA_GlobalFlagsAttr:$flags);
  let assemblyFormat = "$flags attr-dict";
}

def DXSA_DclTemps : DXSA_Op<"dcl_temps"> {
  let summary = "declares the number of temporary registers";
  let arguments = (ins ConfinedAttr<I32Attr,
      [IntPositive, IntMaxValue<4096>]>:$count);
  let assemblyFormat = [{ $count attr-dict }];
}

def DXSA_DclInputControlPointCount :
    DXSA_Op<"dcl_input_control_point_count"> {
  let summary = "declares the hull shader input control point count";
  let description = [{
    The `dxsa.dcl_input_control_point_count` operation declares the number of
    input control points for the hull shader.

    The count must be in [1, 32].

    Example:

    ```mlir
    dxsa.dcl_input_control_point_count 3
    ```
  }];
  let arguments = (ins ConfinedAttr<I32Attr,
      [IntPositive, IntMaxValue<32>]>:$count);
  let assemblyFormat = [{ $count attr-dict }];
}

def DXSA_DclOutputControlPointCount :
    DXSA_Op<"dcl_output_control_point_count"> {
  let summary = "declares the hull shader output control point count";
  let description = [{
    The `dxsa.dcl_output_control_point_count` operation declares the number
    of output control points for the hull shader.

    The count must be in [0, 32].

    Example:

    ```mlir
    dxsa.dcl_output_control_point_count 4
    ```
  }];
  let arguments = (ins ConfinedAttr<I32Attr,
      [IntNonNegative, IntMaxValue<32>]>:$count);
  let assemblyFormat = [{ $count attr-dict }];
}

def DXSA_DclTessellatorDomain : DXSA_Op<"dcl_tessellator_domain"> {
  let summary = "declares the tessellator domain";
  let description = [{
    The `dxsa.dcl_tessellator_domain` operation declares the tessellator
    domain.

    Example:

    ```mlir
    dxsa.dcl_tessellator_domain domain_quad
    ```
  }];

  let arguments = (ins DXSA_TessellatorDomainAttr:$domain);
  let assemblyFormat = "$domain attr-dict";
}

def DXSA_DclTessellatorOutputPrimitive
    : DXSA_Op<"dcl_tessellator_output_primitive"> {
  let summary = "declares the tessellator output primitive type";
  let description = [{
    The `dxsa.dcl_tessellator_output_primitive` operation declares the
    tessellator output primitive type.

    Example:

    ```mlir
    dxsa.dcl_tessellator_output_primitive output_triangle_cw
    ```
  }];
  let arguments = (ins DXSA_TessellatorOutputPrimitiveTypeAttr:$type);
  let assemblyFormat = "$type attr-dict";
}

def DXSA_DclOutputTopology : DXSA_Op<"dcl_output_topology"> {
  let summary = "declare what primitive topology the shader generates as output";
  let description = [{
    The `dxsa.dcl_output_topology` operation declares the primitive topology
    that the shader generates as output.

    Example:

    ```mlir
    dxsa.dcl_output_topology trianglestrip
    ```
  }];
  let arguments = (ins DXSA_OutputPrimitiveTopologyAttr:$topology);
  let assemblyFormat = "$topology attr-dict";
}

def DXSA_DclTessellatorPartitioning
    : DXSA_Op<"dcl_tessellator_partitioning"> {
  let summary = "declares the tessellator partitioning mode";
  let description = [{
    The `dxsa.dcl_tessellator_partitioning` operation declares the
    tessellator partitioning mode in a Hull Shader declaration section.

    Example:

    ```mlir
    dxsa.dcl_tessellator_partitioning partitioning_integer
    ```
  }];
  let arguments = (ins DXSA_TessellatorPartitioningModeAttr:$partitioningMode);
  let assemblyFormat = "$partitioningMode attr-dict";
}

def DXSA_DclInputPrimitive : DXSA_Op<"dcl_input_primitive"> {
  let summary = "declares the shader input primitive type";
  let description = [{
    The `dxsa.dcl_input_primitive` operation declares the input primitive
    type that the shader is invoked with.

    Example:

    ```mlir
    dxsa.dcl_input_primitive triangle
    ```
  }];
  let arguments = (ins DXSA_InputPrimitiveAttr:$type);
  let assemblyFormat = "$type attr-dict";
}

def DXSA_DclInputPs : DXSA_Op<"dcl_input_ps"> {
  let summary = "declares a pixel shader input";
  let description = [{
    The `dxsa.dcl_input_ps` operation declares a pixel shader input
    with the given attribute interpolation mode.

    Example:

    ```mlir
    dxsa.dcl_input_ps linear v<0, <x>>
    ```
  }];
  let arguments = (ins DXSA_InterpolationModeAttr:$mode,
                       DXSA_DstOperandAttr:$operand);
  let assemblyFormat = "$mode $operand attr-dict";
}

def DXSA_DclInputPsSiv : DXSA_Op<"dcl_input_ps_siv"> {
  let summary = "declares a pixel shader system-interpreted input";
  let description = [{
    The `dxsa.dcl_input_ps_siv` operation declares a pixel shader input
    that receives a System Interpreted Value, with the given attribute
    interpolation mode.

    Example:

    ```mlir
    dxsa.dcl_input_ps_siv linear v<0, <x>>, <position>
    ```
  }];
  let arguments = (ins DXSA_InterpolationModeAttr:$mode,
                       DXSA_DstOperandAttr:$operand,
                       DXSA_SystemValueNameAttr:$name);
  let assemblyFormat = "$mode $operand `,` $name attr-dict";
}

def DXSA_DclInputPsSgv : DXSA_Op<"dcl_input_ps_sgv"> {
  let summary = "declares a pixel shader system-generated input";
  let description = [{
    The `dxsa.dcl_input_ps_sgv` operation declares a pixel shader input
    that receives a System Generated Value.

    Example:

    ```mlir
    dxsa.dcl_input_ps_sgv v<0, <x>>, <sampleIndex>
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       DXSA_SystemValueNameAttr:$name);
  let assemblyFormat = "$operand `,` $name attr-dict";
}

def DXSA_DclInput : DXSA_Op<"dcl_input"> {
  let summary = "declares a shader input register";
  let description = [{
    The `dxsa.dcl_input` operation declares a shader input register.

    The register operand can be either an indexed input register
    or a special scalar input.

    Example:

    ```mlir
    dxsa.dcl_input v<0, <x>>
    dxsa.dcl_input vOutputControlPointID
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand);
  let assemblyFormat = "$operand attr-dict";
}

def DXSA_DclOutput : DXSA_Op<"dcl_output"> {
  let summary = "declares a shader output register";
  let description = [{
    The `dxsa.dcl_output` operation declares a shader output register.

    The register operand can be either an indexed output register
    or a special scalar output.

    Example:

    ```mlir
    dxsa.dcl_output o<0>
    dxsa.dcl_output oDepth
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand);
  let assemblyFormat = "$operand attr-dict";
}

def DXSA_DclIndexRange : DXSA_Op<"dcl_index_range"> {
  let summary = "declares a range of input or output registers to be indexed";
  let description = [{
    The `dxsa.dcl_index_range` operation declares a range of input or output
    registers to be indexed. `$operand` is the first register of the range
    and `$count` is the number of registers it spans.

    Example:

    ```mlir
    dxsa.dcl_index_range o<0, <x>>, 4
    dxsa.dcl_index_range vpc<7, <x, y, z>>, 3
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       ConfinedAttr<I32Attr, [IntPositive]>:$count);
  let assemblyFormat = "$operand `,` $count attr-dict";
  let hasVerifier = 1;
}

def DXSA_DclInputSgv : DXSA_Op<"dcl_input_sgv"> {
  let summary = "declares an input register as a System Generated Value";
  let description = [{
    The `dxsa.dcl_input_sgv` operation declares an input register
    that expects a System Generated Value to be provided
    from the upstream stage.

    Example:

    ```mlir
    dxsa.dcl_input_sgv v<0, <x>>, <vertexID>
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       DXSA_SystemValueNameAttr:$name);
  let assemblyFormat = "$operand `,` $name attr-dict";
}

def DXSA_DclInputSiv : DXSA_Op<"dcl_input_siv"> {
  let summary = "declares an input register as a System Interpreted Value";
  let description = [{
    The `dxsa.dcl_input_siv` operation declares an input register
    that expects a System Interpreted Value to be provided
    from the upstream Stage.

    Example:

    ```mlir
    dxsa.dcl_input_siv v<0>, <position>
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       DXSA_SystemValueNameAttr:$name);
  let assemblyFormat = "$operand `,` $name attr-dict";
}

def DXSA_DclOutputSgv : DXSA_Op<"dcl_output_sgv"> {
  let summary = "declares an output as a System Generated Value";
  let description = [{
    The `dxsa.dcl_output_sgv` operation declares an output to be written
    that represents a System Generated Value.

    Example:

    ```mlir
    dxsa.dcl_output_sgv o<0, <x>>, <primitiveID>
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       DXSA_SystemValueNameAttr:$name);
  let assemblyFormat = "$operand `,` $name attr-dict";
}

def DXSA_DclOutputSiv : DXSA_Op<"dcl_output_siv"> {
  let summary = "declares an output as a System Interpreted Value";
  let description = [{
    The `dxsa.dcl_output_siv` operation declares an output to be written
    that represents a System Interpreted Value.

    Example:

    ```mlir
    dxsa.dcl_output_siv o<0>, <position>
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       DXSA_SystemValueNameAttr:$name);
  let assemblyFormat = "$operand `,` $name attr-dict";
}

def DXSA_DclHsMaxTessFactor : DXSA_Op<"dcl_hs_max_tessfactor"> {
  let summary = "declares the MaxTessFactor for the patch";
  let description = [{
    The `dxsa.dcl_hs_max_tessfactor` operation declares the MaxTessFactor
    for the patch.

    The `$max_tessfactor` is a float32 value in the range [1.0, 64.0].

    Example:

    ```mlir
    dxsa.dcl_hs_max_tessfactor 4.200000e+01
    ```
  }];
  let arguments = (ins F32Attr:$max_tessfactor);
  let assemblyFormat = "$max_tessfactor attr-dict";
  let hasVerifier = 1;
}

def DXSA_DclHsJoinPhaseInstanceCount :
    DXSA_Op<"dcl_hs_join_phase_instance_count"> {
  let summary = "declares the Join Phase instance count";
  let description = [{
    The `dxsa.dcl_hs_join_phase_instance_count` operation declares
    the Join Phase instance count.

    The `$count` must be a positive 32-bit unsigned integer in the range [1, 2^32 - 1].

    Example:

    ```mlir
    dxsa.dcl_hs_join_phase_instance_count 42
    ```
  }];
  let arguments = (ins ConfinedAttr<UI32Attr, [DXSA_UIntNonZero]>:$count);
  let assemblyFormat = "$count attr-dict";
}

def DXSA_DclHsForkPhaseInstanceCount :
    DXSA_Op<"dcl_hs_fork_phase_instance_count"> {
  let summary = "declares the Fork Phase instance count";
  let description = [{
    The `dxsa.dcl_hs_fork_phase_instance_count` operation declares
    the Fork Phase instance count.

    The `$count` must be a positive 32-bit unsigned integer in the range [1, 2^32 - 1].

    Example:

    ```mlir
    dxsa.dcl_hs_fork_phase_instance_count 42
    ```
  }];
  let arguments = (ins ConfinedAttr<UI32Attr, [DXSA_UIntNonZero]>:$count);
  let assemblyFormat = "$count attr-dict";
}

//===----------------------------------------------------------------------===//
// Hull shader phase markers
//===----------------------------------------------------------------------===//

def DXSA_HsDecls : DXSA_NoOperandOp<"hs_decls"> {
  let summary = "marks the hull shader declaration section";
  let description = [{
    The `dxsa.hs_decls` operation marks the declaration section of a hull
    shader program.

    Example:

    ```mlir
    dxsa.hs_decls
    ```
  }];
}

def DXSA_HsControlPointPhase : DXSA_NoOperandOp<"hs_control_point_phase"> {
  let summary = "marks the hull shader control point phase";
  let description = [{
    The `dxsa.hs_control_point_phase` operation marks the control point phase
    of a hull shader program.

    Example:

    ```mlir
    dxsa.hs_control_point_phase
    ```
  }];
}

def DXSA_HsForkPhase : DXSA_NoOperandOp<"hs_fork_phase"> {
  let summary = "marks the hull shader fork phase";
  let description = [{
    The `dxsa.hs_fork_phase` operation marks the fork phase of a hull shader
    program.

    Example:

    ```mlir
    dxsa.hs_fork_phase
    ```
  }];
}

def DXSA_HsJoinPhase : DXSA_NoOperandOp<"hs_join_phase"> {
  let summary = "marks the hull shader join phase";
  let description = [{
    The `dxsa.hs_join_phase` operation marks the join phase of a hull shader
    program.

    Example:

    ```mlir
    dxsa.hs_join_phase
    ```
  }];
}

def DXSA_DclGsInstanceCount : DXSA_Op<"dcl_gs_instance_count"> {
  let summary = "declares instance count for the geometry shader";
  let description = [{
    The `dxsa.dcl_gs_instance_count` operation declares declares instance count
    for the geometry shader.

    The `$count` must be in [1, 32].

    Example:

    ```mlir
    dxsa.dcl_gs_instance_count 4
    ```
  }];
  let arguments = (ins ConfinedAttr<I32Attr,
      [IntPositive, IntMaxValue<32>]>:$count);
  let assemblyFormat = [{ $count attr-dict }];
}

def DXSA_DclMaxOutputVertexCount :
    DXSA_Op<"dcl_max_output_vertex_count"> {
  let summary = "declares the geometry shader maximum output vertex count";
  let description = [{
    The `dxsa.dcl_max_output_vertex_count` operation declares the maximum
    number of vertices that a single invocation of the geometry shader will
    emit.

    The count must be in [1, 1024].

    Example:

    ```mlir
    dxsa.dcl_max_output_vertex_count 42
    ```
  }];
  let arguments = (ins ConfinedAttr<I32Attr,
      [IntPositive, IntMaxValue<1024>]>:$count);
  let assemblyFormat = [{ $count attr-dict }];
}

def DXSA_DclTgsmRaw : DXSA_Op<"dcl_tgsm_raw"> {
  let summary = "declares a reference to a Thread Group Shared Memory region";
  let description = [{
    The `dxsa.dcl_tgsm_raw` operation declares a reference to a Thread Group Shared Memory region.

    The `$operand` is the `g#` register being declared.
    The `$byte_count$ is the size of the block of untyped shared memory;
    must be a multiple of 4.

    Example:

    ```mlir
    dxsa.dcl_tgsm_raw g<0>, 1024
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       ConfinedAttr<I32Attr,
                           [IntPositive, IntMaxValue<32768>]>:$byte_count);
  let assemblyFormat = "$operand `,` $byte_count attr-dict";
  let hasVerifier = 1;
}

def DXSA_DclTgsmStructured : DXSA_Op<"dcl_tgsm_structured"> {
  let summary = "declares a reference to a Thread Group Shared Memory region";
  let description = [{
    The `dxsa.dcl_tgsm_structured` operation declares a reference to a Thread Group Shared Memory region.
    The memory is viewed as an array of structures.

    The `$operand` is the `g#` register being declared.
    The `$struct_byte_stride` is a uint in bytes and must be a multiple of 4.
    The `$struct_count` is the number of structures.
    The total size `$struct_byte_stride * $struct_count` must not exceed
    32 KB.

    Example:

    ```mlir
    dxsa.dcl_tgsm_structured g<0>, 16, 64
    ```
  }];
  let arguments = (ins DXSA_DstOperandAttr:$operand,
                       ConfinedAttr<I32Attr,
                           [IntPositive, IntMaxValue<32768>]>:$struct_byte_stride,
                       ConfinedAttr<I32Attr,
                           [IntPositive, IntMaxValue<8192>]>:$struct_count);
  let assemblyFormat =
      "$operand `,` $struct_byte_stride `,` $struct_count attr-dict";
  let hasVerifier = 1;
}

def DXSA_DclImmediateConstantBuffer : DXSA_Op<"dcl_immediate_constant_buffer"> {
  let summary = "declares an immediate constant buffer";
  let description = [{
    The `dxsa.dcl_immediate_constant_buffer` operation declares
    an immediate constant buffer holding an array of 32-bit floating-point
    values grouped into 4-component vectors.

    Example:

    ```mlir
    dxsa.dcl_immediate_constant_buffer
                   [
                     1.000000e+00, 0.000000e+00, 0.000000e+00, 0.000000e+00,
                     0.000000e+00, 1.000000e+00, 0.000000e+00, 0.000000e+00,
                     0.000000e+00, 0.000000e+00, 1.000000e+00, 0.000000e+00,
                     0.000000e+00, 0.000000e+00, 0.000000e+00, 1.000000e+00
                   ]
    ```
  }];
  let arguments = (ins DenseF32ArrayAttr:$values);
  let assemblyFormat = "$values attr-dict";
}

def DXSA_DclConstantBuffer : DXSA_Op<"dcl_constant_buffer"> {
  let summary = "declares a constant buffer";
  let description = [{
    The `dxsa.dcl_constant_buffer` operation declares a constant buffer
    with its access pattern.

    Examples:

    ```mlir
    dxsa.dcl_constant_buffer <id = 0, size = 1>, <immediateIndexed>
    dxsa.dcl_constant_buffer <id = 0, size = 4, lbound = 0, ubound = 3, space = 1>, <dynamicIndexed>
    ```
  }];
  let arguments = (ins
      I32Attr:$id,
      I32Attr:$size,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space,
      DXSA_ConstantBufferAccessPatternAttr:$access_pattern);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id `,` `size` `=` $size
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)?
    `>` `,` $access_pattern attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclSampler : DXSA_Op<"dcl_sampler"> {
  let summary = "declares a sampler that will be referenced in the shader";
  let description = [{
    The `dxsa.dcl_sampler` operation declares sampler that will be referenced in the shader.

    Examples:

    ```mlir
    dxsa.dcl_sampler <id = 3, mode = default>
    dxsa.dcl_sampler <id = 0, mode = comparison, lbound = 0, ubound = 3, space = 1>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      DXSA_SamplerModeAttr:$mode,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id `,` `mode` `=` $mode
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)?
    `>` attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclStream : DXSA_GsStreamIndexOp<"dcl_stream"> {
  let summary = "declares a geometry shader (GS) output stream";
  let description = [{
    The `dxsa.dcl_stream` operation declares a geometry shader (GS) output stream.

    The `$index` must be in the range [0, 3].

    Example:

    ```mlir
    dxsa.dcl_stream 0
    ```
  }];
}

def DXSA_DclResource : DXSA_Op<"dcl_resource"> {
  let summary = "declares a shader input resource bound to a register";
  let description = [{
    The `dxsa.dcl_resource` operation declares a shader input resource
    bound to a register.

    Example:

    ```mlir
    dxsa.dcl_resource <id = 0>, <dim = buffer>,
        <x = unorm, y = snorm, z = sint, w = uint>
    dxsa.dcl_resource <id = 5>, <dim = texture2dms, sample_count = 4>,
        <x = float, y = float, z = float, w = float>
    dxsa.dcl_resource <id = 0, lbound = 0, ubound = 3, space = 1>,
        <dim = texture3d>, <x = float, y = float, z = float, w = float>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      DXSA_ResourceDimensionAttr:$dim,
      DXSA_ResourceReturnTypeAttr:$x,
      DXSA_ResourceReturnTypeAttr:$y,
      DXSA_ResourceReturnTypeAttr:$z,
      DXSA_ResourceReturnTypeAttr:$w,
      // A count of zero is how fxc spells a multisampled resource whose
      // sample count the shader never named.
      OptionalAttr<ConfinedAttr<I32Attr,
          [IntNonNegative, IntMaxValue<127>]>>:$sample_count,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)? `>` `,`
    `<` `dim` `=` $dim
         (`,` `sample_count` `=` $sample_count^)? `>` `,`
    `<` `x` `=` $x `,` `y` `=` $y `,` `z` `=` $z `,` `w` `=` $w `>`
    attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclResourceStructured : DXSA_Op<"dcl_resource_structured"> {
  let summary = "declares a structured buffer shader input resource bound to a register";
  let description = [{
    The `dxsa.dcl_resource_structured` operation declares a structured
    buffer shader input resource bound to a register.

    Example:

    ```mlir
    dxsa.dcl_resource_structured <id = 3, struct_byte_stride = 16>
    dxsa.dcl_resource_structured
        <id = 0, struct_byte_stride = 32, lbound = 0, ubound = 3, space = 1>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      ConfinedAttr<I32Attr, [IntPositive]>:$struct_byte_stride,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id `,` `struct_byte_stride` `=` $struct_byte_stride
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)? `>`
    attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclResourceRaw : DXSA_Op<"dcl_resource_raw"> {
  let summary = "declares a raw buffer shader input resource bound to a register";
  let description = [{
    The `dxsa.dcl_resource_raw` operation declares a raw buffer shader
    input resource bound to a register.

    Example:

    ```mlir
    dxsa.dcl_resource_raw <id = 3>
    dxsa.dcl_resource_raw <id = 0, lbound = 0, ubound = 3, space = 1>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)? `>`
    attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclUavTyped : DXSA_Op<"dcl_uav_typed"> {
  let summary = "declares a typed UnorderedAccessView (UAV) for use by a shader";
  let description = [{
    The `dxsa.dcl_uav_typed` operation declares a typed UnorderedAccessView (UAV) for use by a shader.

    Example:

    ```mlir
    dxsa.dcl_uav_typed <id = 0, dim = buffer>,
        <x = unorm, y = snorm, z = sint, w = uint>
    dxsa.dcl_uav_typed <id = 1, dim = texture2d>,
        <x = float, y = float, z = float, w = float>,
        <flags = globallyCoherent>
    dxsa.dcl_uav_typed <id = 0, dim = texture3d,
        lbound = 0, ubound = 3, space = 1>,
        <x = float, y = float, z = float, w = float>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      DXSA_ResourceDimensionAttr:$dim,
      DXSA_ResourceReturnTypeAttr:$x,
      DXSA_ResourceReturnTypeAttr:$y,
      DXSA_ResourceReturnTypeAttr:$z,
      DXSA_ResourceReturnTypeAttr:$w,
      OptionalAttr<DXSA_UAVFlagsAttr>:$flags,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id `,` `dim` `=` $dim
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)? `>` `,`
    `<` `x` `=` $x `,` `y` `=` $y `,` `z` `=` $z `,` `w` `=` $w `>`
    (`,` `<` `flags` `=` $flags^ `>`)?
    attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclUavRaw : DXSA_Op<"dcl_uav_raw"> {
  let summary = "declares a raw UnorderedAccessView (UAV) for use by a shader";
  let description = [{
    The `dxsa.dcl_uav_raw` operation declares a raw UnorderedAccessView (UAV) for use by a shader.

    Example:

    ```mlir
    dxsa.dcl_uav_raw <id = 0>
    dxsa.dcl_uav_raw <id = 1>, <flags = globallyCoherent>
    dxsa.dcl_uav_raw <id = 0, lbound = 0, ubound = 3, space = 1>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      OptionalAttr<DXSA_UAVFlagsAttr>:$flags,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)? `>`
    (`,` `<` `flags` `=` $flags^ `>`)?
    attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclUavStructured : DXSA_Op<"dcl_uav_structured"> {
  let summary = "declares a structured UnorderedAccessView (UAV) for use by a shader";
  let description = [{
    The `dxsa.dcl_uav_structured` operation declares a structured UnorderedAccessView (UAV) for use by a shader.

    `$struct_byte_stride` is the structure size in bytes; it must be a
    multiple of 4.

    Example:

    ```mlir
    dxsa.dcl_uav_structured <id = 0, struct_byte_stride = 16>
    dxsa.dcl_uav_structured <id = 1, struct_byte_stride = 32>,
        <flags = globallyCoherent|hasOrderPreservingCounter>
    dxsa.dcl_uav_structured <id = 0, struct_byte_stride = 32,
        lbound = 0, ubound = 3, space = 1>
    ```
  }];

  let arguments = (ins
      I32Attr:$id,
      ConfinedAttr<I32Attr, [IntPositive]>:$struct_byte_stride,
      OptionalAttr<DXSA_UAVFlagsAttr>:$flags,
      OptionalAttr<I32Attr>:$lbound,
      OptionalAttr<I32Attr>:$ubound,
      OptionalAttr<I32Attr>:$space);
  let assemblyFormat = [{
    ` ` `<` `id` `=` $id
            `,` `struct_byte_stride` `=` $struct_byte_stride
            (`,` `lbound` `=` $lbound^ `,` `ubound` `=` $ubound
             `,` `space` `=` $space)? `>`
    (`,` `<` `flags` `=` $flags^ `>`)?
    attr-dict
  }];
  let hasVerifier = 1;
}

def DXSA_DclIndexableTemp : DXSA_Op<"dcl_indexable_temp"> {
  let summary = "declares a temporary register array";
  let description = [{
    The `dxsa.dcl_indexable_temp` operation declares a temporary register array.
    Each temporary register array to be used in the shader must be declared.
    - `$id` identifies the `x#` register being declared.
    - `$size` is an integer that defines how many elements are in
    this array of 32-bit 4-component indexable temp storage that is being declared.
    - `$num_components` is the number of components (1..4) used per element;
    omitted from the textual form when equal to 4.

    Example:

    ```mlir
    dxsa.dcl_indexable_temp x<0>[1], 1
    dxsa.dcl_indexable_temp x<1>[23], 2
    dxsa.dcl_indexable_temp x<7>[4096]
    ```
  }];
  let arguments = (ins
      I32Attr:$id,
      ConfinedAttr<I32Attr, [IntPositive, IntMaxValue<4096>]>:$size,
      DefaultValuedAttr<
          ConfinedAttr<I32Attr, [IntPositive, IntMaxValue<4>]>,
          "4">:$num_components);
  let assemblyFormat = "`x` `<` $id `>` `` `[` $size `]` (`,` $num_components^)? attr-dict";
}

def DXSA_DclThreadGroup : DXSA_Op<"dcl_thread_group"> {
  let summary = "declares compute shader thread group dimensions";
  let description = [{
    The `dxsa.dcl_thread_group` operation declares the `$x`, `$y` and `$z`
    dimensions of a compute shader thread group.

    The product `$x` * `$y` * `$z` must not exceed 1024.

    Example:

    ```mlir
    dxsa.dcl_thread_group <x = 1, y = 1, z = 1>
    ```
  }];
  let arguments = (ins
      ConfinedAttr<I32Attr, [IntPositive, IntMaxValue<1024>]>:$x,
      ConfinedAttr<I32Attr, [IntPositive, IntMaxValue<1024>]>:$y,
      ConfinedAttr<I32Attr, [IntPositive, IntMaxValue<64>]>:$z);
  let assemblyFormat = "` ` `<` `x` `=` $x `,` `y` `=` $y `,` `z` `=` $z `>` attr-dict";
  let hasVerifier = 1;
}

def DXSA_Nop : DXSA_NoOperandOp<"nop"> {
  let summary = "no operation";
  let description = [{
    The `dxsa.nop` operation has no effect.

    Example:

    ```mlir
    dxsa.nop
    ```
  }];
}

#endif // DXSA_OPS
