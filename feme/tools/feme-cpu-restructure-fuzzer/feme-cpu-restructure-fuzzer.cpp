//===- feme-cpu-restructure-fuzzer.cpp - Fuzzer for CFG restructurization ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 5's layer 4 (see the "CFG restructurization test
// suite" section of feme/docs/FeMeCPUDesign.md): interprets its input as a
// feme::cpu::generateCFGIR seed (plus a handful of small option knobs --
// see feme::cpu::CFGGenOptions) rather than raw IR text directly, since
// libFuzzer mutates bytes and a generator seed is exactly the kind of
// small, structured input that mutates into other valid ones. Runs the
// generated shader through feme::cpu::PreparePass and asserts
// feme::cpu::verifyStructured's postconditions on the result, the same
// thing the named-shape corpus and its `-verify-structured` `lit` tests
// check by hand. A failing seed reduces to a new file in
// feme/test/Transforms/CPU/CFG/, by hand or with `llvm-reduce`.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/CFGGen.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/VerifyStructured.h"

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  uint64_t Seed = 0;
  for (size_t I = 0; I < Size && I < 8; ++I)
    Seed = (Seed << 8) | Data[I];

  CFGGenOptions Opts;
  Opts.Seed = Seed;
  Opts.MaxDepth = Size > 8 ? 2 + (Data[8] % 4) : 3;        // 2..5
  Opts.MaxConstructs = Size > 9 ? 4 + (Data[9] % 20) : 12; // 4..23
  Opts.AllowDivergent = Size <= 10 || (Data[10] & 1);
  Opts.AllowLoops = Size <= 10 || (Data[10] & 2);
  Opts.AllowUnstructured = Size <= 10 || (Data[10] & 4);

  std::string IR = generateCFGIR(Opts);

  // A fresh LLVMContext per input, matching how other in-tree fuzzers
  // (e.g. llvm-dis-fuzzer) use a fresh one, and how FeMe's own importer
  // fuzzers use a fresh feme::Context: nothing should rely on state
  // surviving across calls (see the "No Global State" principle in
  // feme/docs/Design.md).
  LLVMContext Ctx;
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(IR, Err, Ctx);
  if (!M)
    report_fatal_error(
        "feme-cpu-restructure-fuzzer: generator produced unparseable IR");
  if (verifyModule(*M, &errs()))
    report_fatal_error(
        "feme-cpu-restructure-fuzzer: generator produced an invalid module");

  ModuleAnalysisManager MAM;
  PreparePass().run(*M, MAM);

  Function *F = M->getFunction("main");
  if (!F || !verifyStructured(*F, &errs()))
    report_fatal_error(
        "feme-cpu-restructure-fuzzer: feme-cpu-prepare's output is not "
        "structured");

  return 0;
}
