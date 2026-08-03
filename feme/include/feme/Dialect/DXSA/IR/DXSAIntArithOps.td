//===- DXSAIntArithOps.td - DXSA integer arithmetic ops -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Integer arithmetic instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSAINTARITHOPS
#define FEME_DIALECT_DXSA_IR_DXSAINTARITHOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.iadd
//===----------------------------------------------------------------------===//

def DXSA_IAdd : DXSA_BinaryOp<"iadd"> {
  let summary = "component-wise integer add";
  let description = [{
    The `dxsa.iadd` operation computes the component-wise sum of two 32-bit
    integers, `$dst = $lhs + $rhs`. No carry or borrow beyond the 32-bit
    value of each component is performed, so the result is not sensitive to
    the signedness of its operands.

    Example:

    ```mlir
    dxsa.iadd r<0>, r<1>, r<2>
    dxsa.iadd r<0>, -r<1>, -r<2>
    dxsa.iadd r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imax
//===----------------------------------------------------------------------===//

def DXSA_IMax : DXSA_BinaryOp<"imax"> {
  let summary = "component-wise signed integer maximum";
  let description = [{
    The `dxsa.imax` operation computes the component-wise maximum of two
    32-bit signed integers, `$dst = $lhs > $rhs ? $lhs : $rhs`.

    Example:

    ```mlir
    dxsa.imax r<0>, r<1>, r<2>
    dxsa.imax r<0>, -r<1>, -r<2>
    dxsa.imax r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imin
//===----------------------------------------------------------------------===//

def DXSA_IMin : DXSA_BinaryOp<"imin"> {
  let summary = "component-wise signed integer minimum";
  let description = [{
    The `dxsa.imin` operation computes the component-wise minimum of two
    32-bit signed integers, `$dst = $lhs < $rhs ? $lhs : $rhs`.

    Example:

    ```mlir
    dxsa.imin r<0>, r<1>, r<2>
    dxsa.imin r<0>, -r<1>, -r<2>
    dxsa.imin r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ineg
//===----------------------------------------------------------------------===//

def DXSA_INeg : DXSA_UnaryOp<"ineg"> {
  let summary = "component-wise integer two's complement";
  let description = [{
    The `dxsa.ineg` operation computes the component-wise two's complement of
    each 32-bit integer in `$src`, `$dst = -$src`.

    Example:

    ```mlir
    dxsa.ineg r<0>, r<1>
    dxsa.ineg r<0, <x>>, r<1, <y, z, w, y>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.umax
//===----------------------------------------------------------------------===//

def DXSA_UMax : DXSA_BinaryOp<"umax"> {
  let summary = "component-wise unsigned integer maximum";
  let description = [{
    The `dxsa.umax` operation computes the component-wise maximum of two
    32-bit unsigned integers, `$dst = $lhs > $rhs ? $lhs : $rhs`.

    Example:

    ```mlir
    dxsa.umax r<0>, r<1>, r<2>
    dxsa.umax r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.umin
//===----------------------------------------------------------------------===//

def DXSA_UMin : DXSA_BinaryOp<"umin"> {
  let summary = "component-wise unsigned integer minimum";
  let description = [{
    The `dxsa.umin` operation computes the component-wise minimum of two
    32-bit unsigned integers, `$dst = $lhs < $rhs ? $lhs : $rhs`.

    Example:

    ```mlir
    dxsa.umin r<0>, r<1>, r<2>
    dxsa.umin r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imad
//===----------------------------------------------------------------------===//

def DXSA_Imad : DXSA_MultiplyAddOp<"imad"> {
  let summary = "component-wise integer multiply-add";
  let description = [{
    The `dxsa.imad` operation computes the component-wise integer
    multiply-add `$dst = $lhs * $rhs + $acc`. No carry or borrow beyond the
    32-bit value of each component is performed, so the result is not
    sensitive to the signedness of its operands.

    Example:

    ```mlir
    dxsa.imad r<0>, r<1>, r<2>, r<3>
    dxsa.imad r<0>, -r<1>, r<2>, r<3>
    dxsa.imad r<0, <x>>, r<1, <y>>, r<2, <x>>, r<3, <z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.imul
//===----------------------------------------------------------------------===//

def DXSA_Imul : DXSA_Op<"imul"> {
  let summary = "component-wise integer multiply";
  let description = [{
    The `dxsa.imul` operation computes the component-wise product
    `$lhs * $rhs` as a 64-bit integer and writes the high 32 bits to
    `$dstHi` and the low 32 bits to `$dstLo`. Either destination may be
    `null` when that half of the result is not needed.

    Example:

    ```mlir
    dxsa.imul r<0>, r<1>, r<2>, r<3>
    dxsa.imul null, r<0, <z>>, r<0, <y>>, -r<0, <x>>
    dxsa.imul r<7, <x, y, z>>, r<3, <x, y, z>>, r<3, <x, y, z, x>>, r<4, <x, y, z, x>>
    ```
  }];
  let arguments = (ins
      DXSA_DstOperandAttr:$dstHi,
      DXSA_DstOperandAttr:$dstLo,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dstHi `,` $dstLo `,` $lhs `,` $rhs attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.msad
//===----------------------------------------------------------------------===//

def DXSA_Msad : DXSA_TernaryOp<"msad"> {
  let summary = "component-wise masked sum of absolute differences";
  let description = [{
    The `dxsa.msad` operation computes the component-wise masked sum of
    absolute differences. For each 32-bit component, `$src0` and `$src1`
    each hold four packed 8-bit unsigned integers, and `$src2` supplies a
    32-bit unsigned accumulation value. The result is written to `$dst`.

    Example:

    ```mlir
    dxsa.msad r<0>, r<0>, r<1>, r<2>
    dxsa.msad r<1, <x>>, r<1, <x>>, r<1, <y>>, r<1, <z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.udiv
//===----------------------------------------------------------------------===//

def DXSA_UDiv : DXSA_Op<"udiv"> {
  let summary = "component-wise unsigned integer divide";
  let description = [{
    The `dxsa.udiv` operation computes the component-wise unsigned integer
    quotient `$dst_quotient = $lhs / $rhs` and remainder
    `$dst_remainder = $lhs % $rhs` of two 32-bit unsigned integers. Division
    by zero produces `0xffffffff` for both quotient and remainder. Either
    destination may be `null` when that result is not needed.

    Example:

    ```mlir
    dxsa.udiv r<0>, r<1>, r<2>, r<3>
    dxsa.udiv null, r<1>, r<2>, r<3>
    dxsa.udiv r<0>, null, r<2>, r<3>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst_quotient,
      DXSA_DstOperandAttr:$dst_remainder,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst_quotient `,` $dst_remainder `,` $lhs `,` $rhs attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.umul
//===----------------------------------------------------------------------===//

def DXSA_UMul : DXSA_Op<"umul"> {
  let summary = "component-wise unsigned integer multiply";
  let description = [{
    The `dxsa.umul` operation computes the component-wise unsigned product
    `$lhs * $rhs` as a 64-bit integer and writes the high 32 bits to
    `$dst_hi` and the low 32 bits to `$dst_lo`. Either destination may be
    `null` when that half of the result is not needed.

    Example:

    ```mlir
    dxsa.umul r<0>, r<1>, r<2>, r<3>
    dxsa.umul null, r<1>, r<2>, r<3>
    dxsa.umul r<0>, null, r<2>, r<3>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst_hi,
      DXSA_DstOperandAttr:$dst_lo,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst_hi `,` $dst_lo `,` $lhs `,` $rhs attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.umad
//===----------------------------------------------------------------------===//

def DXSA_UMad : DXSA_MultiplyAddOp<"umad"> {
  let summary = "component-wise unsigned integer multiply-add";
  let description = [{
    The `dxsa.umad` operation computes the component-wise unsigned integer
    multiply-add `$dst = $lhs * $rhs + $acc`, keeping only the low 32 bits
    of the multiply before adding `$acc`.

    Example:

    ```mlir
    dxsa.umad r<0>, r<1>, r<2>, r<3>
    dxsa.umad r<0, <x>>, r<1, <y, z, w, y>>, r<2, <x, x, x, x>>, r<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.uaddc
//===----------------------------------------------------------------------===//

def DXSA_UAddc : DXSA_Op<"uaddc"> {
  let summary = "component-wise unsigned integer add with carry";
  let description = [{
    The `dxsa.uaddc` operation computes the component-wise unsigned integer
    sum `$dst = $lhs + $rhs` of two 32-bit operands and writes the carry
    (1 if the addition overflowed, 0 otherwise) to `$dst_carry`. The
    `$dst_carry` destination may be `null` when the carry is not needed.

    Example:

    ```mlir
    dxsa.uaddc r<0>, r<1>, r<2>, r<3>
    dxsa.uaddc r<0>, null, r<2>, r<3>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_DstOperandAttr:$dst_carry,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $dst_carry `,` $lhs `,` $rhs attr-dict";
}

//===----------------------------------------------------------------------===//
// dxsa.usubb
//===----------------------------------------------------------------------===//

def DXSA_USubb : DXSA_Op<"usubb"> {
  let summary = "component-wise unsigned integer subtract with borrow";
  let description = [{
    The `dxsa.usubb` operation computes the component-wise unsigned integer
    difference `$dst = $lhs - $rhs` of two 32-bit operands and writes the
    borrow (1 if the subtraction underflowed, 0 otherwise) to `$dst_borrow`.
    The `$dst_borrow` destination may be `null` when the borrow is not needed.

    Example:

    ```mlir
    dxsa.usubb r<0>, r<1>, r<2>, r<3>
    dxsa.usubb r<0>, null, r<2>, r<3>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_DstOperandAttr:$dst_borrow,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $dst_borrow `,` $lhs `,` $rhs attr-dict";
}

#endif // FEME_DIALECT_DXSA_IR_DXSAINTARITHOPS
