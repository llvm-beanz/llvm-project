//===---------------- DXSATypes.h - DXSA Dialect Types ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIALECT_DXSA_IR_DXSATYPES_H_
#define FEME_DIALECT_DXSA_IR_DXSATYPES_H_

#include "mlir/IR/Types.h"

//===----------------------------------------------------------------------===//
// DXSA Dialect Types
//===----------------------------------------------------------------------===//

#define GET_TYPEDEF_CLASSES
#include "feme/Dialect/DXSA/IR/DXSAOpsTypes.h.inc"

#endif // FEME_DIALECT_DXSA_IR_DXSATYPES_H_
