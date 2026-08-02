//===- feme-opt.cpp - FeMe pass-pipeline testing driver ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// feme-opt is an mlir-opt-style pass-pipeline driver used to lit-test FeMe's
// own passes/conversions in isolation on textual MLIR (see the "Testing
// Tools" section of feme/docs/Design.md). Unlike `feme` itself, feme-opt is
// a testing-only entrypoint and may use `llvm::cl::opt` (via
// MlirOptMain/PassPipelineCLParser), matching mlir-opt convention.
//
// This is currently a scaffolding-only skeleton (roadmap step 1) that
// registers no FeMe-specific dialects or passes yet; later roadmap steps
// will register the `dxsa` dialect and FeMe's conversion passes here.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  mlir::registerAllPasses();
  // TODO: Register FeMe's own passes (e.g. DXIL op-raising, dxsa lowering)
  // here as they are implemented.

  mlir::DialectRegistry Registry;
  mlir::registerAllDialects(Registry);
  // TODO: Add FeMe's own dialects (e.g. dxsa) to Registry once they exist.

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "FeMe pass-pipeline testing driver\n", Registry));
}
