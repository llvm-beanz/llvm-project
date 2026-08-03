//===-------- BinaryParser.h - Parse DXSA binary to MLIR  ---*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_DXSA_BINARYPARSER_H
#define FEME_TARGET_DXSA_BINARYPARSER_H

#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/Support/SourceMgr.h"

namespace feme::dxsa {
/// Deserializes the given binary \p source and creates a MLIR ModuleOp in the
/// given \p context.
mlir::OwningOpRef<ModuleOp> deserialize(llvm::SourceMgr &source,
                                        mlir::MLIRContext *context);

/// Deserializes a textual listing of little-endian hex DWORDs,
/// separated by whitespace or comma.
/// This method is used in tests to store hexadeciman tokens representation
/// right inside the text body.
mlir::OwningOpRef<ModuleOp> deserializeHex(llvm::SourceMgr &source,
                                           mlir::MLIRContext *context);

/// Serializes the given MLIR \p moduleOp and writes to \p output.
mlir::LogicalResult serialize(mlir::ModuleOp moduleOp,
                              llvm::raw_ostream &output);
} // namespace feme::dxsa

#endif // FEME_TARGET_DXSA_BINARYPARSER_H
