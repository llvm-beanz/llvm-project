//===- feme.cpp - FeMe command line driver -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the entry point for `feme`, FeMe's primary CLI tool (see the
// "Command Line Tool(s)" section of feme/docs/Design.md). Argument parsing
// itself is not handled here: `main()` is a thin wrapper that hands argv to
// the `feme::frontend` library's `OptTable`-based parser (see
// feme/include/feme/Frontend/Options.h and FrontendOptions.h), constructs a
// `feme::Context`, and hands the parsed `DriverOptions` and input buffer to
// `feme::Driver::run` (feme/include/feme/Driver/Driver.h), which computes
// and executes the full import -> translate -> retarget/export chain.
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Driver/Driver.h"
#include "feme/Frontend/FrontendOptions.h"
#include "feme/Frontend/Options.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme::frontend;

static void printHelp(llvm::raw_ostream &OS) {
  getOptTable().printHelp(OS, "feme [options] <input file>",
                          "FeMe: FrontEnd for the MiddleEnd");
}

int main(int argc, char **argv) {
  std::optional<DriverOptions> Options =
      parseArgs(llvm::ArrayRef(argv + 1, argc - 1), llvm::errs());
  if (!Options) {
    printHelp(llvm::errs());
    return 1;
  }

  if (Options->ShowHelp) {
    printHelp(llvm::outs());
    return 0;
  }

  if (Options->ShowVersion) {
    llvm::outs() << "feme (LLVM/FeMe)\n";
    return 0;
  }

  if (Options->InputFilename.empty()) {
    llvm::errs() << "feme: no input file specified\n";
    printHelp(llvm::errs());
    return 1;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> InputOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(Options->InputFilename,
                                         /*IsText=*/false);
  if (std::error_code EC = InputOrErr.getError()) {
    llvm::errs() << "feme: could not open '" << Options->InputFilename
                 << "': " << EC.message() << "\n";
    return 1;
  }

  // feme::TargetMachineBackend does not itself initialize any LLVM targets
  // (see its header comment), to avoid forcing every FeMe consumer to link
  // every target's codegen library; `feme` itself, like `llc`, wants every
  // target configured into this build available for `--to`/`--target`.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  std::string OutputFilename =
      Options->OutputFilename.empty() ? "-" : Options->OutputFilename;
  std::error_code EC;
  llvm::ToolOutputFile Out(OutputFilename, EC, llvm::sys::fs::OF_None);
  if (EC) {
    llvm::errs() << "feme: could not open '" << OutputFilename
                 << "': " << EC.message() << "\n";
    return 1;
  }

  feme::Context Ctx;
  feme::Driver TheDriver(Ctx);
  llvm::Expected<feme::DriverResult> Result =
      TheDriver.run((*InputOrErr)->getMemBufferRef(), *Options);
  if (!Result) {
    llvm::errs() << "feme: " << llvm::toString(Result.takeError()) << "\n";
    return 1;
  }

  Out.os().write(Result->Output.data(), Result->Output.size());
  Out.keep();
  return 0;
}
