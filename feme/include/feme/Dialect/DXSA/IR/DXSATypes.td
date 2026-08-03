//===----------------- DXSATypes.td - DXSA dialect types ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSATYPES
#define FEME_DIALECT_DXSA_IR_DXSATYPES

include "mlir/IR/AttrTypeBase.td"
include "feme/Dialect/DXSA/IR/DXSADialect.td"

//===----------------------------------------------------------------------===//
// DXSA Types
//===----------------------------------------------------------------------===//

class DXSA_Type<string name, string typeMnemonic> : TypeDef<DXSADialect,
                                                            name> {
  let mnemonic = typeMnemonic;
}

def DXSA_IndexType : DXSA_Type<"Index", "index"> {
  let summary = "dxsa index type";
  let description = [{
    TODO
  }];
}

// The `Legacy` prefix frees the bare `OperandType` C++ name for the new
// `DXSA_OperandType` enum (the operand-kind classifier shared by
// `#dxsa.dst_operand` / `#dxsa.src_operand`). The textual mnemonic
// `!dxsa.operand` is unchanged so existing tests keep working.
def DXSA_LegacyOperandType : DXSA_Type<"LegacyOperand", "operand"> {
  let summary = "dxsa legacy operand type";
  let description = [{
    TODO
  }];
}

#endif // FEME_DIALECT_DXSA_IR_DXSATYPES
