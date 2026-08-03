//===- DXSABitwiseOps.td - DXSA bitwise ops -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bitwise instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSABITWISEOPS
#define FEME_DIALECT_DXSA_IR_DXSABITWISEOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.and
//===----------------------------------------------------------------------===//

def DXSA_And : DXSA_BinaryOp<"and"> {
  let summary = "component-wise logical AND";
  let description = [{
    The `dxsa.and` operation computes the component-wise logical AND of
    each pair of 32-bit values, `$dst = $lhs & $rhs`.

    Example:

    ```mlir
    dxsa.and r<0>, r<1>, r<2>
    dxsa.and r<0, <x, y>>, r<1, <y, z, x, w>>, r<2, <z, z, z, z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.bfrev
//===----------------------------------------------------------------------===//

def DXSA_BFRev : DXSA_UnaryOp<"bfrev"> {
  let summary = "reverse the bits in a number";
  let description = [{
    The `dxsa.bfrev` operation reverses the bits in each 32-bit value in
    $src, `$dst = bitreverse($src)`.

    Example:

    ```mlir
    dxsa.bfrev o<0>, v<0, <y, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.countbits
//===----------------------------------------------------------------------===//

def DXSA_CountBits : DXSA_UnaryOp<"countbits"> {
  let summary = "count the number of bits set in a number";
  let description = [{
    The `dxsa.countbits` operation counts the number of bits set in
    each 32-bit value in $src, `$dst = ctpop($src)`.

    Example:

    ```mlir
    dxsa.countbits o<0>, v<0, <y, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.firstbit_hi
//===----------------------------------------------------------------------===//

def DXSA_FirstBitHi : DXSA_UnaryOp<"firstbit_hi"> {
  let summary = "find the first bit set in a number from the MSB";
  let description = [{
    The `dxsa.firstbit_hi` operation finds the first bit set in each 32-bit
    value in $src from the MSB, `$dst = 31 - ctlz($src)`.

    Example:

    ```mlir
    dxsa.firstbit_hi o<0>, v<0, <y, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.firstbit_lo
//===----------------------------------------------------------------------===//

def DXSA_FirstBitLo : DXSA_UnaryOp<"firstbit_lo"> {
  let summary = "find the first bit set in a number from the LSB";
  let description = [{
    The `dxsa.firstbit_lo` operation finds the first bit set in each 32-bit
    value in $src from the LSB, `$dst = $src ? cttz($src) : -1`.

    Example:

    ```mlir
    dxsa.firstbit_lo o<0>, v<0, <y, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.firstbit_shi
//===----------------------------------------------------------------------===//

def DXSA_FirstBitSHi : DXSA_UnaryOp<"firstbit_shi"> {
  let summary = "find the first different bit in a number from the MSB";
  let description = [{
    The `dxsa.firstbit_shi` operation finds the first bit in each 32-bit
    value in $src from the MSB that differs from the MSB,
    `$dst = 31 - ctlz($src ^ ($src >> 31))`.

    Example:

    ```mlir
    dxsa.firstbit_shi o<0>, v<0, <y, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ibfe
//===----------------------------------------------------------------------===//

def DXSA_IBFE : DXSA_TernaryOp<"ibfe"> {
  let summary = "signed bit field extract";
  let description = [{
    The `dxsa.ibfe` operation takes a range of bits of a number, shifts
    those bits to the LSB, and fills the remaining bits with the MSB of
    the range.

    Example:

    ```mlir
    dxsa.ibfe r<0, min16i, <x>>, l(0x1B), l(0x5), cb<[0, 0], vector, min16i, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ishl
//===----------------------------------------------------------------------===//

def DXSA_IShl : DXSA_BinaryOp<"ishl"> {
  let summary = "component-wise shift left";
  let description = [{
    The `dxsa.ishl` operation computes the component-wise shift of
    each 32-bit value in $lhs left by $rhs, `$dst = $lhs << $rhs`.

    Example:

    ```mlir
    dxsa.ishl r<0>, r<1>, r<2>
    dxsa.ishl r<0, <x, y>>, r<1, <y, z, x, w>>, r<2, <z, z, z, z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ishr
//===----------------------------------------------------------------------===//

def DXSA_IShr : DXSA_BinaryOp<"ishr"> {
  let summary = "component-wise arithmetic shift right";
  let description = [{
    The `dxsa.ishr` operation computes the component-wise arithmetic
    shift of each 32-bit value in $lhs right by $rhs,
    `$dst = $lhs >> $rhs`.

    Example:

    ```mlir
    dxsa.ishr r<0>, r<1>, r<2>
    dxsa.ishr r<0, <x, y>>, r<1, <y, z, x, w>>, r<2, <z, z, z, z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.not
//===----------------------------------------------------------------------===//

def DXSA_Not : DXSA_UnaryOp<"not"> {
  let summary = "component-wise one's complement";
  let description = [{
    The `dxsa.not` operation performs a component-wise one's complement of
    each 32-bit value in $src, `$dst = ~$src`.

    Example:

    ```mlir
    dxsa.not o<0>, v<0, <y, x, x, x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.or
//===----------------------------------------------------------------------===//

def DXSA_Or : DXSA_BinaryOp<"or"> {
  let summary = "component-wise logical OR";
  let description = [{
    The `dxsa.or` operation computes the component-wise logical OR of
    each pair of 32-bit values, `$dst = $lhs | $rhs`.

    Example:

    ```mlir
    dxsa.or r<0>, r<1>, r<2>
    dxsa.or r<0, <x, y>>, r<1, <y, z, x, w>>, r<2, <z, z, z, z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ubfe
//===----------------------------------------------------------------------===//

def DXSA_UBFE : DXSA_TernaryOp<"ubfe"> {
  let summary = "unsigned bit field extract";
  let description = [{
    The `dxsa.ubfe` operation takes a range of bits of a number, shifts
    those bits to the LSB, and sets the remaining bits to 0.

    Example:

    ```mlir
    dxsa.ubfe r<0, min16u, <x>>, l(0x1B), l(0x5), cb<[0, 0], vector, min16u, <x>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.ushr
//===----------------------------------------------------------------------===//

def DXSA_UShr : DXSA_BinaryOp<"ushr"> {
  let summary = "component-wise logical shift right";
  let description = [{
    The `dxsa.ushr` operation computes the component-wise logical shift
    of each 32-bit value in $lhs right by $rhs,
    `$dst = $lhs >> $rhs`.

    Example:

    ```mlir
    dxsa.ushr r<0>, r<1>, r<2>
    dxsa.ushr r<0, <x, y>>, r<1, <y, z, x, w>>, r<2, <z, z, z, z>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.xor
//===----------------------------------------------------------------------===//

def DXSA_Xor : DXSA_BinaryOp<"xor"> {
  let summary = "component-wise logical XOR";
  let description = [{
    The `dxsa.xor` operation computes the component-wise logical XOR of
    each pair of 32-bit values, `$dst = $lhs ^ $rhs`.

    Example:

    ```mlir
    dxsa.xor r<0>, r<1>, r<2>
    dxsa.xor r<0, <x, y>>, r<1, <y, z, x, w>>, r<2, <z, z, z, z>>
    ```
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSABITWISEOPS
