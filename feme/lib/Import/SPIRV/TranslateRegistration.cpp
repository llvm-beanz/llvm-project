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
#include "mlir/IR/Verifier.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

using namespace feme;

namespace {
// Exposed for testing feme::SPIRVImporter's structurization-retry logic
// directly (see ImportOptions::SPIRVEnableControlFlowStructurization's
// comment for why it defaults to off in the importer itself); every other
// caller uses `ImportOptions{}`'s own defaults instead of these flags.
llvm::cl::opt<bool> ImportEnableStructurization(
    "import-spirv-structurize-control-flow",
    llvm::cl::desc(
        "Attempt structurized SPIR-V deserialization before falling back "
        "(see feme::ImportOptions::SPIRVEnableControlFlowStructurization)"),
    llvm::cl::init(false));
llvm::cl::opt<bool> ImportFallBackToUnstructured(
    "import-spirv-fallback-to-unstructured",
    llvm::cl::desc(
        "If structurized deserialization fails, retry unstructured (see "
        "feme::ImportOptions::SPIRVFallBackToUnstructuredControlFlow)"),
    llvm::cl::init(true));
// Roadmap L35(a): `mlir::TranslateToMLIRRegistration`'s own wrapper
// (`registerTranslateToMLIRFunction` in
// mlir/lib/Tools/mlir-translate/Translation.cpp) unconditionally calls
// `mlir::verify()` on every module `--import-spirv` produces, with no way
// for a caller to opt out. That full-module verification runs every op's
// own verifier, including `mlir/lib/Dialect/SPIRV/IR/ImageOps.cpp`'s
// `verifyImageOperands`, which has a literal "TODO: Add the validation
// rules for the following Image Operands" comment followed by an
// unconditional `assert(!bitEnumContainsAny(...))` that rejects a real
// `ConstOffset`/`Offset`/`ConstOffsets`/`MinLod`/etc. image operand
// outright in any assertions-enabled build (this project's own standard
// build configuration) -- even though the real Vulkan runtime import path
// (`feme::SPIRVImporter`'s own callers in `Pipeline.cpp`) never invokes
// full op verification at all, and imports/lowers these exact operands
// correctly end-to-end (confirmed by roadmap L26/L58's own already-passing
// cases). This makes `--import-spirv` -- this project's own standard
// real-IR-reduction tool -- crash on any real repro using these operands,
// a real, narrow, pre-existing gap purely in this tool's own use of
// unconditional verification, not in the importer itself. Default off,
// preserving every existing lit test's own exact current behavior
// (verification still catches a genuinely malformed/mistranslated module);
// a user investigating a real repro that is confirmed to hit exactly this
// known, narrow gap can pass this to see the real imported IR instead of
// an assertion crash.
llvm::cl::opt<bool> ImportSkipVerify(
    "import-spirv-skip-verify",
    llvm::cl::desc(
        "Skip mlir::verify() on the module --import-spirv produces, "
        "working around a narrow upstream gap in ImageOps.cpp's own "
        "verifyImageOperands (unconditionally rejects ConstOffset/Offset/"
        "ConstOffsets/MinLod/etc. image operands via an assert, even though "
        "the real Vulkan runtime import path never hits it); use only once "
        "a real repro is confirmed to hit exactly this gap, since skipping "
        "verification can also mask a genuine structural bug"),
    llvm::cl::init(false));
} // namespace

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
  ImportOptions Opts;
  Opts.SPIRVEnableControlFlowStructurization = ImportEnableStructurization;
  Opts.SPIRVFallBackToUnstructuredControlFlow = ImportFallBackToUnstructured;
  llvm::Expected<Module> Result =
      Importer.import(Input->getMemBufferRef(), Opts, Ctx);
  if (!Result) {
    mlir::emitError(mlir::UnknownLoc::get(MLIRCtx))
        << llvm::toString(Result.takeError());
    return {};
  }
  return Result->takeMLIROperation();
}

// Roadmap L35(a): a raw file-to-file translation (registered below via
// `mlir::TranslateRegistration`, not `mlir::TranslateToMLIRRegistration`)
// rather than a to-MLIR one, specifically so this function -- not
// `mlir::registerTranslateToMLIRFunction`'s own wrapper -- controls whether
// `mlir::verify()` runs, per `ImportSkipVerify`'s own comment above. Under
// `ImportSkipVerify`, `Op.get()->print()` is also given
// `OpPrintingFlags().assumeVerified()` below: `mlir::Operation::print`'s own
// `AsmState` constructor (`verifyOpAndAdjustFlags`, mlir/lib/IR/
// AsmPrinter.cpp) unconditionally re-runs `mlir::verify()` on the whole
// operation before printing (to decide whether to fall back to generic op
// form), so skipping only this function's own explicit `mlir::verify()`
// call above is not sufficient by itself -- printing would still
// independently re-trigger the identical assert.
static mlir::LogicalResult
translateImportSPIRV(const std::shared_ptr<llvm::SourceMgr> &SourceMgr,
                     llvm::raw_ostream &Output, mlir::MLIRContext *MLIRCtx) {
  mlir::DialectRegistry Registry;
  Registry.insert<mlir::spirv::SPIRVDialect>();
  MLIRCtx->appendDialectRegistry(Registry);

  mlir::OwningOpRef<mlir::Operation *> Op = importSPIRV(*SourceMgr, MLIRCtx);
  if (!Op)
    return mlir::failure();
  if (!ImportSkipVerify && failed(mlir::verify(*Op)))
    return mlir::failure();

  mlir::OpPrintingFlags PrintFlags;
  if (ImportSkipVerify)
    PrintFlags.assumeVerified();
  Op.get()->print(Output, PrintFlags);
  return mlir::success();
}

void feme::registerSPIRVImportTranslation() {
  mlir::TranslateRegistration Registration(
      "import-spirv", "import a SPIR-V binary module via feme::SPIRVImporter",
      translateImportSPIRV);
}
