//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/TranslateRegistration.h"

#include "feme/Target/TargetMachineBackend.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"

using namespace feme;

namespace {
// A cl::opt scoped to this testing-only feme-translate hook: acceptable
// per the "narrowly-scoped, testing-only entrypoints" exception to feme's
// "No Global State" cl::opt ban (feme/docs/Design.md); feme::Backend and
// feme::BackendOptions never use cl::opt.
llvm::cl::opt<std::string> TargetTripleOpt(
    "target-triple",
    llvm::cl::desc("target triple for the --llvm-backend translation"));
} // namespace

// Parses the single input buffer in `SourceMgr` as LLVM IR (textual .ll or
// bitcode), runs feme::TargetMachineBackend on it targeting
// `TargetTripleOpt`, and writes the resulting binary bytes to `Output`.
static mlir::LogicalResult
runLLVMBackend(const std::shared_ptr<llvm::SourceMgr> &SourceMgr,
               llvm::raw_ostream &Output, mlir::MLIRContext *) {
  assert(SourceMgr->getNumBuffers() == 1 && "expected one buffer");
  const llvm::MemoryBuffer *Input =
      SourceMgr->getMemoryBuffer(SourceMgr->getMainFileID());

  llvm::LLVMContext LLVMCtx;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M =
      llvm::parseIR(Input->getMemBufferRef(), Err, LLVMCtx);
  if (!M) {
    Err.print("feme-translate", llvm::errs());
    return mlir::failure();
  }

  BackendOptions Opts;
  Opts.TargetTriple = TargetTripleOpt;

  // TargetMachineBackend needs a seekable raw_pwrite_stream (some targets
  // patch in a header once the output size is known), whereas mlir-translate
  // hands translations a plain raw_ostream; buffer the output in memory and
  // copy it to `Output` afterward instead.
  llvm::SmallVector<char, 0> Buffer;
  llvm::raw_svector_ostream BufferOS(Buffer);
  TargetMachineBackend Backend;
  llvm::Error BackendErr = Backend.run(*M, Opts, BufferOS);
  if (BackendErr) {
    llvm::errs() << "feme-translate: " << llvm::toString(std::move(BackendErr))
                 << "\n";
    return mlir::failure();
  }

  Output.write(Buffer.data(), Buffer.size());
  return mlir::success();
}

void feme::registerTargetMachineBackendTranslation() {
  // Initialize every target configured into this build, matching how
  // tools like llc initialize targets, so `--target-triple` can name any
  // of them (not just SPIR-V, which the "null pipeline" validation in
  // feme/docs/Design.md happens to use).
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  mlir::TranslateRegistration Registration(
      "llvm-backend",
      "lower LLVM IR to a target's binary encoding via "
      "feme::TargetMachineBackend",
      runLLVMBackend);
}
