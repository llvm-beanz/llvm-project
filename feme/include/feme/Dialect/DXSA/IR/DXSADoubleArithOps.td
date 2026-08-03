//===- DXSADoubleArithOps.td - DXSA double arithmetic ops -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Double-precision arithmetic instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSADOUBLEARITHOPS
#define FEME_DIALECT_DXSA_IR_DXSADOUBLEARITHOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.dadd
//===----------------------------------------------------------------------===//

def DXSA_DAdd : DXSA_BinaryOp<"dadd"> {
  let summary = "component-wise double-precision add";
  let description = [{
    The `dxsa.dadd` operation computes the component-wise double-precision
    sum `$dst = $lhs + $rhs`. Each operand holds a vector of doubles, one
    double per `xy` and `zw` component pair.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dadd r<0>, r<1>, r<2>
    dxsa.dadd r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dadd_sat
//===----------------------------------------------------------------------===//

def DXSA_DAddSat : DXSA_BinaryOp<"dadd_sat"> {
  let summary = "component-wise double-precision add, saturated to [0, 1]";
  let description = [{
    The `dxsa.dadd_sat` operation computes the component-wise double-precision
    sum of `$lhs` and `$rhs`, clamps each result component to `[0.0, 1.0]`,
    and writes it to `$dst`.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dadd_sat r<0>, r<1>, r<2>
    dxsa.dadd_sat r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ddiv
//===----------------------------------------------------------------------===//

def DXSA_DDiv : DXSA_BinaryOp<"ddiv"> {
  let summary = "component-wise double-precision divide";
  let description = [{
    The `dxsa.ddiv` operation computes the component-wise double-precision
    quotient `$dst = $lhs / $rhs`. Each operand holds a vector of doubles, one
    double per `xy` and `zw` component pair.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.ddiv r<0>, r<1>, r<2>
    dxsa.ddiv r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ddiv_sat
//===----------------------------------------------------------------------===//

def DXSA_DDivSat : DXSA_BinaryOp<"ddiv_sat"> {
  let summary = "component-wise double-precision divide, saturated to [0, 1]";
  let description = [{
    The `dxsa.ddiv_sat` operation computes the component-wise double-precision
    quotient of `$lhs` and `$rhs`, clamps each result component to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.ddiv_sat r<0>, r<1>, r<2>
    dxsa.ddiv_sat r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmax
//===----------------------------------------------------------------------===//

def DXSA_DMax : DXSA_BinaryOp<"dmax"> {
  let summary = "component-wise double-precision maximum";
  let description = [{
    The `dxsa.dmax` operation computes the component-wise double-precision
    maximum `$dst = $lhs >= $rhs ? $lhs : $rhs`. If one source component is
    NaN, the other source component is returned.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dmax r<0>, r<1>, r<2>
    dxsa.dmax r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmax_sat
//===----------------------------------------------------------------------===//

def DXSA_DMaxSat : DXSA_BinaryOp<"dmax_sat"> {
  let summary = "component-wise double-precision maximum, saturated to [0, 1]";
  let description = [{
    The `dxsa.dmax_sat` operation computes the component-wise double-precision
    maximum of `$lhs` and `$rhs`, clamps each result component to `[0.0, 1.0]`,
    and writes it to `$dst`. If one source component is NaN, the other source
    component is used.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dmax_sat r<0>, r<1>, r<2>
    dxsa.dmax_sat r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmin
//===----------------------------------------------------------------------===//

def DXSA_DMin : DXSA_BinaryOp<"dmin"> {
  let summary = "component-wise double-precision minimum";
  let description = [{
    The `dxsa.dmin` operation computes the component-wise double-precision
    minimum `$dst = $lhs < $rhs ? $lhs : $rhs`. If one source component is
    NaN, the other source component is returned.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dmin r<0>, r<1>, r<2>
    dxsa.dmin r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmin_sat
//===----------------------------------------------------------------------===//

def DXSA_DMinSat : DXSA_BinaryOp<"dmin_sat"> {
  let summary = "component-wise double-precision minimum, saturated to [0, 1]";
  let description = [{
    The `dxsa.dmin_sat` operation computes the component-wise double-precision
    minimum of `$lhs` and `$rhs`, clamps each result component to `[0.0, 1.0]`,
    and writes it to `$dst`. If one source component is NaN, the other source
    component is used.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dmin_sat r<0>, r<1>, r<2>
    dxsa.dmin_sat r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmul
//===----------------------------------------------------------------------===//

def DXSA_DMul : DXSA_BinaryOp<"dmul"> {
  let summary = "component-wise double-precision multiply";
  let description = [{
    The `dxsa.dmul` operation computes the component-wise double-precision
    product `$dst = $lhs * $rhs`. Each operand holds a vector of doubles, one
    double per `xy` and `zw` component pair.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dmul r<0>, r<1>, r<2>
    dxsa.dmul r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmul_sat
//===----------------------------------------------------------------------===//

def DXSA_DMulSat : DXSA_BinaryOp<"dmul_sat"> {
  let summary = "component-wise double-precision multiply, saturated to [0, 1]";
  let description = [{
    The `dxsa.dmul_sat` operation computes the component-wise double-precision
    product of `$lhs` and `$rhs`, clamps each result component to `[0.0, 1.0]`,
    and writes it to `$dst`.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dmul_sat r<0>, r<1>, r<2>
    dxsa.dmul_sat r<0, <x, y>>, r<1, <z, w, x, y>>, -r<2>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.drcp
//===----------------------------------------------------------------------===//

def DXSA_DRcp : DXSA_UnaryOp<"drcp"> {
  let summary = "component-wise double-precision reciprocal";
  let description = [{
    The `dxsa.drcp` operation computes the component-wise double-precision
    reciprocal `$dst = 1.0 / $src`. Each operand holds a vector of doubles, one
    double per `xy` and `zw` component pair.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and the source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.drcp r<0>, r<1>
    dxsa.drcp r<0, <x, y>>, -r<1, <z, w, x, y>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.drcp_sat
//===----------------------------------------------------------------------===//

def DXSA_DRcpSat : DXSA_UnaryOp<"drcp_sat"> {
  let summary = "component-wise double-precision reciprocal, saturated to [0, 1]";
  let description = [{
    The `dxsa.drcp_sat` operation computes the component-wise double-precision
    reciprocal of `$src`, clamps each result component to `[0.0, 1.0]`, and
    writes it to `$dst`.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and the source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.drcp_sat r<0>, r<1>
    dxsa.drcp_sat r<0, <x, y>>, -r<1, <z, w, x, y>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dfma
//===----------------------------------------------------------------------===//

def DXSA_DFma : DXSA_MultiplyAddOp<"dfma"> {
  let summary = "component-wise double-precision multiply-add";
  let description = [{
    The `dxsa.dfma` operation computes the component-wise double-precision
    multiply-add `$dst = $lhs * $rhs + $acc`. Each operand holds a vector of
    doubles, one double per `xy` and `zw` component pair.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dfma r<0>, r<1>, r<2>, r<3>
    dxsa.dfma r<1, <x, y>>, r<1, <x, y, x, y>>, r<1, <z, w, z, w>>, r<2, <x, y, x, y>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dfma_sat
//===----------------------------------------------------------------------===//

def DXSA_DFmaSat : DXSA_MultiplyAddOp<"dfma_sat"> {
  let summary = "component-wise double-precision multiply-add, saturated to [0, 1]";
  let description = [{
    The `dxsa.dfma_sat` operation computes the component-wise double-precision
    multiply-add of `$lhs`, `$rhs` and `$acc`, clamps each result component to
    `[0.0, 1.0]`, and writes it to `$dst`.

    Because each double spans a component pair, the destination write mask must
    be `<x, y>`, `<z, w>`, or `<x, y, z, w>`, and each source swizzle must be
    one of `<x, y, z, w>`, `<x, y, x, y>`, `<z, w, x, y>`, or `<z, w, z, w>`.

    Example:

    ```mlir
    dxsa.dfma_sat r<0>, r<1>, r<2>, r<3>
    dxsa.dfma_sat r<1, <x, y>>, r<1, <x, y, x, y>>, r<1, <z, w, z, w>>, r<2, <x, y, x, y>>
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSADOUBLEARITHOPS
