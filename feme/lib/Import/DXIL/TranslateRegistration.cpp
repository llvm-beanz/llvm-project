//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/DXIL/TranslateRegistration.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Import/Importer.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme;

// Runs feme::DXILImporter on the single input buffer in `sourceMgr`,
// printing the resulting llvm::Module's textual IR to `output`. Unlike
// SPIRVImporter's registration, this does not need MLIRCtx: DXIL import
// produces a plain llvm::Module (see feme/docs/Design.md's DXIL section),
// so this uses a private, single-use feme::Context rather than sharing one
// with mlir-translate's MLIRContext.
static mlir::LogicalResult
importDXIL(const std::shared_ptr<llvm::SourceMgr> &SourceMgr,
           llvm::raw_ostream &Output, mlir::MLIRContext *) {
  assert(SourceMgr->getNumBuffers() == 1 && "expected one buffer");
  const llvm::MemoryBuffer *Input =
      SourceMgr->getMemoryBuffer(SourceMgr->getMainFileID());

  Context Ctx;
  DXILImporter Importer;
  llvm::Expected<Module> Result =
      Importer.import(Input->getMemBufferRef(), ImportOptions{}, Ctx);
  if (!Result) {
    llvm::errs() << "error: " << llvm::toString(Result.takeError()) << "\n";
    return mlir::failure();
  }

  Result->getLLVMModule().print(Output, /*AAW=*/nullptr);
  return mlir::success();
}

void feme::registerDXILImportTranslation() {
  mlir::TranslateRegistration Registration(
      "import-dxil",
      "import a DXIL bitcode or DXContainer module via feme::DXILImporter",
      importDXIL);
}
