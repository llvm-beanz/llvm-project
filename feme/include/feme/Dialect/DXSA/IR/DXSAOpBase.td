//===- DXSAOpBase.td - DXSA op base classes --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Base classes shared by the DXSA operation families.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSAOPBASE
#define FEME_DIALECT_DXSA_IR_DXSAOPBASE

include "feme/Dialect/DXSA/IR/DXSADialect.td"
include "feme/Dialect/DXSA/IR/DXSAOperand.td"

//===----------------------------------------------------------------------===//
// DXSA op base class
//===----------------------------------------------------------------------===//

// Base class for all operations in this dialect.
class DXSA_Op<string mnemonic, list<Trait> traits = []> :
    Op<DXSADialect, mnemonic, traits>;

//===----------------------------------------------------------------------===//
// DXSA shared bases for ops with inline operands
//===----------------------------------------------------------------------===//

class DXSA_NoOperandOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins);
  let results = (outs);
  let assemblyFormat = "attr-dict";
}

class DXSA_UnaryOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $src attr-dict";
}

class DXSA_BinaryOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $lhs `,` $rhs attr-dict";
}

class DXSA_TernaryOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$src0,
      DXSA_SrcOperandAttr:$src1,
      DXSA_SrcOperandAttr:$src2,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $src0 `,` $src1 `,` $src2 attr-dict";
}

// Shared base for the multiply-add family: `$dst = $lhs * $rhs + $acc`.
class DXSA_MultiplyAddOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins
      DXSA_DstOperandAttr:$dst,
      DXSA_SrcOperandAttr:$lhs,
      DXSA_SrcOperandAttr:$rhs,
      DXSA_SrcOperandAttr:$acc,
      OptionalAttr<DXSA_ComponentMaskAttr>:$precise);
  let results = (outs);
  let assemblyFormat =
      "(`precise` $precise^)? $dst `,` $lhs `,` $rhs `,` $acc attr-dict";
}

class DXSA_GsStreamIndexOp<string mnemonic> : DXSA_Op<mnemonic> {
  let arguments = (ins ConfinedAttr<I32Attr,
      [IntNonNegative, IntMaxValue<3>]>:$index);
  let assemblyFormat = [{ $index attr-dict }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSAOPBASE
