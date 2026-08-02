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
// feme/include/feme/Frontend/Options.h and FrontendOptions.h), matching the
// "Core Architectural Principle: No Global State" section of the design doc
// (no `llvm::cl::opt` in `feme` or library code). `feme` does not yet
// implement any translation (roadmap step 1: Scaffolding); subsequent
// roadmap steps will drive a `feme::Driver` from the parsed
// `DriverOptions`.
//
//===----------------------------------------------------------------------===//

#include "feme/Frontend/FrontendOptions.h"
#include "feme/Frontend/Options.h"

#include "llvm/Support/raw_ostream.h"

using namespace feme::frontend;

static void printHelp(llvm::raw_ostream &OS) {
  getOptTable().printHelp(OS, "feme [options] <input file>",
                          "FeMe: FrontEnd for the MiddleEnd");
  OS << "\nfeme does not yet implement any translation; this is a "
        "scaffolding-only skeleton (see feme/docs/Design.md).\n";
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

  // No import/translate/export pipeline exists yet (see roadmap step 1);
  // this is scaffolding-only.
  llvm::errs() << "feme does not yet implement any translation (see "
                  "feme/docs/Design.md).\n";
  return 1;
}
