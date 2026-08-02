//===- feme.cpp - FeMe command line driver -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the entry point for `feme`, FeMe's primary CLI tool (see the
// "Command Line Tool(s)" section of feme/docs/Design.md). This is currently
// a `--help`-only skeleton (roadmap step 1: Scaffolding); subsequent roadmap
// steps will hand argv to an `llvm::opt`-based options component and drive a
// `feme::Driver` (see the "Core Architectural Principle: No Global State"
// section of the design doc for why this deliberately avoids
// `llvm::cl::opt`).
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

static void printHelp(llvm::raw_ostream &OS) {
  OS << "OVERVIEW: FeMe: FrontEnd for the MiddleEnd\n\n"
     << "USAGE: feme [options] <input file>\n\n"
     << "OPTIONS:\n"
     << "  -h, --help    Display this help and exit\n\n"
     << "feme does not yet implement any translation; this is a "
        "scaffolding-only skeleton (see feme/docs/Design.md).\n";
}

int main(int argc, char **argv) {
  // Deliberately hand-rolled rather than llvm::cl::opt, per the "No Global
  // State" architectural principle in feme/docs/Design.md: feme's real
  // option parsing will be built on llvm::opt's OptTable/ArgList instead.
  for (int I = 1; I < argc; ++I) {
    llvm::StringRef Arg(argv[I]);
    if (Arg == "-h" || Arg == "--help") {
      printHelp(llvm::outs());
      return 0;
    }
  }

  printHelp(llvm::errs());
  return 1;
}
