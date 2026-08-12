//===- feme-opt.cpp - FeMe pass-pipeline testing driver ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// feme-opt is an mlir-opt-style pass-pipeline driver used to lit-test FeMe's
// own passes/conversions in isolation on textual MLIR/LLVM IR (see the
// "Testing Tools" section of feme/docs/Design.md). Unlike `feme` itself,
// feme-opt is a testing-only entrypoint and may use `llvm::cl::opt` (via
// MlirOptMain/PassPipelineCLParser, or, for the LLVM IR mode below, LLVM's
// own PassBuilder), matching mlir-opt/opt convention.
//
// FeMe passes that operate on MLIR (e.g. the eventual `dxsa` lowering) run
// through MlirOptMain, exactly as originally scaffolded (roadmap step 1).
// FeMe passes that operate on plain `llvm::Module` (e.g. the DXIL op-raising
// pass, whose input/output isn't an MLIR operation, so it cannot run through
// MlirOptMain) instead run through a small `opt`-style LLVM IR pass pipeline
// driver, selected with a leading `--llvm` argument -- see runLLVMIRMode
// below and the corresponding note in feme/docs/Design.md's Testing Tools
// section.
//
//===----------------------------------------------------------------------===//

#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"
#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Transforms/AMDGPU/RaisedLowering.h"
#include "feme/Transforms/AMDGPU/ResourceLowering.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/DXIL/IntrinsicExpansion.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Transforms/SPIRV/RaisedLowering.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace llvm;

namespace {
/// Registers FeMe's own LLVM-IR-level passes so `-passes=<name>` can find
/// them by the name each pass's `name()` reports, matching how `opt` looks
/// up in-tree passes; grows one line per FeMe LLVM IR pass as they're added.
void registerFeMePasses(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::dxil::OpRaisingPass::name())
          return false;
        MPM.addPass(feme::dxil::OpRaisingPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::dxil::IntrinsicExpansionPass::name())
          return false;
        MPM.addPass(feme::dxil::IntrinsicExpansionPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::dxil::MetadataRaisingPass::name())
          return false;
        MPM.addPass(feme::dxil::MetadataRaisingPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::amdgpu::RaisedLoweringPass::name())
          return false;
        MPM.addPass(feme::amdgpu::RaisedLoweringPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::amdgpu::ResourceLoweringPass::name())
          return false;
        MPM.addPass(feme::amdgpu::ResourceLoweringPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::spirv::RaisedLoweringPass::name())
          return false;
        MPM.addPass(feme::spirv::RaisedLoweringPass());
        return true;
      });
  // FeMe CPU target passes (see feme/docs/FeMeCPUDesign.md's Pipeline
  // Overview); currently scaffolding, see each pass's own header comment.
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::PreparePass::name())
          return false;
        MPM.addPass(feme::cpu::PreparePass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::ResourceLoweringPass::name())
          return false;
        MPM.addPass(feme::cpu::ResourceLoweringPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::LinearizePass::name())
          return false;
        MPM.addPass(feme::cpu::LinearizePass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::SIMDizePass::name())
          return false;
        MPM.addPass(feme::cpu::SIMDizePass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::WaveLoweringPass::name())
          return false;
        MPM.addPass(feme::cpu::WaveLoweringPass());
        return true;
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::EntryWrapperPass::name())
          return false;
        MPM.addPass(feme::cpu::EntryWrapperPass());
        return true;
      });
  // Phase 2's analysis printer (see feme/docs/FeMeCPUDesign.md's "Phase 2:
  // Uniformity Analysis" section) is a function pass, not a module pass, so
  // it is registered against the FunctionPassManager parsing callback;
  // `PassBuilder::parsePassPipeline`'s module-level overload auto-wraps a
  // recognized leading function-pass name in `function(...)`, matching how
  // `opt -passes='print<uniformity>'` works upstream.
  PB.registerPipelineParsingCallback(
      [](StringRef Name, FunctionPassManager &FPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != feme::cpu::WaveUniformityPrinterPass::name())
          return false;
        FPM.addPass(feme::cpu::WaveUniformityPrinterPass(outs()));
        return true;
      });
}

/// A minimal `opt`-style new-pass-manager driver for FeMe's LLVM IR passes
/// (see the file comment above). Deliberately far smaller than `opt` itself
/// -- FeMe doesn't need `opt`'s legacy-pass-manager support, IR-linking
/// options, or the many analysis/debug flags; it only needs to parse
/// textual/bitcode LLVM IR, run a `-passes=` pipeline, and print the result,
/// which is all `lit`-testing a single module pass in isolation requires.
int runLLVMIRMode(int Argc, char **Argv) {
  cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input file>"),
                                     cl::init("-"));
  cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"),
                                      cl::value_desc("filename"),
                                      cl::init("-"));
  cl::opt<std::string> PassPipeline(
      "passes", cl::desc("A textual description of the pass pipeline, e.g. "
                         "'feme-dxil-raise-ops'"));
  cl::opt<bool> OutputAssembly(
      "S", cl::desc("Write output as LLVM assembly, not bitcode"));

  cl::ParseCommandLineOptions(Argc, Argv,
                              "FeMe LLVM IR pass-pipeline testing driver\n");

  SMDiagnostic Err;
  LLVMContext Context;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
  if (!M) {
    Err.print(Argv[0], errs());
    return 1;
  }

  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  registerFeMePasses(PB);
  // Not part of PassBuilder's own registry (see the "Phase 2" comment
  // above), so the FunctionAnalysisManager needs it registered directly.
  FAM.registerPass([] { return feme::cpu::WaveUniformityAnalysis(); });

  ModulePassManager MPM;
  if (Error E = PB.parsePassPipeline(MPM, PassPipeline)) {
    errs() << Argv[0] << ": " << toString(std::move(E)) << "\n";
    return 1;
  }

  MPM.run(*M, MAM);

  if (verifyModule(*M, &errs())) {
    errs() << Argv[0] << ": output module is invalid\n";
    return 1;
  }

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC,
                     OutputAssembly ? sys::fs::OF_TextWithCRLF
                                    : sys::fs::OF_None);
  if (EC) {
    errs() << Argv[0] << ": " << EC.message() << "\n";
    return 1;
  }

  if (OutputAssembly)
    M->print(Out.os(), nullptr);
  else
    WriteBitcodeToFile(*M, Out.os());

  Out.keep();
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  // `--llvm` as the leading argument selects the LLVM IR pass pipeline
  // driver (see the file comment above) instead of the default MLIR path;
  // strip it before handing the rest of argv to either driver's own option
  // parsing.
  if (argc > 1 && StringRef(argv[1]) == "--llvm") {
    InitLLVM Init(argc, argv);
    return runLLVMIRMode(argc - 1, argv + 1);
  }

  mlir::registerAllPasses();
  // FeMe's own MLIR passes; grows one line per pass as they are added.
  mlir::registerPass(feme::spirv::createConvertSPIRVToLLVMPass);

  mlir::DialectRegistry Registry;
  mlir::registerAllDialects(Registry);
  Registry.insert<feme::dxsa::DXSADialect>();

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "FeMe pass-pipeline testing driver\n", Registry));
}
