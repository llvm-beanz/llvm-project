//===- DXSAOperand.td - DXSA operand attributes ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Attributes describing a fully decoded dialect operand.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSAOPERAND
#define FEME_DIALECT_DXSA_IR_DXSAOPERAND

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributeInterfaces.td"
include "mlir/IR/EnumAttr.td"
include "feme/Dialect/DXSA/IR/DXSADialect.td"

//===----------------------------------------------------------------------===//
// DXSA operand type
//===----------------------------------------------------------------------===//

def DXSA_OperandType_Temp                          : I32EnumAttrCase<"r",                          0>;
def DXSA_OperandType_Input                         : I32EnumAttrCase<"v",                          1>;
def DXSA_OperandType_Output                        : I32EnumAttrCase<"o",                          2>;
def DXSA_OperandType_IndexableTemp                 : I32EnumAttrCase<"x",                          3>;
def DXSA_OperandType_Immediate32                   : I32EnumAttrCase<"l",                          4>;
def DXSA_OperandType_Immediate64                   : I32EnumAttrCase<"d",                          5>;
def DXSA_OperandType_Sampler                       : I32EnumAttrCase<"s",                          6>;
def DXSA_OperandType_Resource                      : I32EnumAttrCase<"t",                          7>;
def DXSA_OperandType_ConstantBuffer                : I32EnumAttrCase<"cb",                         8>;
def DXSA_OperandType_ImmediateConstantBuffer       : I32EnumAttrCase<"icb",                        9>;
def DXSA_OperandType_Label                         : I32EnumAttrCase<"label",                     10>;
def DXSA_OperandType_InputPrimitiveId              : I32EnumAttrCase<"vPrim",                     11>;
def DXSA_OperandType_OutputDepth                   : I32EnumAttrCase<"oDepth",                    12>;
def DXSA_OperandType_Null                          : I32EnumAttrCase<"null",                      13>;
def DXSA_OperandType_Rasterizer                    : I32EnumAttrCase<"rasterizer",                14>;
def DXSA_OperandType_OutputCoverageMask            : I32EnumAttrCase<"oMask",                     15>;
def DXSA_OperandType_Stream                        : I32EnumAttrCase<"m",                         16>;
def DXSA_OperandType_FunctionBody                  : I32EnumAttrCase<"fb",                        17>;
def DXSA_OperandType_FunctionTable                 : I32EnumAttrCase<"ft",                        18>;
def DXSA_OperandType_Interface                     : I32EnumAttrCase<"fp",                        19>;
def DXSA_OperandType_FunctionInput                 : I32EnumAttrCase<"funcInput",                 20>;
def DXSA_OperandType_FunctionOutput                : I32EnumAttrCase<"funcOutput",                21>;
def DXSA_OperandType_OutputControlPointId          : I32EnumAttrCase<"vOutputControlPointID",     22>;
def DXSA_OperandType_InputForkInstanceId           : I32EnumAttrCase<"vForkInstanceID",           23>;
def DXSA_OperandType_InputJoinInstanceId           : I32EnumAttrCase<"vJoinInstanceID",           24>;
def DXSA_OperandType_InputControlPoint             : I32EnumAttrCase<"vicp",                      25>;
def DXSA_OperandType_OutputControlPoint            : I32EnumAttrCase<"vocp",                      26>;
def DXSA_OperandType_InputPatchConstant            : I32EnumAttrCase<"vpc",                       27>;
def DXSA_OperandType_InputDomainPoint              : I32EnumAttrCase<"vDomain",                   28>;
def DXSA_OperandType_ThisPointer                   : I32EnumAttrCase<"thisPtr",                   29>;
def DXSA_OperandType_Uav                           : I32EnumAttrCase<"u",                         30>;
def DXSA_OperandType_ThreadGroupSharedMemory       : I32EnumAttrCase<"g",                         31>;
def DXSA_OperandType_InputThreadId                 : I32EnumAttrCase<"vThreadID",                 32>;
def DXSA_OperandType_InputThreadGroupId            : I32EnumAttrCase<"vThreadGroupID",            33>;
def DXSA_OperandType_InputThreadIdInGroup          : I32EnumAttrCase<"vThreadIDInGroup",          34>;
def DXSA_OperandType_InputCoverageMask             : I32EnumAttrCase<"vCoverage",                 35>;
def DXSA_OperandType_InputThreadIdInGroupFlattened : I32EnumAttrCase<"vThreadIDInGroupFlattened", 36>;
def DXSA_OperandType_InputGsInstanceId             : I32EnumAttrCase<"vGSInstanceID",             37>;
def DXSA_OperandType_OutputDepthGe                 : I32EnumAttrCase<"oDepthGE",                  38>;
def DXSA_OperandType_OutputDepthLe                 : I32EnumAttrCase<"oDepthLE",                  39>;
def DXSA_OperandType_CycleCounter                  : I32EnumAttrCase<"cycleCounter",              40>;
def DXSA_OperandType_OutputStencilRef              : I32EnumAttrCase<"oStencilRef",               41>;
def DXSA_OperandType_InnerCoverage                 : I32EnumAttrCase<"vInnerCoverage",            42>;

def DXSA_OperandType : I32EnumAttr<
    "OperandType", "operand type", [
      DXSA_OperandType_Temp,
      DXSA_OperandType_Input,
      DXSA_OperandType_Output,
      DXSA_OperandType_IndexableTemp,
      DXSA_OperandType_Immediate32,
      DXSA_OperandType_Immediate64,
      DXSA_OperandType_Sampler,
      DXSA_OperandType_Resource,
      DXSA_OperandType_ConstantBuffer,
      DXSA_OperandType_ImmediateConstantBuffer,
      DXSA_OperandType_Label,
      DXSA_OperandType_InputPrimitiveId,
      DXSA_OperandType_OutputDepth,
      DXSA_OperandType_Null,
      DXSA_OperandType_Rasterizer,
      DXSA_OperandType_OutputCoverageMask,
      DXSA_OperandType_Stream,
      DXSA_OperandType_FunctionBody,
      DXSA_OperandType_FunctionTable,
      DXSA_OperandType_Interface,
      DXSA_OperandType_FunctionInput,
      DXSA_OperandType_FunctionOutput,
      DXSA_OperandType_OutputControlPointId,
      DXSA_OperandType_InputForkInstanceId,
      DXSA_OperandType_InputJoinInstanceId,
      DXSA_OperandType_InputControlPoint,
      DXSA_OperandType_OutputControlPoint,
      DXSA_OperandType_InputPatchConstant,
      DXSA_OperandType_InputDomainPoint,
      DXSA_OperandType_ThisPointer,
      DXSA_OperandType_Uav,
      DXSA_OperandType_ThreadGroupSharedMemory,
      DXSA_OperandType_InputThreadId,
      DXSA_OperandType_InputThreadGroupId,
      DXSA_OperandType_InputThreadIdInGroup,
      DXSA_OperandType_InputCoverageMask,
      DXSA_OperandType_InputThreadIdInGroupFlattened,
      DXSA_OperandType_InputGsInstanceId,
      DXSA_OperandType_OutputDepthGe,
      DXSA_OperandType_OutputDepthLe,
      DXSA_OperandType_CycleCounter,
      DXSA_OperandType_OutputStencilRef,
      DXSA_OperandType_InnerCoverage
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

//===----------------------------------------------------------------------===//
// DXSA component mask (destination writemask)
//===----------------------------------------------------------------------===//

def DXSA_ComponentMask_X : I32BitEnumAttrCaseBit<"x", 0>;
def DXSA_ComponentMask_Y : I32BitEnumAttrCaseBit<"y", 1>;
def DXSA_ComponentMask_Z : I32BitEnumAttrCaseBit<"z", 2>;
def DXSA_ComponentMask_W : I32BitEnumAttrCaseBit<"w", 3>;

def DXSA_ComponentMask : I32BitEnumAttr<
    "ComponentMask", "destination writemask (subset of x, y, z, w)", [
      DXSA_ComponentMask_X,
      DXSA_ComponentMask_Y,
      DXSA_ComponentMask_Z,
      DXSA_ComponentMask_W
    ]> {
  let separator = ", ";
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_ComponentMaskAttr :
    EnumAttr<DXSADialect, DXSA_ComponentMask, "component_mask"> {
  let summary = "destination writemask (subset of x, y, z, w)";
  let description = [{
    The `#dxsa.component_mask` attribute is the destination writemask
    of a 4-component operand. It records which components of the
    4-component register are written by the instruction, as a
    non-empty subset of `{x, y, z, w}` in canonical order.

    Each entry names one component of the register:

    - `x` enables writing the first (X) component.
    - `y` enables writing the second (Y) component.
    - `z` enables writing the third (Z) component.
    - `w` enables writing the fourth (W) component.

    Combinations are written as a comma-separated list and always
    print in canonical `x, y, z, w` order, regardless of how the
    bits were set in the source binary.

    Example:

    ```mlir
    <x, y, z, w>   // write all components
    <x, y, z>      // write three components, fourth left unchanged
    <x>            // scalar write into the first component
    ```
  }];
  let assemblyFormat = "`<` $value `>`";
}

//===----------------------------------------------------------------------===//
// DXSA Swizzle attribute (ordered component selection)
//===----------------------------------------------------------------------===//

def DXSA_SwizzleAttr : AttrDef<DXSADialect, "Swizzle"> {
  let mnemonic = "swizzle";
  let summary = "source component selector (1 or 4 of x, y, z, w)";
  let description = [{
    The `#dxsa.swizzle` attribute is an ordered component selector on
    a 4-component source operand. It is a list of 1 or 4 components,
    each one of `x`, `y`, `z`, `w`. Entries may repeat.

    Each entry names which source component is read at that position:

    - `x` reads the first (X) source component.
    - `y` reads the second (Y) source component.
    - `z` reads the third (Z) source component.
    - `w` reads the fourth (W) source component.

    The size of the list encodes the component-selection mode of the
    source:

    - 4 entries form a full swizzle. The list defines the mapping of
    destination components `x, y, z, w` onto source components, in
    that order.
    - 1 entry selects a single component. Inside source operand
    a 1-component list is a select and a 4-component list is a swizzle.

    Example:

    ```mlir
    <x, y, z, w>   // identity swizzle (no rearrangement)
    <y, x, w, z>   // swap pairs
    <x, x, x, x>   // broadcast first component to all destination components
    <x>            // single-component select
    ```
  }];
  let parameters = (ins ArrayRefParameter<"unsigned">:$components);
  let hasCustomAssemblyFormat = 1;
  let genVerifyDecl = 1;
}

//===----------------------------------------------------------------------===//
// DXSA source operand modifier
//===----------------------------------------------------------------------===//

def DXSA_OperandModifier_Neg    : I32EnumAttrCase<"neg",     1>;
def DXSA_OperandModifier_Abs    : I32EnumAttrCase<"abs",     2>;
def DXSA_OperandModifier_AbsNeg : I32EnumAttrCase<"abs_neg", 3>;

def DXSA_OperandModifier : I32EnumAttr<
    "OperandModifier", "source operand modifier", [
      DXSA_OperandModifier_Neg,
      DXSA_OperandModifier_Abs,
      DXSA_OperandModifier_AbsNeg
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_OperandModifierAttr :
    EnumAttr<DXSADialect, DXSA_OperandModifier, "operand_modifier"> {
  let summary = "source operand modifier (abs, neg, or both)";
  let description = [{
    The `#dxsa.operand_modifier` attribute is the source-operand
    modifier carried by an arithmetic instruction. Each case has a
    fixed spelling that wraps the source operand:

    - `|OPERAND|` (abs) yields the absolute value of the source.
    For float operations the sign is forced positive, including on
    INF; NaN inputs remain NaN, but the resulting NaN bit pattern
    is unspecified.
    - `-OPERAND` (neg) flips the sign of the source. For float
    operations the sign is flipped including on INF, and NaN is
    preserved. For integer operations the modifier yields the two's
    complement of the source.
    - `-|OPERAND|` (abs_neg) is the documented combination of the
    two: absolute is applied first, then negated, so the result is
    always negative.

    Example:

    ```mlir
    dxsa.add r<0>, -r<1>, r<2>
    dxsa.add r<0>, |r<1>|, r<2>
    dxsa.add r<0>, -|r<1>|, r<2>
    ```
  }];
  let assemblyFormat = "`<` $value `>`";
}

//===----------------------------------------------------------------------===//
// DXSA operand minimum precision
//===----------------------------------------------------------------------===//

def DXSA_OperandMinPrecision_Float16  : I32EnumAttrCase<"min16f",  1>;
def DXSA_OperandMinPrecision_Float2_8 : I32EnumAttrCase<"min2_8f", 2>;
def DXSA_OperandMinPrecision_SInt16   : I32EnumAttrCase<"min16i",  4>;
def DXSA_OperandMinPrecision_UInt16   : I32EnumAttrCase<"min16u",  5>;

def DXSA_OperandMinPrecision : I32EnumAttr<
    "OperandMinPrecision", "minimum precision hint on an operand", [
      DXSA_OperandMinPrecision_Float16,
      DXSA_OperandMinPrecision_Float2_8,
      DXSA_OperandMinPrecision_SInt16,
      DXSA_OperandMinPrecision_UInt16
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_OperandMinPrecisionAttr :
    EnumAttr<DXSADialect, DXSA_OperandMinPrecision, "operand_min_precision"> {
  let summary = "minimum precision hint on an operand";
  let description = [{
    The `#dxsa.operand_min_precision` attribute is the minimum
    precision hint carried by an operand. It declares the lowest
    precision at which the operation may execute; an implementation
    is free to run at any equal or higher precision.

    Each case also encodes the element type (float, signed integer,
    unsigned integer), so that type-neutral instructions like `mov`
    are unambiguous when a size change is involved. The four cases
    are:

    - `min16f` is at least 16-bit-per-component float.
    - `min2_8f` is at least 10-bit-per-component float, laid out as a
    2.8 mantissa/exponent split.
    - `min16i` is at least 16-bit-per-component signed integer.
    - `min16u` is at least 16-bit-per-component unsigned integer.

    The default precision for the shader model is encoded by
    the attribute being absent on the parent destination or source operand.

    Example:

    ```mlir
    dxsa.dcl_input v<0, min16f, <x, y, z, w>>
    dxsa.add r<0>, v<0, min16f, <x>>, r<2>
    ```
  }];
  let assemblyFormat = "`<` $value `>`";
}

//===----------------------------------------------------------------------===//
// DXSA operand component count
//===----------------------------------------------------------------------===//

def DXSA_OperandComponents_None     : I32EnumAttrCase<"none",     0>;
def DXSA_OperandComponents_Scalar   : I32EnumAttrCase<"scalar",   1>;
def DXSA_OperandComponents_Vector   : I32EnumAttrCase<"vector",   2>;
def DXSA_OperandComponents_Reserved : I32EnumAttrCase<"reserved", 3>;

def DXSA_OperandComponents : I32EnumAttr<
    "OperandComponents", "operand component count", [
      DXSA_OperandComponents_None,
      DXSA_OperandComponents_Scalar,
      DXSA_OperandComponents_Vector,
      DXSA_OperandComponents_Reserved
    ]> {
  let cppNamespace = "::feme::dxsa";
  let genSpecializedAttr = 0;
}

def DXSA_OperandComponentsAttr :
    EnumAttr<DXSADialect, DXSA_OperandComponents, "operand_components"> {
  let summary = "component count declared on an operand";
  let description = [{
    The `#dxsa.operand_components` attribute records the
    component-count declaration of an operand, i.e. how many
    components the operand exposes. The four cases are:

    - `none` is a zero-component operand, used for handles and
    built-in operands that carry no component data, such as
    `null`, `oDepth`, or `label`.
    - `scalar` is a one-component operand, used for system-value
    scalars such as `vPrim` or `vCoverage`.
    - `vector` is a four-component operand, the common case for
    general-purpose registers (`r`, `v`, `o`, etc.).
    - `reserved` is an "N-component" placeholder, documented as
    unused but retained for round-trip fidelity in case a binary
    uses it.

    Every operand type has a canonical component count. The attribute
    is omitted on the printed form of destination or source operand
    when its value matches the canonical one for that operand type.
    It is shown only when the operand overrides the canonical count.

    Example:

    ```mlir
    dxsa.dcl_input v<0, scalar>
    dxsa.dcl_input vPrim
    ```
  }];
  let assemblyFormat = "`<` $value `>`";
}

//===----------------------------------------------------------------------===//
// DXSA operand index slot (one entry of an operand's index list)
//===----------------------------------------------------------------------===//

def DXSA_IndexAttr : AttrDef<DXSADialect, "Index"> {
  let mnemonic = "index";
  let summary = "operand index slot (immediate, relative, or both)";
  let description = [{
    The `#dxsa.index` attribute is one slot of an operand's index
    list, i.e. one dimension of indexing into a register file or
    resource binding array.

    There are several ways to express the value of an index:

    - A 32-bit immediate constant.
    - A 64-bit immediate constant.
    - A relative operand: another register (typically `r#` or a
    statically-indexed `x#`) holding the value at run time.
    - A 32-bit immediate plus a relative operand, added at run time.
    - A 64-bit immediate plus a relative operand, added at run time.

    These five representations are stored here as a pair of
    optional fields: an integer immediate (`i32` or `i64`) and a
    relative nested source operand. At least one of the two is present.

    Example:

    ```mlir
    5
    5 : i64
    r<1, <x>>
    2 + r<1, <x>>
    2 : i64 + r<1, <x>>
    ```
  }];
  let parameters = (ins
      OptionalParameter<"::mlir::IntegerAttr">:$imm,
      OptionalParameter<"SrcOperandAttr">:$relative);
  let hasCustomAssemblyFormat = 1;
  let genVerifyDecl = 1;
}

def DXSA_OperandIndexAttr :
    ArrayOfAttr<DXSADialect, "OperandIndex", "operand_index",
                "IndexAttr"> {
  let summary = "ordered list of operand index slots, one per dimension";
  let description = [{
    The `#dxsa.operand_index` attribute is the ordered list of
    index slots making up the full index expression of an
    operand. There is one slot per index dimension. Arrays of
    1, 2, or 3 slots correspond to operands with one, two, or three
    levels of indexing.

    Example:

    ```mlir
    [0]
    [0, r<1, <x>>]
    [r<1, <x>>, 2, 3]
    ```
  }];
}

//===----------------------------------------------------------------------===//
// DXSA destination operand attribute
//===----------------------------------------------------------------------===//

def DXSA_DstOperandAttr : AttrDef<DXSADialect, "DstOperand"> {
  let mnemonic = "dst_operand";
  let summary = "decoded destination operand";
  let description = [{
    The `#dxsa.dst_operand` attribute describes an operand that is
    written by an instruction.

    The textual form is the operand-type keyword, optionally
    followed by an angle-bracketed body. The parameters print in
    this fixed order, with absent or canonical fields omitted:

    - `$type` is the operand-type keyword (`r`, `o`, `oDepth`,
    `null`, ...).
    - `$index` is the list of index slots, one per dimension.
    A single-entry list omits the brackets.
    - `$components` is the component-count declaration. Omitted
    when it equals the canonical count for the operand type.
    - `$minPrecision` is the minimum precision hint on the operand.
    - `$mask` is the destination writemask. 1 to 4 unique
    components are accepted, duplicates are rejected.

    When every body field is absent or canonical the entire body is
    dropped from the printed form, leaving just the operand-type
    keyword (e.g. `oDepth`, `null`).

    Example:

    ```mlir
    dxsa.dcl_output oDepth
    dxsa.dcl_output o<0>
    dxsa.dcl_input v<0, min16f>
    dxsa.add null<vector>, r<1>, r<2>
    dxsa.add r<0, <x, y>>, r<1>, r<2>
    ```
  }];
  let parameters = (ins
      EnumParameter<DXSA_OperandType>:$type,
      OptionalParameter<"OperandIndexAttr",
        "index slot list, one entry per dimension">:$index,
      AttrParameter<"OperandComponentsAttr",
        "component-count declaration">:$components,
      OptionalParameter<"OperandMinPrecisionAttr",
        "minimum precision hint">:$minPrecision,
      OptionalParameter<"ComponentMaskAttr",
        "destination writemask">:$mask);
  let skipDefaultBuilders = 1;
  let builders = [
    // Builder args after the required type are ordered by how often they
    // appear in a real-world HLSL shader corpus:
    // - index (almost all)
    // - mask (~40%)
    // - explicit components (~4%)
    // - minPrecision (~2%)
    AttrBuilder<(ins "OperandType":$type,
                     CArg<"OperandIndexAttr", "{}">:$index,
                     CArg<"ComponentMaskAttr", "{}">:$mask,
                     CArg<"OperandComponentsAttr", "{}">:$components,
                     CArg<"OperandMinPrecisionAttr", "{}">:$minPrecision), [{
      if (!components)
        components =
            OperandComponentsAttr::get($_ctxt, defaultComponentsFor(type));
      if (!mask && components.getValue() == OperandComponents::vector)
        mask = ComponentMaskAttr::get($_ctxt, ComponentMask::x |
                                                  ComponentMask::y |
                                                  ComponentMask::z |
                                                  ComponentMask::w);
      return $_get($_ctxt, type, index, components, minPrecision, mask);
    }]>
  ];
  let hasCustomAssemblyFormat = 1;
  let genVerifyDecl = 1;
}

//===----------------------------------------------------------------------===//
// DXSA source operand attribute
//===----------------------------------------------------------------------===//

def DXSA_SrcOperandAttr : AttrDef<DXSADialect, "SrcOperand"> {
  let mnemonic = "src_operand";
  let summary = "decoded source operand";
  let description = [{
    The `#dxsa.src_operand` attribute describes an operand that is
    read by an instruction.

    The textual form is the operand-type keyword, optionally
    preceded by a unary modifier and followed by an angle-bracketed
    body or, for the `l` and `d` operand types, a parenthesised
    immediate literal list. The parameters print in this fixed
    order, with absent or canonical fields omitted:

    - `$type` is the operand-type keyword (`r`, `v`, `cb`, `l`,
    `d`, ...).
    - `$index` is the list of index slots, one per dimension.
    A single-entry list omits the brackets.
    - `$components` is the component-count declaration. Omitted
    when it equals the canonical count for the operand type.
    - `$minPrecision` is the minimum precision hint on the operand.
    - `$nonUniform` marks a resource handle whose index is not
    uniform across the lockstep execution of the draw, printed
    as `nonuniform`.
    - `$swizzle` is the source component selection. A 1-component
    list means a single-component select; a 4-component list is an
    ordered swizzle (repetition allowed).
    - `$modifier`, when set, decorates the printed operand text:
    `neg` adds a leading minus (`-OPERAND`), `abs` wraps in pipes
    (`|OPERAND|`), `abs_neg` combines both (`-|OPERAND|`).
    - `$values32` is the immediate literal 32-bit payload used by
    the `l` operand type.
    - `$values64` is the immediate literal 64-bit payload used by
    the `d` operand type.

    When every body field is absent or canonical the entire body is
    dropped from the printed form, leaving just the operand-type
    keyword (e.g. `vPrim`, `null`).

    Example:

    ```mlir
    dxsa.add r<0>, vPrim<vector>, l(0x3F800000, 0x40000000, 0x40400000, 0x40800000)
    dxsa.add r<0>, -r<1, <x>>, r<2, <y, z, x, w>>
    dxsa.add r<0>, -|r<1, min16f, nonuniform>|, d(0x3FF0000000000000, 0x4000000000000000)
    ```
  }];
  let parameters = (ins
      EnumParameter<DXSA_OperandType>:$type,
      OptionalParameter<"OperandIndexAttr",
        "index slot list, one entry per dimension">:$index,
      AttrParameter<"OperandComponentsAttr",
        "component-count declaration">:$components,
      OptionalParameter<"OperandMinPrecisionAttr",
        "minimum precision hint">:$minPrecision,
      OptionalParameter<"::mlir::UnitAttr",
        "nonuniform-index marker for resource handles">:$nonUniform,
      OptionalParameter<"SwizzleAttr",
        "source component selection: 1-component select or 4-component swizzle"
        >:$swizzle,
      OptionalParameter<"OperandModifierAttr",
        "neg, abs or abs_neg modifier for the operand"
        >:$modifier,
      OptionalParameter<"::mlir::DenseI32ArrayAttr",
        "32-bit immediate literal payload (for the `l` operand type)"
        >:$values32,
      OptionalParameter<"::mlir::DenseI64ArrayAttr",
        "64-bit immediate literal payload (for the `d` operand type)"
        >:$values64);
  let builders = [
    // Register/resource operand. Builder args after the required type are
    // ordered by how often they appear in a real-world HLSL shader corpus:
    // - index (almost all)
    // - swizzle (~14%)
    // - explicit components (~4%)
    // - minPrecision (~2%)
    // - modifier (<1%)
    // - nonUniform (<0.1%)
    AttrBuilder<(ins "OperandType":$type,
                     CArg<"OperandIndexAttr", "{}">:$index,
                     CArg<"SwizzleAttr", "{}">:$swizzle,
                     CArg<"OperandComponentsAttr", "{}">:$components,
                     CArg<"OperandMinPrecisionAttr", "{}">:$minPrecision,
                     CArg<"OperandModifierAttr", "{}">:$modifier,
                     CArg<"::mlir::UnitAttr", "{}">:$nonUniform), [{
      if (!components)
        components =
            OperandComponentsAttr::get($_ctxt, defaultComponentsFor(type));
      if (!swizzle && components.getValue() == OperandComponents::vector)
        swizzle = SwizzleAttr::get($_ctxt, ArrayRef<unsigned>{0, 1, 2, 3});
      return $_get($_ctxt, type, index, components, minPrecision, nonUniform,
                   swizzle, modifier, ::mlir::DenseI32ArrayAttr(), ::mlir::DenseI64ArrayAttr());
    }]>,
    // Immediate operand. Both the operand type and the component count are
    // implied by the payload:
    // 32-bit payload means `l`
    // 64-bit payload means `d`
    // the element count fixes scalar vs vector (invalid counts are left for
    // the verifier to report).
    AttrBuilder<(ins "::mlir::DenseI32ArrayAttr":$values32,
                     "::mlir::DenseI64ArrayAttr":$values64,
                     CArg<"OperandModifierAttr", "{}">:$modifier), [{
      OperandType type = values32 ? OperandType::l : OperandType::d;
      auto count = values32 ? values32.size() : values64.size();
      auto components =
          immComponentsFor(type, count);
      auto componentsAttr = OperandComponentsAttr::get(
          $_ctxt, components.value_or(OperandComponents::vector));
      return $_get($_ctxt, type, OperandIndexAttr(), componentsAttr,
                   OperandMinPrecisionAttr(), UnitAttr(), SwizzleAttr(),
                   modifier, values32, values64);
    }]>
  ];
  let hasCustomAssemblyFormat = 1;
  let genVerifyDecl = 1;
}

#endif // FEME_DIALECT_DXSA_IR_DXSAOPERAND
