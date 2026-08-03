//===- DXSAFPArithOps.td - DXSA float arithmetic ops ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Floating-point arithmetic instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSAFPARITHOPS
#define FEME_DIALECT_DXSA_IR_DXSAFPARITHOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.add
//===----------------------------------------------------------------------===//

def DXSA_Add : DXSA_BinaryOp<"add"> {
  let summary = "component-wise floating-point add";
  let description = [{
    The `dxsa.add` operation computes the component-wise floating-point
    sum `$dst = $lhs + $rhs`.

    Example:

    ```mlir
    dxsa.add r<0>, r<1>, r<2>
    dxsa.add r<0>, -r<1>, -|r<2>|
    dxsa.add r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.add_sat
//===----------------------------------------------------------------------===//

def DXSA_AddSat : DXSA_BinaryOp<"add_sat"> {
  let summary = "component-wise floating-point add, saturated to [0, 1]";
  let description = [{
    The `dxsa.add_sat` operation computes the component-wise floating-point
    sum of `$lhs` and `$rhs`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.add_sat r<0>, r<1>, r<2>
    dxsa.add_sat r<0>, -r<1>, -|r<2>|
    dxsa.add_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.div
//===----------------------------------------------------------------------===//

def DXSA_Div : DXSA_BinaryOp<"div"> {
  let summary = "component-wise floating-point divide";
  let description = [{
    The `dxsa.div` operation computes the component-wise floating-point
    quotient `$dst = $lhs / $rhs`.

    Example:

    ```mlir
    dxsa.div r<0>, r<1>, r<2>
    dxsa.div r<0>, -r<1>, -|r<2>|
    dxsa.div r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.div_sat
//===----------------------------------------------------------------------===//

def DXSA_DivSat : DXSA_BinaryOp<"div_sat"> {
  let summary = "component-wise floating-point divide, saturated to [0, 1]";
  let description = [{
    The `dxsa.div_sat` operation computes the component-wise floating-point
    quotient of `$lhs` and `$rhs`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.div_sat r<0>, r<1>, r<2>
    dxsa.div_sat r<0>, -r<1>, -|r<2>|
    dxsa.div_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dp2
//===----------------------------------------------------------------------===//

def DXSA_Dp2 : DXSA_BinaryOp<"dp2"> {
  let summary = "2D floating-point dot product";
  let description = [{
    The `dxsa.dp2` operation computes the 2D dot product of `$lhs` and
    `$rhs`. Each source is swizzled first, then the products of their first
    two components are summed into a single scalar that is replicated to
    every component selected by the write mask of `$dst`.

    Example:

    ```mlir
    dxsa.dp2 r<0>, r<1>, r<2>
    dxsa.dp2 r<0>, -r<1>, -|r<2>|
    dxsa.dp2 r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dp2_sat
//===----------------------------------------------------------------------===//

def DXSA_Dp2Sat : DXSA_BinaryOp<"dp2_sat"> {
  let summary = "2D floating-point dot product, saturated to [0, 1]";
  let description = [{
    The `dxsa.dp2_sat` operation computes the 2D dot product of `$lhs` and
    `$rhs`. Each source is swizzled first, then the products of their first
    two components are summed into a single scalar, clamped to `[0.0, 1.0]`,
    and replicated to every component selected by the write mask of `$dst`.

    Example:

    ```mlir
    dxsa.dp2_sat r<0>, r<1>, r<2>
    dxsa.dp2_sat r<0>, -r<1>, -|r<2>|
    dxsa.dp2_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dp3
//===----------------------------------------------------------------------===//

def DXSA_Dp3 : DXSA_BinaryOp<"dp3"> {
  let summary = "3D floating-point dot product";
  let description = [{
    The `dxsa.dp3` operation computes the 3D dot product of `$lhs` and
    `$rhs`. Each source is swizzled first, then the products of their first
    three components are summed into a single scalar that is replicated to
    every component selected by the write mask of `$dst`.

    Example:

    ```mlir
    dxsa.dp3 r<0>, r<1>, r<2>
    dxsa.dp3 r<0>, -r<1>, -|r<2>|
    dxsa.dp3 r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dp3_sat
//===----------------------------------------------------------------------===//

def DXSA_Dp3Sat : DXSA_BinaryOp<"dp3_sat"> {
  let summary = "3D floating-point dot product, saturated to [0, 1]";
  let description = [{
    The `dxsa.dp3_sat` operation computes the 3D dot product of `$lhs` and
    `$rhs`. Each source is swizzled first, then the products of their first
    three components are summed into a single scalar, clamped to
    `[0.0, 1.0]`, and replicated to every component selected by the write
    mask of `$dst`.

    Example:

    ```mlir
    dxsa.dp3_sat r<0>, r<1>, r<2>
    dxsa.dp3_sat r<0>, -r<1>, -|r<2>|
    dxsa.dp3_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dp4
//===----------------------------------------------------------------------===//

def DXSA_Dp4 : DXSA_BinaryOp<"dp4"> {
  let summary = "4D floating-point dot product";
  let description = [{
    The `dxsa.dp4` operation computes the 4D dot product of `$lhs` and
    `$rhs`. Each source is swizzled first, then the products of their four
    components are summed into a single scalar that is replicated to every
    component selected by the write mask of `$dst`.

    Example:

    ```mlir
    dxsa.dp4 r<0>, r<1>, r<2>
    dxsa.dp4 r<0>, -r<1>, -|r<2>|
    dxsa.dp4 r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dp4_sat
//===----------------------------------------------------------------------===//

def DXSA_Dp4Sat : DXSA_BinaryOp<"dp4_sat"> {
  let summary = "4D floating-point dot product, saturated to [0, 1]";
  let description = [{
    The `dxsa.dp4_sat` operation computes the 4D dot product of `$lhs` and
    `$rhs`. Each source is swizzled first, then the products of their four
    components are summed into a single scalar, clamped to `[0.0, 1.0]`, and
    replicated to every component selected by the write mask of `$dst`.

    Example:

    ```mlir
    dxsa.dp4_sat r<0>, r<1>, r<2>
    dxsa.dp4_sat r<0>, -r<1>, -|r<2>|
    dxsa.dp4_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.exp
//===----------------------------------------------------------------------===//

def DXSA_Exp : DXSA_UnaryOp<"exp"> {
  let summary = "component-wise base-2 exponential";
  let description = [{
    The `dxsa.exp` operation computes the component-wise base-2 exponential
    `$dst = 2 ^ $src`.

    Example:

    ```mlir
    dxsa.exp r<0>, r<1>
    dxsa.exp r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.exp_sat
//===----------------------------------------------------------------------===//

def DXSA_ExpSat : DXSA_UnaryOp<"exp_sat"> {
  let summary = "component-wise base-2 exponential, saturated to [0, 1]";
  let description = [{
    The `dxsa.exp_sat` operation computes the component-wise base-2
    exponential of `$src`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.exp_sat r<0>, r<1>
    dxsa.exp_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.frc
//===----------------------------------------------------------------------===//

def DXSA_Frc : DXSA_UnaryOp<"frc"> {
  let summary = "component-wise fractional part";
  let description = [{
    The `dxsa.frc` operation extracts the component-wise fractional part of
    `$src` and writes it to `$dst`. The fractional part is `$src` minus its
    floor - `$dst` = `$rhs` - round_ni(`$lhs`), so every result component
    lies in `[0.0, 1.0)`.

    Example:

    ```mlir
    dxsa.frc r<0>, r<1>
    dxsa.frc r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.frc_sat
//===----------------------------------------------------------------------===//

def DXSA_FrcSat : DXSA_UnaryOp<"frc_sat"> {
  let summary = "component-wise fractional part, saturated to [0, 1]";
  let description = [{
    The `dxsa.frc_sat` operation extracts the component-wise fractional part
    of `$src`, clamps each component of the result to `[0.0, 1.0]`, and
    writes it to `$dst`.

    Example:

    ```mlir
    dxsa.frc_sat r<0>, r<1>
    dxsa.frc_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.log
//===----------------------------------------------------------------------===//

def DXSA_Log : DXSA_UnaryOp<"log"> {
  let summary = "component-wise base-2 logarithm";
  let description = [{
    The `dxsa.log` operation computes the component-wise base-2 logarithm
    `$dst = log2($src)`.

    Example:

    ```mlir
    dxsa.log r<0>, r<1>
    dxsa.log r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.log_sat
//===----------------------------------------------------------------------===//

def DXSA_LogSat : DXSA_UnaryOp<"log_sat"> {
  let summary = "component-wise base-2 logarithm, saturated to [0, 1]";
  let description = [{
    The `dxsa.log_sat` operation computes the component-wise base-2 logarithm
    of `$src`, clamps each component of the result to `[0.0, 1.0]`, and
    writes it to `$dst`.

    Example:

    ```mlir
    dxsa.log_sat r<0>, r<1>
    dxsa.log_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.mad
//===----------------------------------------------------------------------===//

def DXSA_Mad : DXSA_MultiplyAddOp<"mad"> {
  let summary = "component-wise floating-point multiply-add";
  let description = [{
    The `dxsa.mad` operation computes the component-wise floating-point
    multiply-add `$dst = $lhs * $rhs + $acc`.

    Example:

    ```mlir
    dxsa.mad r<0>, r<1>, r<2>, r<3>
    dxsa.mad r<0>, -r<1>, |r<2>|, r<3>
    dxsa.mad r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>, r<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.mad_sat
//===----------------------------------------------------------------------===//

def DXSA_MadSat : DXSA_MultiplyAddOp<"mad_sat"> {
  let summary = "component-wise floating-point multiply-add, saturated to [0, 1]";
  let description = [{
    The `dxsa.mad_sat` operation computes the component-wise floating-point
    multiply-add of `$lhs`, `$rhs` and `$acc`, clamps each component of
    the result to `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.mad_sat r<0>, r<1>, r<2>, r<3>
    dxsa.mad_sat r<0>, -r<1>, |r<2>|, r<3>
    dxsa.mad_sat r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>, r<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.max
//===----------------------------------------------------------------------===//

def DXSA_Max : DXSA_BinaryOp<"max"> {
  let summary = "component-wise floating-point maximum";
  let description = [{
    The `dxsa.max` operation computes the component-wise floating-point
    maximum of `$lhs` and `$rhs` and writes it to `$dst`. If one source
    component is NaN, the other source component is returned.

    Example:

    ```mlir
    dxsa.max r<0>, r<1>, r<2>
    dxsa.max r<0>, -r<1>, -|r<2>|
    dxsa.max r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.max_sat
//===----------------------------------------------------------------------===//

def DXSA_MaxSat : DXSA_BinaryOp<"max_sat"> {
  let summary = "component-wise floating-point maximum, saturated to [0, 1]";
  let description = [{
    The `dxsa.max_sat` operation computes the component-wise floating-point
    maximum of `$lhs` and `$rhs`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.max_sat r<0>, r<1>, r<2>
    dxsa.max_sat r<0>, -r<1>, -|r<2>|
    dxsa.max_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.min
//===----------------------------------------------------------------------===//

def DXSA_Min : DXSA_BinaryOp<"min"> {
  let summary = "component-wise floating-point minimum";
  let description = [{
    The `dxsa.min` operation computes the component-wise floating-point
    minimum of `$lhs` and `$rhs` and writes it to `$dst`. If one source
    component is NaN, the other source component is returned.

    Example:

    ```mlir
    dxsa.min r<0>, r<1>, r<2>
    dxsa.min r<0>, -r<1>, -|r<2>|
    dxsa.min r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.min_sat
//===----------------------------------------------------------------------===//

def DXSA_MinSat : DXSA_BinaryOp<"min_sat"> {
  let summary = "component-wise floating-point minimum, saturated to [0, 1]";
  let description = [{
    The `dxsa.min_sat` operation computes the component-wise floating-point
    minimum of `$lhs` and `$rhs`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.min_sat r<0>, r<1>, r<2>
    dxsa.min_sat r<0>, -r<1>, -|r<2>|
    dxsa.min_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.mul
//===----------------------------------------------------------------------===//

def DXSA_Mul : DXSA_BinaryOp<"mul"> {
  let summary = "component-wise floating-point multiply";
  let description = [{
    The `dxsa.mul` operation computes the component-wise floating-point
    product `$dst = $lhs * $rhs`.

    Example:

    ```mlir
    dxsa.mul r<0>, r<1>, r<2>
    dxsa.mul r<0>, -r<1>, -|r<2>|
    dxsa.mul r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.mul_sat
//===----------------------------------------------------------------------===//

def DXSA_MulSat : DXSA_BinaryOp<"mul_sat"> {
  let summary = "component-wise floating-point multiply, saturated to [0, 1]";
  let description = [{
    The `dxsa.mul_sat` operation computes the component-wise floating-point
    product of `$lhs` and `$rhs`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.mul_sat r<0>, r<1>, r<2>
    dxsa.mul_sat r<0>, -r<1>, -|r<2>|
    dxsa.mul_sat r<0, <x, y>>, r<1, <y, z, x, w>>, |r<2, <z, z, z, z>>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.rcp
//===----------------------------------------------------------------------===//

def DXSA_Rcp : DXSA_UnaryOp<"rcp"> {
  let summary = "component-wise reciprocal";
  let description = [{
    The `dxsa.rcp` operation computes the component-wise reciprocal
    `$dst = 1.0 / $src`.

    Example:

    ```mlir
    dxsa.rcp r<0>, r<1>
    dxsa.rcp r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.rcp_sat
//===----------------------------------------------------------------------===//

def DXSA_RcpSat : DXSA_UnaryOp<"rcp_sat"> {
  let summary = "component-wise reciprocal, saturated to [0, 1]";
  let description = [{
    The `dxsa.rcp_sat` operation computes the component-wise reciprocal of
    `$src`, clamps each component of the result to `[0.0, 1.0]`, and writes
    it to `$dst`.

    Example:

    ```mlir
    dxsa.rcp_sat r<0>, r<1>
    dxsa.rcp_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_ne
//===----------------------------------------------------------------------===//

def DXSA_RoundNe : DXSA_UnaryOp<"round_ne"> {
  let summary =
      "component-wise floating-point round to integral float, towards nearest even";
  let description = [{
    The `dxsa.round_ne` operation performs a component-wise floating-point
    round of `$src` to an integral float, writing the integral floating-point
    values to `$dst`. Rounding is towards nearest even.

    Example:

    ```mlir
    dxsa.round_ne r<0>, r<1>
    dxsa.round_ne r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_ne_sat
//===----------------------------------------------------------------------===//

def DXSA_RoundNeSat : DXSA_UnaryOp<"round_ne_sat"> {
  let summary = "component-wise floating-point round to integral float, towards "
                "nearest even, saturated to [0, 1]";
  let description = [{
    The `dxsa.round_ne_sat` operation performs a component-wise floating-point
    round of `$src` to an integral float, clamps each resulting component to
    `[0.0, 1.0]`, and writes it to `$dst`. Rounding is towards nearest even.

    Example:

    ```mlir
    dxsa.round_ne_sat r<0>, r<1>
    dxsa.round_ne_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_ni
//===----------------------------------------------------------------------===//

def DXSA_RoundNi : DXSA_UnaryOp<"round_ni"> {
  let summary = "component-wise floating-point round to integral float, towards "
                "negative infinity";
  let description = [{
    The `dxsa.round_ni` operation performs a component-wise floating-point
    round of `$src` to an integral float, writing the integral floating-point
    values to `$dst`. Rounding is towards negative infinity (floor).

    Example:

    ```mlir
    dxsa.round_ni r<0>, r<1>
    dxsa.round_ni r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_ni_sat
//===----------------------------------------------------------------------===//

def DXSA_RoundNiSat : DXSA_UnaryOp<"round_ni_sat"> {
  let summary = "component-wise floating-point round to integral float, towards "
                "negative infinity, saturated to [0, 1]";
  let description = [{
    The `dxsa.round_ni_sat` operation performs a component-wise floating-point
    round of `$src` to an integral float, clamps each resulting component to
    `[0.0, 1.0]`, and writes it to `$dst`. Rounding is towards negative
    infinity (floor).

    Example:

    ```mlir
    dxsa.round_ni_sat r<0>, r<1>
    dxsa.round_ni_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_pi
//===----------------------------------------------------------------------===//

def DXSA_RoundPi : DXSA_UnaryOp<"round_pi"> {
  let summary = "component-wise floating-point round to integral float, towards "
                "positive infinity";
  let description = [{
    The `dxsa.round_pi` operation performs a component-wise floating-point
    round of `$src` to an integral float, writing the integral floating-point
    values to `$dst`. Rounding is towards positive infinity (ceil).

    Example:

    ```mlir
    dxsa.round_pi r<0>, r<1>
    dxsa.round_pi r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_pi_sat
//===----------------------------------------------------------------------===//

def DXSA_RoundPiSat : DXSA_UnaryOp<"round_pi_sat"> {
  let summary = "component-wise floating-point round to integral float, towards "
                "positive infinity, saturated to [0, 1]";
  let description = [{
    The `dxsa.round_pi_sat` operation performs a component-wise floating-point
    round of `$src` to an integral float, clamps each resulting component to
    `[0.0, 1.0]`, and writes it to `$dst`. Rounding is towards positive
    infinity (ceil).

    Example:

    ```mlir
    dxsa.round_pi_sat r<0>, r<1>
    dxsa.round_pi_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_z
//===----------------------------------------------------------------------===//

def DXSA_RoundZ : DXSA_UnaryOp<"round_z"> {
  let summary =
      "component-wise floating-point round to integral float, towards zero";
  let description = [{
    The `dxsa.round_z` operation performs a component-wise floating-point
    round of `$src` to an integral float, writing the integral floating-point
    values to `$dst`. Rounding is towards zero.

    Example:

    ```mlir
    dxsa.round_z r<0>, r<1>
    dxsa.round_z r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.round_z_sat
//===----------------------------------------------------------------------===//

def DXSA_RoundZSat : DXSA_UnaryOp<"round_z_sat"> {
  let summary = "component-wise floating-point round to integral float, towards "
                "zero, saturated to [0, 1]";
  let description = [{
    The `dxsa.round_z_sat` operation performs a component-wise floating-point
    round of `$src` to an integral float, clamps each resulting component to
    `[0.0, 1.0]`, and writes it to `$dst`. Rounding is towards zero.

    Example:

    ```mlir
    dxsa.round_z_sat r<0>, r<1>
    dxsa.round_z_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.rsq
//===----------------------------------------------------------------------===//

def DXSA_Rsq : DXSA_UnaryOp<"rsq"> {
  let summary = "component-wise reciprocal square root";
  let description = [{
    The `dxsa.rsq` operation computes the component-wise reciprocal square
    root `$dst = 1.0 / sqrt($src)`.

    Example:

    ```mlir
    dxsa.rsq r<0>, r<1>
    dxsa.rsq r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.rsq_sat
//===----------------------------------------------------------------------===//

def DXSA_RsqSat : DXSA_UnaryOp<"rsq_sat"> {
  let summary = "component-wise reciprocal square root, saturated to [0, 1]";
  let description = [{
    The `dxsa.rsq_sat` operation computes the component-wise reciprocal
    square root of `$src`, clamps each component of the result to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.rsq_sat r<0>, r<1>
    dxsa.rsq_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sincos
//===----------------------------------------------------------------------===//

// Shared base for sine/cosine: sin of `$operand` into `$sin`, cosine into
// `$cos`. Either destination may be `null` when that result is not needed.
class DXSA_SincosOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$sin,
      DXSA_DstOperandAttr:$cos,
      DXSA_SrcOperandAttr:$operand,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $sin `,` $cos `,` $operand attr-dict";
}

def DXSA_Sincos : DXSA_SincosOp<"sincos"> {
  let summary = "component-wise floating-point sine and cosine";
  let description = [{
    The `dxsa.sincos` operation computes the component-wise sine of
    `$operand` into `$sin` and the cosine of `$operand` into `$cos`, with the
    angle expressed in radians. Either destination may be `null` when that
    result is not needed.

    Example:

    ```mlir
    dxsa.sincos r<0>, r<1>, r<2>
    dxsa.sincos null, r<1>, r<2>
    dxsa.sincos r<0>, null, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sincos_sat
//===----------------------------------------------------------------------===//

def DXSA_SincosSat : DXSA_SincosOp<"sincos_sat"> {
  let summary = "component-wise floating-point sine and cosine, saturated to [0, 1]";
  let description = [{
    The `dxsa.sincos_sat` operation computes the component-wise sine of
    `$operand` into `$sin` and the cosine of `$operand` into `$cos`, clamps
    each result component to `[0.0, 1.0]`, with the angle expressed in
    radians. Either destination may be `null` when that result is not needed.

    Example:

    ```mlir
    dxsa.sincos_sat r<0>, r<1>, r<2>
    dxsa.sincos_sat null, r<1>, r<2>
    dxsa.sincos_sat r<0>, null, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sqrt
//===----------------------------------------------------------------------===//

def DXSA_Sqrt : DXSA_UnaryOp<"sqrt"> {
  let summary = "component-wise square root";
  let description = [{
    The `dxsa.sqrt` operation computes the component-wise square root
    `$dst = sqrt($src)`.

    Example:

    ```mlir
    dxsa.sqrt r<0>, r<1>
    dxsa.sqrt r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.sqrt_sat
//===----------------------------------------------------------------------===//

def DXSA_SqrtSat : DXSA_UnaryOp<"sqrt_sat"> {
  let summary = "component-wise square root, saturated to [0, 1]";
  let description = [{
    The `dxsa.sqrt_sat` operation computes the component-wise square root of
    `$src`, clamps each component of the result to `[0.0, 1.0]`, and writes
    it to `$dst`.

    Example:

    ```mlir
    dxsa.sqrt_sat r<0>, r<1>
    dxsa.sqrt_sat r<0>, -|r<1>|
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSAFPARITHOPS
