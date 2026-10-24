//===- IRPrintingPasses.h - Passes to print out IR constructs ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines passes to print out IR in various granularities. The
/// PrintModulePass pass simply prints out the entire module when it is
/// executed. The PrintFunctionPass class is designed to be pipelined with
/// other FunctionPass's, and prints out the functions of the module as they
/// are processed.
///
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_IRPRINTINGPASSES_H
#define DXIL_IR_IRPRINTINGPASSES_H

#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {
class raw_ostream;
}

namespace dxil {
class BasicBlockPass;
class Function;
class FunctionPass;
class Module;
class ModulePass;
class PreservedAnalyses;

/// \brief Create and return a pass that writes the module to the specified
/// \c raw_ostream.
ModulePass *createPrintModulePass(llvm::raw_ostream &OS,
                                  const std::string &Banner = "",
                                  bool ShouldPreserveUseListOrder = false);

/// \brief Create and return a pass that prints functions to the specified
/// \c raw_ostream as they are processed.
FunctionPass *createPrintFunctionPass(llvm::raw_ostream &OS,
                                      const std::string &Banner = "");

/// \brief Create and return a pass that writes the BB to the specified
/// \c raw_ostream.
BasicBlockPass *createPrintBasicBlockPass(llvm::raw_ostream &OS,
                                          const std::string &Banner = "");

/// \brief Pass for printing a Module as LLVM's text IR assembly.
///
/// Note: This pass is for use with the new pass manager. Use the create...Pass
/// functions above to create passes for use with the legacy pass manager.
class PrintModulePass {
  llvm::raw_ostream &OS;
  std::string Banner;
  bool ShouldPreserveUseListOrder;

public:
  PrintModulePass();
  PrintModulePass(llvm::raw_ostream &OS, const std::string &Banner = "",
                  bool ShouldPreserveUseListOrder = false);

  PreservedAnalyses run(Module &M);

  static llvm::StringRef name() { return "PrintModulePass"; }
};

/// \brief Pass for printing a Function as LLVM's text IR assembly.
///
/// Note: This pass is for use with the new pass manager. Use the create...Pass
/// functions above to create passes for use with the legacy pass manager.
class PrintFunctionPass {
  llvm::raw_ostream &OS;
  std::string Banner;

public:
  PrintFunctionPass();
  PrintFunctionPass(llvm::raw_ostream &OS, const std::string &Banner = "");

  PreservedAnalyses run(Function &F);

  static llvm::StringRef name() { return "PrintFunctionPass"; }
};

} // End llvm namespace

#endif
