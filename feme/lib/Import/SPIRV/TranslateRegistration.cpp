//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/SPIRV/TranslateRegistration.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/Importer.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

using namespace feme;

// Runs feme::SPIRVImporter on the single input buffer in `sourceMgr`,
// wrapping `context` in a feme::Context so the Importer runs against the
// same MLIRContext mlir-translate's MlirTranslateMain already configured
// (dialect registry, printing flags, threading), rather than a private one.
static mlir::OwningOpRef<mlir::Operation *>
importSPIRV(llvm::SourceMgr &SourceMgr, mlir::MLIRContext *MLIRCtx) {
  assert(SourceMgr.getNumBuffers() == 1 && "expected one buffer");
  const llvm::MemoryBuffer *Input =
      SourceMgr.getMemoryBuffer(SourceMgr.getMainFileID());

  Context Ctx(*MLIRCtx);
  SPIRVImporter Importer;
  llvm::Expected<Module> Result =
      Importer.import(Input->getMemBufferRef(), ImportOptions{}, Ctx);
  if (!Result) {
    mlir::emitError(mlir::UnknownLoc::get(MLIRCtx))
        << llvm::toString(Result.takeError());
    return {};
  }
  return Result->takeMLIROperation();
}

void feme::registerSPIRVImportTranslation() {
  mlir::TranslateToMLIRRegistration Registration(
      "import-spirv", "import a SPIR-V binary module via feme::SPIRVImporter",
      importSPIRV, [](mlir::DialectRegistry &Registry) {
        Registry.insert<mlir::spirv::SPIRVDialect>();
      });
}
