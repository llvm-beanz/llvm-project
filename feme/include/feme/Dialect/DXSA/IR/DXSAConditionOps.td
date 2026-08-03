//===- DXSAConditionOps.td - DXSA condition compute ops -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Condition computing instructions of the DXSA dialect. Each op compares
// two source operands component-wise and writes 0xFFFFFFFF to a component
// of the destination when the comparison is true, or 0 when it is false.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSACONDITIONOPS
#define FEME_DIALECT_DXSA_IR_DXSACONDITIONOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.eq
//===----------------------------------------------------------------------===//

def DXSA_Eq : DXSA_BinaryOp<"eq"> {
  let summary = "component-wise floating-point equality comparison";
  let description = [{
    The `dxsa.eq` operation compares `$lhs` and `$rhs` for floating-point
    equality component-wise, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.eq r<0>, r<1>, r<2>
    dxsa.eq r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ge
//===----------------------------------------------------------------------===//

def DXSA_Ge : DXSA_BinaryOp<"ge"> {
  let summary = "component-wise floating-point greater-equal comparison";
  let description = [{
    The `dxsa.ge` operation computes the component-wise floating-point
    comparison `$lhs >= $rhs`, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ge r<0>, r<1>, r<2>
    dxsa.ge r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.lt
//===----------------------------------------------------------------------===//

def DXSA_Lt : DXSA_BinaryOp<"lt"> {
  let summary = "component-wise floating-point less-than comparison";
  let description = [{
    The `dxsa.lt` operation computes the component-wise floating-point
    comparison `$lhs < $rhs`, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.lt r<0>, r<1>, r<2>
    dxsa.lt r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ne
//===----------------------------------------------------------------------===//

def DXSA_Ne : DXSA_BinaryOp<"ne"> {
  let summary = "component-wise floating-point not-equal comparison";
  let description = [{
    The `dxsa.ne` operation compares `$lhs` and `$rhs` for floating-point
    inequality component-wise, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ne r<0>, r<1>, r<2>
    dxsa.ne r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ieq
//===----------------------------------------------------------------------===//

def DXSA_Ieq : DXSA_BinaryOp<"ieq"> {
  let summary = "component-wise integer equality comparison";
  let description = [{
    The `dxsa.ieq` operation compares `$lhs` and `$rhs` for integer
    equality component-wise, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ieq r<0>, r<1>, r<2>
    dxsa.ieq r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ige
//===----------------------------------------------------------------------===//

def DXSA_Ige : DXSA_BinaryOp<"ige"> {
  let summary = "component-wise signed integer greater-equal comparison";
  let description = [{
    The `dxsa.ige` operation computes the component-wise signed integer
    comparison `$lhs >= $rhs`, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ige r<0>, r<1>, r<2>
    dxsa.ige r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ilt
//===----------------------------------------------------------------------===//

def DXSA_Ilt : DXSA_BinaryOp<"ilt"> {
  let summary = "component-wise signed integer less-than comparison";
  let description = [{
    The `dxsa.ilt` operation computes the component-wise signed integer
    comparison `$lhs < $rhs`, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ilt r<0>, r<1>, r<2>
    dxsa.ilt r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ine
//===----------------------------------------------------------------------===//

def DXSA_Ine : DXSA_BinaryOp<"ine"> {
  let summary = "component-wise integer not-equal comparison";
  let description = [{
    The `dxsa.ine` operation compares `$lhs` and `$rhs` for integer
    inequality component-wise, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ine r<0>, r<1>, r<2>
    dxsa.ine r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.uge
//===----------------------------------------------------------------------===//

def DXSA_Uge : DXSA_BinaryOp<"uge"> {
  let summary = "component-wise unsigned integer greater-equal comparison";
  let description = [{
    The `dxsa.uge` operation computes the component-wise unsigned integer
    comparison `$lhs >= $rhs`, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.uge r<0>, r<1>, r<2>
    dxsa.uge r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ult
//===----------------------------------------------------------------------===//

def DXSA_Ult : DXSA_BinaryOp<"ult"> {
  let summary = "component-wise unsigned integer less-than comparison";
  let description = [{
    The `dxsa.ult` operation computes the component-wise unsigned integer
    comparison `$lhs < $rhs`, writing 0xFFFFFFFF to a component of `$dst`
    when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.ult r<0>, r<1>, r<2>
    dxsa.ult r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.deq
//===----------------------------------------------------------------------===//

def DXSA_Deq : DXSA_BinaryOp<"deq"> {
  let summary = "component-wise double-precision equality comparison";
  let description = [{
    The `dxsa.deq` operation computes the component-wise double-precision
    floating-point comparison `$lhs == $rhs`, writing 0xFFFFFFFF to a
    component of `$dst` when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.deq r<0>, r<1>, r<2>
    dxsa.deq r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dge
//===----------------------------------------------------------------------===//

def DXSA_Dge : DXSA_BinaryOp<"dge"> {
  let summary = "component-wise double-precision greater-equal comparison";
  let description = [{
    The `dxsa.dge` operation computes the component-wise double-precision
    floating-point comparison `$lhs >= $rhs`, writing 0xFFFFFFFF to a
    component of `$dst` when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.dge r<0>, r<1>, r<2>
    dxsa.dge r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dlt
//===----------------------------------------------------------------------===//

def DXSA_Dlt : DXSA_BinaryOp<"dlt"> {
  let summary = "component-wise double-precision less-than comparison";
  let description = [{
    The `dxsa.dlt` operation computes the component-wise double-precision
    floating-point comparison `$lhs < $rhs`, writing 0xFFFFFFFF to a
    component of `$dst` when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.dlt r<0>, r<1>, r<2>
    dxsa.dlt r<0>, -r<1>, |r<2>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dne
//===----------------------------------------------------------------------===//

def DXSA_Dne : DXSA_BinaryOp<"dne"> {
  let summary = "component-wise double-precision not-equal comparison";
  let description = [{
    The `dxsa.dne` operation computes the component-wise double-precision
    floating-point comparison `$lhs != $rhs`, writing 0xFFFFFFFF to a
    component of `$dst` when the result is true and 0 when it is false.

    Example:

    ```mlir
    dxsa.dne r<0>, r<1>, r<2>
    dxsa.dne r<0>, -r<1>, |r<2>|
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSACONDITIONOPS
