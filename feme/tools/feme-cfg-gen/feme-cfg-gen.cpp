//===- feme-cfg-gen.cpp - Seeded CFG-shaped shader generator --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// feme-cfg-gen is the command-line front end for feme::cpu::generateCFGIR
// (see feme/include/feme/Transforms/CPU/CFGGen.h): roadmap milestone 5's
// layer 3 generator (see the "CFG restructurization test suite" section of
// feme/docs/FeMeCPUDesign.md). Prints the generated shader's textual LLVM
// IR to stdout (or `-o`).
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/CFGGen.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::opt<uint64_t> Seed("seed", cl::init(0),
                         cl::desc("Seeds the generator's PRNG"));
  cl::opt<unsigned> MaxDepth(
      "max-depth", cl::init(3),
      cl::desc("How deeply constructs may nest (default: 3)"));
  cl::opt<unsigned> MaxConstructs(
      "max-constructs", cl::init(12),
      cl::desc("The construct budget the generator spends (default: 12)"));
  cl::opt<bool> Divergent(
      "divergent", cl::init(true),
      cl::desc("Allow divergent (thread-id-derived) branch conditions"));
  cl::opt<bool> Loops("loops", cl::init(true),
                      cl::desc("Allow loops with random break/continue"));
  cl::opt<bool> Unstructured(
      "unstructured", cl::init(false),
      cl::desc("Allow unstructured edges that make the result irreducible"));
  cl::opt<std::string> OutputFilename("o", cl::init("-"),
                                      cl::desc("Output filename"),
                                      cl::value_desc("filename"));

  cl::ParseCommandLineOptions(argc, argv,
                              "FeMe seeded CFG-shaped shader generator\n");

  CFGGenOptions Opts;
  Opts.Seed = Seed;
  Opts.MaxDepth = MaxDepth;
  Opts.MaxConstructs = MaxConstructs;
  Opts.AllowDivergent = Divergent;
  Opts.AllowLoops = Loops;
  Opts.AllowUnstructured = Unstructured;

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::OF_TextWithCRLF);
  if (EC) {
    errs() << "feme-cfg-gen: " << EC.message() << "\n";
    return 1;
  }

  Out.os() << generateCFGIR(Opts);
  Out.keep();
  return 0;
}
