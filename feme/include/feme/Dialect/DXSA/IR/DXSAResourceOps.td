//===- DXSAResourceOps.td - DXSA resource ops --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Resource instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSARESOURCEOPS
#define FEME_DIALECT_DXSA_IR_DXSARESOURCEOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.bufinfo
//===----------------------------------------------------------------------===//

def DXSA_BufInfo : DXSA_UnaryOp<"bufinfo"> {
  let summary = "query the element count on a Buffer (but not Constant Buffer)";
  let description = [{
    srcResource (`$src`) can be a Buffer (not a Constant Buffer) in an
    SRV (`t#`) or UAV (`u#`).

    All components in `$dst` receive the integer number of elements in the
    Buffer’s Shader Resource View. The number of elements depends on the
    view parameters such as memory format.

    For a Typed Buffer SRV or UAV, the return value is the number of elements
    in the View (where an element is one unit of the typed format).

    For a Raw Buffer SRV or UAV, the return value is the number of bytes in
    the view.

    For a Structured Buffer SRV or UAV, the return value is the number of
    structures in the view.

    Example:

    ```mlir
    dxsa.bufinfo r<0, <x>>, t<0, vector>
    dxsa.bufinfo r<0, <y>>, u<1, vector, <y, x, z, w>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.resinfo
//===----------------------------------------------------------------------===//

def DXSA_ResInfo : DXSA_BinaryOp<"resinfo"> {
  let summary = "query the dimensions of a given input resource.";
  let description = [{
    The `dxsa.resinfo` queries the dimensions of a given input resource.

    srcMipLevel (`$lhs`) is read as an unsigned integer scalar
    (so a single component selector is required for the
    source register, if it is not a scalar immediate value).
    srcResource (`$rhs`) is a `t#` or `u#` input texture for which the
    dimensions are being queried.
    `$dst` receives [width, height, depth or array size, total-mip-count],
    selected by the write mask.

    Returned values are all floating point, unless the `_uint` modifier is used,
    in which case the returned values are all integers. If the `_rcpFloat`
    modifier is used, all returned values are floating point, and the width,
    height and depth are returned as reciprocals (1.0f/width, 1.0f/height,
    1.0f/depth), including INF if width/height/depth are 0 (from out-of-range
    srcMipLevel behavior above). Note that the `_rcpFloat` modifier only applies
    to width, height, and depth returned values (and does not apply to values
    that are set to 0 and thus not returned, and also does not apply to array
    size returns).

    For more information, see Direct3D 11.3 Functional Specification, 22.4.14.

    Example:

    ```mlir
    dxsa.resinfo r<0>, r<1, <x>>, r<2>
    dxsa.resinfo r<0, <x>>, r<1, <x>>, r<2, <x, x, x, x>>
    ```
  }];
}

def DXSA_ResInfoUInt : DXSA_BinaryOp<"resinfo_uint"> {
  let summary = "query the dimensions of a given input resource.";
  let description = [{
    The `dxsa.resinfo_uint` queries the dimensions of a given input resource.
    Returned values are all integer.

    See `dxsa.resinfo` for more info.

    Example:

    ```mlir
    dxsa.resinfo_uint r<0>, r<1, <x>>, r<2>
    dxsa.resinfo_uint r<0, <x>>, r<1, <x>>, r<2, <x, x, x, x>>
    ```
  }];
}

def DXSA_ResInfoRcpFloat : DXSA_BinaryOp<"resinfo_rcpFloat"> {
  let summary = "query the dimensions of a given input resource.";
  let description = [{
    The `dxsa.resinfo_rcpFloat` queries the dimensions of a given input resource.
    All returned values are floating point, and the width, height and depth are
    returned as reciprocals (1.0f/width, 1.0f/height, 1.0f/depth),
    including INF if width/height/depth are 0 (from out-of-range srcMipLevel
    behavior above).
    Note that the `_rcpFloat` modifier only applies to width, height, and depth
    returned values (and does not apply to values that are set to 0 and thus
    not returned, and also does not apply to array size returns).

    See `dxsa.resinfo` for more info.

    Example:

    ```mlir
    dxsa.resinfo_rcpFloat r<0>, r<1, <x>>, r<2>
    dxsa.resinfo_rcpFloat r<0, <x>>, r<1, <x>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sampleinfo
//===----------------------------------------------------------------------===//

def DXSA_SampleInfo : DXSA_UnaryOp<"sampleinfo"> {
  let summary = "query the number of samples in a given shader resource view "
                "or in the rasterizer";
  let description = [{
    The `dxsa.sampleinfo` returns the number of samples for the given resource
    or the rasterizer. Only valid for resources that can be loaded using `ld2dms`
    unless the "rasterizer" is specified as `$src`.
    `$src` could be `t#` register (a shader resource view) or "rasterizer" register.

    The instruction computes the following vector (SampleCount,0,0,0).
    The swizzle on srcResource allows the returned values to be swizzled
    arbitrarily before they are written to the destination.
    If there is no resource bound to the specified slot, 0 is returned.
    Returned value is floating point.

    Example:

    ```mlir
    dxsa.sampleinfo r<0>, r<1>
    dxsa.sampleinfo o<0, <x, y>>, rasterizer<vector, <x>>
    ```
  }];
}

def DXSA_SampleInfoUInt : DXSA_UnaryOp<"sampleinfo_uint"> {
  let summary = "query the number of samples in a given shader resource view "
                "or in the rasterizer";
  let description = [{
    The `dxsa.sampleinfo_uint` returns the number of samples for the given resource
    or the rasterizer. Only valid for resources that can be loaded using `ld2dms`
    unless the "rasterizer" is specified as `$src`.
    `$src` could be `t#` register (a shader resource view) or "rasterizer" register.

    The instruction computes the following vector (SampleCount,0,0,0).
    The swizzle on srcResource allows the returned values to be swizzled
    arbitrarily before they are written to the destination.
    If there is no resource bound to the specified slot, 0 is returned.
    Returned value is integer.

    Example:

    ```mlir
    dxsa.sampleinfo_uint r<0>, r<1>
    dxsa.sampleinfo_uint o<0, <x, y>>, rasterizer<vector, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.samplepos
//===----------------------------------------------------------------------===//

def DXSA_SamplePos : DXSA_BinaryOp<"samplepos"> {
  let summary = "query the position of a sample in a given shader resource "
                "view or in the rasterizer.";
  let description = [{
    The `dxsa.samplepos` returns the 2D sample position of sample
    sampleIndex (`$rhs`) for the given resource. Only valid for resources that
    can be loaded using `ld2dms` unless the "rasterizer" is specified as
    srcResource (`$lhs`).

    srcResource could be t# register (a shader resource view) or "rasterizer" register.

    The instruction computes the following floating point vector (Xposition, Yposition, 0, 0).

    The swizzle on srcResource allows the returned values to be swizzled arbitrarily
    before they are written to the destination.
    The sample position is relative to the pixel's center, based on the Pixel Coordinate System (3.3.1).

    If sampleIndex is out of bounds a zero vector is returned.
    If there is no resource bound to the specified slot, 0 is returned.

    Example:

    ```mlir
    dxsa.samplepos r<0>, r<1>, l(0x2)
    dxsa.samplepos r<0>, r<1>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.eval_centroid
//===----------------------------------------------------------------------===//

def DXSA_EvalCentroid : DXSA_UnaryOp<"eval_centroid"> {
  let summary = "evaluate a pixel shader input at the centroid location";
  let description = [{
    The `dxsa.eval_centroid` operation samples the pixel shader input
    attribute `$src` at the centroid location within the pixel and writes
    the result to `$dst`.

    Example:

    ```mlir
    dxsa.eval_centroid r<0>, r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.eval_sample_index
//===----------------------------------------------------------------------===//

def DXSA_EvalSampleIndex : DXSA_BinaryOp<"eval_sample_index"> {
  let summary = "evaluate a pixel shader input at a given sample index";
  let description = [{
    The `dxsa.eval_sample_index` operation samples the pixel shader input
    attribute `$lhs` at the sample whose index is the scalar operand
    `$rhs` and writes the result to `$dst`. `$rhs` is either an integer
    immediate or a single-component register reference.

    Example:

    ```mlir
    dxsa.eval_sample_index r<0>, r<1>, l(0x2)
    dxsa.eval_sample_index r<0>, r<1>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.eval_snapped
//===----------------------------------------------------------------------===//

def DXSA_EvalSnapped : DXSA_BinaryOp<"eval_snapped"> {
  let summary = "evaluate a pixel shader input at a snapped pixel offset";
  let description = [{
    The `dxsa.eval_snapped` operation samples the pixel shader input
    attribute `$lhs` at a fractional pixel offset from the pixel center
    given by the int4 immediate operand `$rhs` (interpreted on a 16x16
    grid using the lowest 4 bits of its first two components) and writes
    the result to `$dst`.

    Example:

    ```mlir
    dxsa.eval_snapped r<0>, r<1>, l(0x2, 0x3, 0x0, 0x0)
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sample
//===----------------------------------------------------------------------===//

def DXSA_SampleOffsetAttr : AttrDef<DXSADialect, "SampleOffset"> {
  let mnemonic = "sample_offset";
  let summary = "immediate offset for the texture coordinates for the sample";
  let description = [{
    The dxsa.sample_offset attribute indicates that the texture coordinates for
    the sample are to be offset by a set of provided immediate texel space
    integer constant values. The literal values are a set of 4 bit 2's
    complement numbers, having integer range [-8,7].
  }];
  let parameters = (ins "int32_t":$u, "int32_t":$v, "int32_t":$w);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def DXSA_SampleClampFeedbackAttr : AttrDef<DXSADialect, "SampleClampFeedback"> {
  let mnemonic = "sample_clamp_feedback";
  let summary = "optional LOD clamp and Tiled Resources shader feedback status output value";
  let description = [{
    The dxsa.sample_clamp_feed attribute is an optional pair of operands for
    dxsa.sample instruction.

    `lod_clamp` is an additional 32 bit scalar LOD clamp operand.

    `feedback` is a shader feedback status output value. The contents
    of the return value are opaque - direct reading by the shader
    program is disallowed.
  }];

  let parameters = (ins
    AttrParameter<"SrcOperandAttr", "lod_clamp">:$lod_clamp,
    AttrParameter<"DstOperandAttr", "feedback">:$feedback);

  let assemblyFormat = "`<` $lod_clamp `,` $feedback `>`";
}

def DXSA_Sample : DXSA_Op<"sample"> {
  let summary = "sample data from the specified texture using the filtering mode identified by the given sampler";
  let description = [{
    The dxsa.sample operation uses provided address, sample data from the
    specified Element/texture using the filtering mode identified by the given
    sampler. The source data may come from any Resource Type (5), other than
    Buffers.

    `src_address` provides the set of texture coordinates needed to perform the
    sample, as floating point values referencing normalized space in the texture.

    `src_resource` is a texture register (t). This is simply a
    placeholder for a texture, including the return data type of the
    resource being sampled.

    `src_sampler` is a sampler register (s).  This is simply a
    placeholder for a collection of filtering controls (such as point
    vs. linear, mipmapping and address wrapping controls).

    The optional `offset` operand suffix (address offset by immediate
    integer) indicates that the texture coordinates for the sample are
    to be offset by a set of provided immediate texel space integer
    constant values. The literal values are a set of 4 bit 2's
    complement numbers, having integer range [-8,7].
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_SampleClampFeedback : DXSA_Op<"sample_cl_s"> {
  let summary = "same as `dxsa.sample`, but with an additional LOD clamp and status output";
  let description = [{
    `src_address`, `src_resource`, `src_sampler`, `offset` operands
    are the same as in `dxsa.sample` instruction.

    The `clamp_feedback` operand appends an additional 32 bit scalar
    LOD clamp operand and an additional 32 bit scalar Tiled Resources
    shader feedback status output value.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SampleClampFeedbackAttr:$clamp_feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $clamp_feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sample_b
//===----------------------------------------------------------------------===//

def DXSA_SampleB : DXSA_Op<"sample_b"> {
  let summary = "sample data from the specified texture with an additional bias applied to the LOD";
  let description = [{
    The `dxsa.sample_b` operation uses provided address, sample data from the
    specified Element/texture using the filtering mode identified by the given
    sampler. The source data may come from any Resource Type(5), other than
    Buffers. An additional bias is applied to the level of detail computed as
    part of the instruction execution.

    `src_address`, `src_resource`, `src_sampler` and `offset` operands
    are the same as in `dxsa.sample` instruction.

    The `src_lod_bias` value is added to the computed LOD on a per-pixel basis,
    along with the sampler MipLODBias value, prior to the clamp to MinLOD and
    MaxLOD.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_lod_bias,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_lod_bias
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_SampleBClampFeedback : DXSA_Op<"sample_b_cl_s"> {
  let summary = "same as `dxsa.sample_b`, but with an additional LOD clamp and status output";
  let description = [{
    `src_address`, `src_resource`, `src_sampler`, `src_lod_bias` and
    `offset` operands are the same as in `dxsa.sample_b` instruction.

    The `clamp_feedback` operand appends an additional 32 bit scalar
    LOD clamp operand and an additional 32 bit scalar Tiled Resources
    shader feedback status output value.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_lod_bias,
      DXSA_SampleClampFeedbackAttr:$clamp_feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_lod_bias `,` $clamp_feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sample_d
//===----------------------------------------------------------------------===//

def DXSA_SampleD : DXSA_Op<"sample_d"> {
  let summary = "sample data from the specified texture with derivatives for X and Y";
  let description = [{
    The `dxsa.sample_d` operation uses provided address, sample data from the
    specified Element/texture using the filtering mode identified by the given
    sampler. The source data may come from any Resource Type(5), other than
    Buffers.

    `src_address`, `src_resource`, `src_sampler` and `offset` operands
    are the same as in `dxsa.sample` instruction.

    Derivatives for the source address in the x direction and the y direction are
    provided by extra parameters, `src_x_derivatives` and `src_y_derivatives`,
    respectively. These derivatives are in normalized texture coordinate space.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_x_derivatives,
      DXSA_SrcOperandAttr:$src_y_derivatives,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler
    `,` $src_x_derivatives `,` $src_y_derivatives
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_SampleDClampFeedback : DXSA_Op<"sample_d_cl_s"> {
  let summary = "same as `dxsa.sample_d`, but with an additional LOD clamp and status output";
  let description = [{
    `dst`, `src`_address, `src`_resource, `src`_sampler,
    `src`_x_derivatives, `src`_y_derivatives and `offset` operands are
    the same as in `dxsa.sample_d` instruction.

    The `clamp_feedback` operand appends an additional 32 bit scalar
    LOD clamp operand and an additional 32 bit scalar Tiled Resources
    shader feedback status output value.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_x_derivatives,
      DXSA_SrcOperandAttr:$src_y_derivatives,
      DXSA_SampleClampFeedbackAttr:$clamp_feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler
    `,` $src_x_derivatives `,` $src_y_derivatives `,` $clamp_feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sample_l
//===----------------------------------------------------------------------===//

def DXSA_SampleL : DXSA_Op<"sample_l"> {
  let summary = "sample data from the specified texture with specific LOD";
  let description = [{
    `dxsa.sample_l` is identical to `dxsa.sample`, except that LOD is provided
    directly by the application as a scalar value, representing no anisotropy.

    `src_address`, `src_resource`, `src_sampler` and `offset` operands
    are the same as in `dxsa.sample` instruction.

    `src_lod` is the LOD value. If the LOD value is <= 0, the zero'th (biggest
    map) is chosen, with the magnify filter applied (if applicable based on the
    filter mode).  Since `src_lod` is a floating point value, the fractional
    value is used to interpolate (if the minify filter is LINEAR or with
    anisotropic filtering) between two mip levels.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_lod,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_lod
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_SampleLFeedback : DXSA_Op<"sample_l_cl_s"> {
  let summary = "same as `dxsa.sample_l`, but with an additional LOD clamp and status output";
  let description = [{
    `dst`, `src_address`, `src_resource`,`src_sampler`, `src_lod` and
    `offset` operands are the same as in `dxsa.sample_l` instruction.

    The `clamp_feedback` operand appends an additional 32 bit scalar
    LOD clamp operand and an additional 32 bit scalar Tiled Resources
    shader feedback status output value.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_lod,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_lod `,` $feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sample_c
//===----------------------------------------------------------------------===//

def DXSA_SampleC : DXSA_Op<"sample_c"> {
  let summary = "perform a comparison filter";
  let description = [{
    `dxsa.sample_c` is to provide a building-block for Percentage-Closer Depth
    filtering. The 'c' in sample_c stands for Comparison.

    The operands to `dxsa.sample_c` are identical to `dxsa.sample`, except that
    there is an additional float32 source operand, `src_reference_value`, which
    must be a register with single-component selected, or a scalar literal.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_reference_value
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_SampleCClampFeedback : DXSA_Op<"sample_c_cl_s"> {
  let summary = "same as `dxsa.sample_c`, but with an additional LOD clamp and status output";
  let description = [{
    `dst`,`src_address`,`src_resource`,`src_sampler`,`src_reference_value`
    and `offset operands are the same as in `dxsa.sample_c`
    instruction.

    The `clamp_feedback` operand appends an additional 32 bit scalar
    LOD clamp operand and an additional 32 bit scalar Tiled Resources
    shader feedback status output value.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      DXSA_SampleClampFeedbackAttr:$clamp_feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_reference_value `,` $clamp_feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sample_c_lz
//===----------------------------------------------------------------------===//

def DXSA_SampleCLZ : DXSA_Op<"sample_c_lz"> {
  let summary = "perform a comparison filter with zero LOD";
  let description = [{
    Same as `dxsa.sample_c`, except LOD is 0, and derivatives are ignored (as if
    they are 0).  The 'lz' stands for level-zero.  Because derivatives are
    ignored, this instruction is available in shaders other than the Pixel
    Shader.

    `dxsa.sample_c` is to provide a building-block for Percentage-Closer Depth
    filtering. The 'c' in sample_c stands for Comparison.

    The operands to `dxsa.sample_c_lz` are identical to `dxsa.sample_c`.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_reference_value
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_SampleCLZFeedback : DXSA_Op<"sample_c_lz_s"> {
  let summary = "same as `dxsa.sample_c_lz`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_resource`, `src_sampler`,
    `src_reference_value`, `offset` operands are the same as in
    `dxsa.sample` instruction.

    The `feedback` operand is an additional 32 bit scalar Tiled Resources
    shader feedback status output value. Unlike the `_cl_s` opcodes,
    `sample_c_lz_s` takes no LOD clamp: its LOD is always zero.
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_reference_value `,` $feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.gather4
//===----------------------------------------------------------------------===//

def DXSA_Gather4 : DXSA_Op<"gather4"> {
  let summary = "gathers four texels and packs them into a single register";
  let description = [{
    The `dxsa.gather4` operation gathers the four texels that would be used in a
    bi-linear filtering operation and packs them into a single register. Only
    works with 2D or CubeMap textures (incl arrays). Only the addressing modes
    of the sampler are used and the top level of any mip pyramid is used.

    `dxsa.gather4` behaves like the `dxsa.sample` instruction, but a filtered
    sample is not generated.  The four samples that would contribute to
    filtering are placed into xyzw in counter clockwise order starting with the
    sample to the lower left of the queried location.  This is the same as point
    sampling with (u,v) texture coordinate deltas at the following locations:
    (-,+),(+,+),(+,-),(-,-), where the magnitude of the deltas are always half a
    texel.

    `src_address` provides the set of texture coordinates needed to perform the
    sample, as floating point values referencing normalized space in the
    texture.

    `src_resource` is a texture register (t). This is simply a placeholder for a
    texture, including the return data type of the resource being sampled.

    `src_sampler` is a sampler register (s).  This is simply a placeholder for a
    collection of filtering controls (such as point vs. linear, mipmapping and
    address wrapping controls).

    The optional `offset` operand suffix (address offset by immediate integer)
    indicates that the texture coordinates for the sample are to be offset by a
    set of provided immediate texel space integer constant values. The literal
    values are a set of 4 bit 2's complement numbers, having integer range
    [-8,7].

    Example:

    ```mlir
    dxsa.gather4 r<0>, v<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <x>>
    dxsa.gather4 r<1>, v<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <y>>, <u = -5, v = 7, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_Gather4Feedback : DXSA_Op<"gather4_s"> {
  let summary = "same as `dxsa.gather4`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_resource`, `src_sampler`, `offset`
    operands are the same as in `dxsa.gather4` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not
    present) if not used.  See Tiled Resources Texture Sampling
    Features(5.9.4.5) for details.

    Example:

    ```mlir
    dxsa.gather4_s r<1>, v<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <w>>, r<2, <x>>, <u = -3, v = 2, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.gather4_c
//===----------------------------------------------------------------------===//

def DXSA_Gather4C : DXSA_Op<"gather4_c"> {
  let summary = "same as `dxsa.gather4`, except performs comparison on texels, similar to `dxsa.sample_c`";
  let description = [{
    The operands to `dxsa.gather4_c` are identical to `dxsa.gather4`, except
    that there is an additional float32 source operand, `src_reference_value`,
    which must be a register with single-component selected, or a scalar
    literal.

    See existing `dxsa.sample_c` for how `src_reference_value` gets compared
    against each fetched texel.  Unlike `dxsa.sample_c`, `dxsa.gather4_c` simply
    returns each comparison result, rather than filtering them.

    Example:

    ```mlir
    dxsa.gather4_c r<0>, v<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <x>>, v<0, <y>>
    dxsa.gather4_c r<1>, v<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <y>>, v<0, <y>>, <u = -5, v = 7, w = 0>
     ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_reference_value
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_Gather4CFeedback : DXSA_Op<"gather4_c_s"> {
  let summary = "same as `dxsa.`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_resource`, `src_sampler`,
    `src_reference_value`, `offset` operands are the same as in
    `dxsa.gather4_c` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not
    present) if not used.  See Tiled Resources Texture Sampling
    Features(5.9.4.5) for details.

    Example:

    ```mlir
    dxsa.gather4_c_s r<1>, v<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <w>>, v<0, <y>>, r<2, <x>>, <u = -3, v = 2, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_resource `,` $src_sampler `,` $src_reference_value `,` $feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.gather4_po
//===----------------------------------------------------------------------===//

def DXSA_Gather4PO : DXSA_Op<"gather4_po"> {
  let summary = "variant of `dxsa.gather4`, where the offset comes as a parameter to the instruction";
  let description = [{
    Variant of `dxsa.gather4`, where instead of supporting an immediate offset
    [-8..7], the offset comes as a `src_offset` parameter to the instruction,
    and also has larger range of [-32..31].

    The first 2 components of the 4-vector offset parameter supply 32-bit
    integer offsets.  The other components of this parameter are ignored.

    The 6 least significant bits of each offset value is honored as a signed
    value, yielding [-32..31] range.

    `dxsa.gather4_po` only works with 2D textures (unlike gather4, which also
    works with TextureCubes).

    Example:

    ```mlir
    dxsa.gather4_po r<1, <x>>, v<0, <x, y, x, x>>, r<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_offset,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_offset `,` $src_resource `,` $src_sampler
    attr-dict
  }];
}

def DXSA_Gather4POFeedback : DXSA_Op<"gather4_po_s"> {
  let summary = "same as `dxsa.gather4_po`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_offset`, `src_resource`, `src_sampler`
    operands are the same as in `dxsa.gather4_po` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not
    present) if not used.  See Tiled Resources Texture Sampling
    Features(5.9.4.5) for details.

    Example:

    ```mlir
    dxsa.gather4_po_s r<3, <x>>, v<0, <x, y, x, x>>, r<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <w>>, r<4, <x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_offset,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_offset `,` $src_resource `,` $src_sampler `,` $feedback
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.gather4_po_c
//===----------------------------------------------------------------------===//

def DXSA_Gather4POC : DXSA_Op<"gather4_po_c"> {
  let summary = "same as `dxsa.gather4_po`, except performs comparison on texels, similar to `dxsa.sample_c`";
  let description = [{
    The operands to `dxsa.gather4_po_c` are identical to `dxsa.gather4_po`,
    except that there is an additional float32 source operand,
    `src_reference_value`, which must be a register with single-component
    selected, or a scalar literal.

    See existing `dxsa.sample_c` for how `src_reference_value` gets compared
    against each fetched texel.  Unlike `dxsa.sample_c`, `dxsa.gather4_po_c`
    simply returns each comparison result, rather than filtering them.

    Example:

    ```mlir
    dxsa.gather4_po_c r<1, <x>>, v<0, <x, y, x, x>>, r<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <x>>, v<0, <y>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_offset,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_offset `,` $src_resource `,` $src_sampler `,` $src_reference_value
    attr-dict
  }];
}

def DXSA_Gather4POCFeedback : DXSA_Op<"gather4_po_c_s"> {
  let summary = "same as `dxsa.gather4_po_c`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_offset`, `src_resource`, `src_sampler`,
    `src_reference_value` operands are the same as in
    `dxsa.gather4_po_c` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not present)
    if not used.  See Tiled Resources Texture Sampling Features(5.9.4.5) for
    details.

    Example:

    ```mlir
    dxsa.gather4_po_c_s r<3, <x>>, v<0, <x, y, x, x>>, r<0, <x, y, x, x>>, t<0, vector>, s<0, vector, <w>>, v<0, <y>>, r<4, <x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_offset,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$src_sampler,
      DXSA_SrcOperandAttr:$src_reference_value,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);

  let assemblyFormat = [{
    (`precise` $precise^)? $dst `,` $src_address `,` $src_offset `,` $src_resource `,` $src_sampler `,` $src_reference_value `,` $feedback
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ld
//===----------------------------------------------------------------------===//

def DXSA_Ld : DXSA_Op<"ld"> {
  let summary = "simplified alternative to the `dxsa.sample` instruction";
  let description = [{
    Using the provided integer address, `dxsa.ld` fetches data from
    the specified Buffer/Texture without any filtering (e.g. point
    sampling). The source data may come from any Resource Type,
    other than TextureCube.

    Unlike `dxsa.sample`, `dxsa.ld` is also capable of fetching data
    from Buffers.

    This instruction is available in the Vertex Shader, Pixel Shader
    and Geometry Shader.

    `src_address` provides the set of texture coordinates needed to
    perform the sample in the form of unsigned integers.

    The optional `offset` operand suffix (address offset by immediate
    integer) indicates that the texture coordinates for the sample are
    to be offset by a set of provided immediate texel space integer
    constant values. The literal values are a set of 4 bit 2's
    complement numbers, having integer range [-8,7].

    Example:

    ```mlir
    dxsa.ld r<0>, r<0>, t<3, vector>
    dxsa.ld r<2>, r<1, <x, y, w, w>>, t<3, vector>, <u = -5, v = 7, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_resource
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_LdFeedback : DXSA_Op<"ld_s"> {
  let summary = "same as `dxsa.ld`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_resource`, `offset` operands are the
    same as in `dxsa.ld` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not
    present) if not used. See Tiled Resources Texture Sampling
    Features(5.9.4.5) for details.

    Example:

    ```mlir
    dxsa.ld_s r<2>, r<1, <x, y, w, w>>, t<3, vector>, r<3, <x>>, <u = -5, v = 7, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_resource `,` $feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ld2dms
//===----------------------------------------------------------------------===//

def DXSA_Ld2dms : DXSA_Op<"ld2dms"> {
  let summary = "variant of `dxsa.ld` for reading individual samples out of 2d multisample textures";
  let description = [{
    Simplified alternative to the `dxsa.sample` instruction for MS
    resources. Using the provided integer address and `sample_index`,
    `dxsa.ld2dms` fetches data from the specified Texture without any
    filtering (e.g. point sampling). `sample_index` does not have to
    be a literal, the multisample count does not have to be specified
    on the texture resource, and it works with depth/stencil views,
    otherwise it is identical to the DX10 version of this instruction.

    Example:

    ```mlir
    dxsa.ld2dms r<2, <x, y, z>>, r<1, <x, y, w, w>>, t<3, vector>, v<0, <z>>
    dxsa.ld2dms r<2, <x, y, z>>, r<1, <x, y, w, w>>, t<3, vector>, v<0, <z>>, <u = -5, v = 7, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$sample_index,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_resource `,` $sample_index
    (`,` $offset^)?
    attr-dict
  }];
}

def DXSA_Ld2dmsFeedback : DXSA_Op<"ld2dms_s"> {
  let summary = "same as `dxsa.ld2dms`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_resource`, `sample_index`, `offset`
    operands are the same as in `dxsa.ld2dms` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not
    present) if not used. See Tiled Resources Texture Sampling
    Features(5.9.4.5) for details.

    Example:

    ```mlir
    dxsa.ld2dms_s r<2, <x, y, z>>, r<1, <x, y, w, w>>, t<3, vector>, v<0, <z>>, r<3, <x>>
    dxsa.ld2dms_s r<2, <x, y, z>>, r<1, <x, y, w, w>>, t<3, vector>, v<0, <z>>, r<3, <x>>, <u = -5, v = 7, w = 0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_resource,
      DXSA_SrcOperandAttr:$sample_index,
      DXSA_DstOperandAttr:$feedback,
      OptionalAttr<DXSA_SampleOffsetAttr>:$offset);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_resource `,` $sample_index `,` $feedback
    (`,` $offset^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ld_raw
//===----------------------------------------------------------------------===//

def DXSA_LdRaw : DXSA_Op<"ld_raw"> {
  let summary = "random-access read of a 1-4 32 bit components from a raw buffer";
  let description = [{
    The `dxsa.ld_raw` reads (1-4) component 32 bit from `src` at `src_offset`.

    `src` must be:
      - Any shader stage: SRV (t#).
      - Compute Shader or Pixel Shader: UAV (u#).
      - Compute Shader:Thread Group Shared Memory (g#).

    `src_byte_offset` specifies the base 32-bit value in memory for a
    window of 4 sequential 32-bit values in which data may be read
    (depending on the swizzle and mask on other parameters).

    Example:

    ```mlir
    dxsa.ld_raw r<0, <x, z>>, r<0, <x>>, g<0, vector, <z, x, w, y>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_byte_offset,
      DXSA_SrcOperandAttr:$src);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_byte_offset `,` $src
    attr-dict
  }];
}

def DXSA_LdRawFeedback : DXSA_Op<"ld_raw_s"> {
  let summary = "same as `dxsa.ld_raw`, but with an additional status output";
  let description = [{
    `dst`, `src_byte_offset`, `src` operands are the same as in
    `dxsa.ld_raw` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not present)
    if not used. See Tiled Resources Texture Sampling Features(5.9.4.5) for
    details.

    Example:

    ```mlir
    dxsa.ld_raw_s r<5, <x>>, r<0, <x>>, u<1, vector, <x, x, x, x>>, r<6, <x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_byte_offset,
      DXSA_SrcOperandAttr:$src,
      DXSA_DstOperandAttr:$feedback);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_byte_offset `,` $src `,` $feedback
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ld_structured
//===----------------------------------------------------------------------===//

def DXSA_LdStructured : DXSA_Op<"ld_structured"> {
  let summary = "random-access read of a 1-4 32 bit components from a structured buffer";
  let description = [{
    The `dxsa.ld_structured` reads (1-4) component 32bit from `src` at
    `src_address` and `src_byte_offset`.

    `src` must be an SRV (t#), UAV (u#), or in the Compute Shader it can
    also be Thread Group Shared Memory (g#).

    `src_address` specifies the index of the structure to read.

    `src_byte_offset` specifies the byte offset in the structure to
    start reading from.

    Example:

    ```mlir
    dxsa.ld_structured r<0, <w>>, r<0, <z>>, l(0x8), g<0, vector, <x, x, x, x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_byte_offset,
      DXSA_SrcOperandAttr:$src);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_byte_offset `,` $src
    attr-dict
  }];
}

def DXSA_LdStructuredFeedback : DXSA_Op<"ld_structured_s"> {
  let summary = "same as `dxsa.ld_structured`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_byte_offset`, `src` operands are the
    same as in `dxsa.ld_structured` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not present)
    if not used. See Tiled Resources Texture Sampling Features(5.9.4.5) for
    details.

    Example:

    ```mlir
    dxsa.ld_structured_s r<5, <x, y>>, r<0, <y>>, l(0x0), u<1, vector>, r<0, <x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_byte_offset,
      DXSA_SrcOperandAttr:$src,
      DXSA_DstOperandAttr:$feedback);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_byte_offset `,` $src `,` $feedback
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ld_uav_typed
//===----------------------------------------------------------------------===//

def DXSA_LdUavTyped : DXSA_Op<"ld_uav_typed"> {
  let summary = "random-access read of an element from a typed UAV";
  let description = [{
    The `dxsa.ld_uav_typed` reads 4 component element from `src_uav`
    at the unsigned integer address in `src_address`, converted to 32
    bit per component based on the format, then written to `dst` in
    Shader.

    `src_uav` is a UAV (u#) declared as typed. However, the type of the
    bound resource must be R32_UINT/SINT/FLOAT. This is a limitation
    on some D3D11 Hardware that is intended to be relaxed in future
    releases. Note that `dxsa.store_uav_typed` has no such
    limitation.

    The number of 32-bit unsigned integer components taken from
    address are determined by the dimensionality of the resource
    declared at `src_uav`. Addressing is the same as the `dxsa.ld`
    instruction.

    Out of bounds addressing is the same as the `dxsa.ld` instruction.

    Example:

    ```mlir
    dxsa.ld_uav_typed r<0, min16i, <x, y, z>>, v<0, <y, y, y, y>>, u<3, vector>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_uav);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_uav
    attr-dict
  }];
}

def DXSA_LdUavTypedFeedback : DXSA_Op<"ld_uav_typed_s"> {
  let summary = "same as `dxsa.ld_uav_typed`, but with an additional status output";
  let description = [{
    `dst`, `src_address`, `src_uav` operands are the same as in
    `dxsa.ld_uav_typed` instruction.

    The `feedback` operand appends an additional 32 bit scalar Tiled
    Resources shader feedback status output value. Can be NULL (or not present)
    if not used. See Tiled Resources Texture Sampling Features(5.9.4.5) for
    details.

    Example:

    ```mlir
    dxsa.ld_uav_typed_s r<0, min16i, <x, y, z>>, v<0, <x, x, x, x>>, u<3, vector, <w, y, z, x>>, r<4, <x>>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_uav,
      DXSA_DstOperandAttr:$feedback);
  let results = (outs);

  let assemblyFormat = [{
    $dst `,` $src_address `,` $src_uav `,` $feedback
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.store_uav_typed
//===----------------------------------------------------------------------===//

def DXSA_StoreUavTyped : DXSA_Op<"store_uav_typed"> {
  let summary = "write a value to a typed unordered access view";
  let description = [{
    The `dxsa.store_uav_typed` operation writes `$src_value` to the
    location `$src_address` selects in the typed unordered access view
    `$dst_uav`, whose write mask names the components the store covers.

    `$src_address` is read as unsigned integers, one component per
    dimension of the view. A typed unordered access view has neither a mip
    level nor a texel offset, so the address carries only coordinates.

    Example:

    ```mlir
    dxsa.store_uav_typed u<3, vector>, v<0, <z, w, w, w>>, r<0>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst_uav,
      DXSA_SrcOperandAttr:$src_address,
      DXSA_SrcOperandAttr:$src_value);
  let results = (outs);

  let assemblyFormat = "$dst_uav `,` $src_address `,` $src_value attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.check_access_fully_mapped
//===----------------------------------------------------------------------===//

def DXSA_CheckAccessFullyMapped : DXSA_UnaryOp<"check_access_fully_mapped"> {
  let summary = "determine whether a tiled resource access was fully mapped";
  let description = [{
    The `dxsa.check_access_fully_mapped` operation interprets the shader
    feedback status value a `_s` resource access wrote into `$src` and sets
    every component `$dst` enables to all ones when every tile the access
    touched was mapped, and to zero otherwise.

    The status value is opaque: `$src` may only be read by this instruction.

    Example:

    ```mlir
    dxsa.check_access_fully_mapped r<1, <x>>, r<2, <x>>
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSARESOURCEOPS
