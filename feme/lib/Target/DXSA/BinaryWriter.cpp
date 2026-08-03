//===- BinaryWriter.cpp - Serialize the dxsa dialect to DXBC binary ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is currently an unimplemented stub inherited from the `wip/dxsa-mlir`
// prototype this dialect was migrated from (see feme/docs/Design.md's "DXBC
// -> new MLIR `dxsa` dialect" section): only the parser/disassembler
// direction (BinaryParser.cpp) is mature today. Implementing this -- the
// `dxsa` dialect's actual DXBC *export* path, used by feme's real
// Exporter/Backend pipeline -- is tracked as a hard prerequisite for DXBC
// export in the Design.md roadmap, and is distinct from `dxbc-as` (see
// feme/lib/DXBC/Assembler), the standalone, MLIR-independent DXBC assembler
// used to build human-readable test fixtures for the DXBC *importer*.
//
//===----------------------------------------------------------------------===//

#include "feme/Target/DXSA/BinaryParser.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugLog.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/LogicalResult.h"

#include "d3d12TokenizedProgramFormat.hpp"

#define DEBUG_TYPE "export-dxsa-bin"

using namespace mlir;
using namespace llvm;

namespace feme::dxsa {
LogicalResult serialize(mlir::ModuleOp source, raw_ostream &output) {
  Region &region = source.getRegion();
  assert(region.hasOneBlock() && "invalid module");
  return failure();
}
} // namespace feme::dxsa
