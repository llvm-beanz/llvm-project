//===- DXSAMoveOps.td - DXSA move ops --------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Move instructions of the DXSA dialect.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSAMOVEOPS
#define FEME_DIALECT_DXSA_IR_DXSAMOVEOPS

include "feme/Dialect/DXSA/IR/DXSAOpBase.td"

//===----------------------------------------------------------------------===//
// dxsa.mov
//===----------------------------------------------------------------------===//

def DXSA_Mov : DXSA_UnaryOp<"mov"> {
  let summary = "component-wise move";
  let description = [{
    The `dxsa.mov` operation copies each component of `$src` to `$dst`.
    Source modifiers other than swizzle assume floating-point data; without
    modifiers the bits are copied unaltered.

    Example:

    ```mlir
    dxsa.mov r<0>, r<1>
    dxsa.mov r<0>, -|r<1>|
    dxsa.mov r<0, <x, y>>, r<1, <y, z, x, w>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.mov_sat
//===----------------------------------------------------------------------===//

def DXSA_MovSat : DXSA_UnaryOp<"mov_sat"> {
  let summary = "component-wise move, saturated to [0, 1]";
  let description = [{
    The `dxsa.mov_sat` operation copies each component of `$src`, clamps the
    result to `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.mov_sat r<0>, r<1>
    dxsa.mov_sat r<0>, -|r<1>|
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.movc
//===----------------------------------------------------------------------===//

// Shared base for the conditional-move family: `$dst = $condition ? $src1
// : $src2`, component-wise.
class DXSA_MovConditionalOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$condition,
      DXSA_SrcOperandAttr:$src1,
      DXSA_SrcOperandAttr:$src2,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $condition `,` $src1 `,` $src2 attr-dict";
}

def DXSA_MovC : DXSA_MovConditionalOp<"movc"> {
  let summary = "component-wise conditional move";
  let description = [{
    The `dxsa.movc` operation performs a component-wise conditional move:

    ```
    $dst = ($condition != 0) ? $src1 : $src2
    ```

    Each component of `$condition` selects independently, and any nonzero
    bit pattern counts as true. The bits of the selected source are copied
    unaltered unless a source modifier requests otherwise.

    Example:

    ```mlir
    dxsa.movc r<0>, r<1>, r<2>, r<3>
    dxsa.movc r<0, <x, y>>, r<1>, r<2, <y, x>>, l(0x0)
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.movc_sat
//===----------------------------------------------------------------------===//

def DXSA_MovCSat : DXSA_MovConditionalOp<"movc_sat"> {
  let summary = "component-wise conditional move, saturated to [0, 1]";
  let description = [{
    The `dxsa.movc_sat` operation performs a component-wise conditional
    move, clamps the result to `[0.0, 1.0]`, and writes it to `$dst`:

    ```
    $dst = clamp(($condition != 0) ? $src1 : $src2, 0.0, 1.0)
    ```

    Example:

    ```mlir
    dxsa.movc_sat r<0>, r<1>, r<2>, r<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmov
//===----------------------------------------------------------------------===//

def DXSA_DMov : DXSA_UnaryOp<"dmov"> {
  let summary = "component-wise double-precision move";
  let description = [{
    The `dxsa.dmov` operation copies each double-precision component of
    `$src` to `$dst`. Source modifiers other than swizzle assume
    floating-point data; without modifiers the bits are copied unaltered.

    The source encodes double values as component pairs: (x,y) holds the
    first double (x = low 32 bits, y = high 32 bits) and (z,w) holds the
    second. Valid source swizzles are .xyzw, .xyxy, .zwxy, and .zwzw.
    Valid destination write masks are .xy, .zw, and .xyzw.

    Example:

    ```mlir
    dxsa.dmov r<0>, r<1>
    dxsa.dmov r<0, <x, y>>, r<1, <z, w, x, y>>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmov_sat
//===----------------------------------------------------------------------===//

def DXSA_DMovSat : DXSA_UnaryOp<"dmov_sat"> {
  let summary = "component-wise double-precision move, saturated to [0, 1]";
  let description = [{
    The `dxsa.dmov_sat` operation copies each double-precision component of
    `$src`, clamps the result to `[0.0, 1.0]`, and writes it to `$dst`.

    Example:

    ```mlir
    dxsa.dmov_sat r<0>, r<1>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmovc
//===----------------------------------------------------------------------===//

def DXSA_DMovC : DXSA_MovConditionalOp<"dmovc"> {
  let summary = "component-wise double-precision conditional move";
  let description = [{
    The `dxsa.dmovc` operation performs a component-wise double-precision
    conditional move:

    ```
    $dst = ($condition != 0) ? $src1 : $src2
    ```

    The first two post-swizzle components of `$condition` are treated as two
    independent 32-bit boolean values, one per output double. Doubles are
    encoded as component pairs (x = low 32 bits, y = high 32 bits) and
    (z = low 32 bits, w = high 32 bits). Valid `$dst` write masks are
    `.xy`, `.zw`, and `.xyzw`; valid `$src1`/`$src2` swizzles are `.xyzw`,
    `.xyxy`, `.zwxy`, and `.zwzw`.

    Example:

    ```mlir
    dxsa.dmovc r<0>, r<1>, r<2>, r<3>
    dxsa.dmovc r<0, <x, y>>, r<1>, r<2, <z, w, x, y>>, r<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.dmovc_sat
//===----------------------------------------------------------------------===//

def DXSA_DMovCSat : DXSA_MovConditionalOp<"dmovc_sat"> {
  let summary =
      "component-wise double-precision conditional move, saturated to [0, 1]";
  let description = [{
    The `dxsa.dmovc_sat` operation performs a component-wise double-precision
    conditional move, clamps the result to `[0.0, 1.0]`, and writes it to
    `$dst`:

    ```
    $dst = clamp(($condition != 0) ? $src1 : $src2, 0.0, 1.0)
    ```

    Example:

    ```mlir
    dxsa.dmovc_sat r<0>, r<1>, r<2>, r<3>
    ```
  }];
}

//===----------------------------------------------------------------------===//
// dxsa.swapc
//===----------------------------------------------------------------------===//

def DXSA_SwapC : DXSA_Op<"swapc"> {
  let summary = "component-wise conditional swap of two values";
  let description = [{
    The `dxsa.swapc` operation performs a component-wise conditional swap of
    the values in `$src1` and `$src2`:

    ```
    $dst0 = ($condition != 0) ? $src2 : $src1
    $dst1 = ($condition != 0) ? $src1 : $src2
    ```

    Both destinations are updated atomically with respect to the source
    values, so reading and writing the same register is well-defined.

    Example:

    ```mlir
    dxsa.swapc r<0>, r<1>, r<2>, r<3>, r<4>
    dxsa.swapc r<0, <x, y>>, r<1, <z, w>>, r<2>, r<3>, r<4>
    ```
  }];

  let arguments = (ins
      DXSA_DstOperandAttr:$dst0,
      DXSA_DstOperandAttr:$dst1,
      DXSA_SrcOperandAttr:$condition,
      DXSA_SrcOperandAttr:$src1,
      DXSA_SrcOperandAttr:$src2,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat = [{
    (`precise` $precise^)? $dst0 `,` $dst1 `,` $condition `,` $src1 `,` $src2
    attr-dict
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSAMOVEOPS
