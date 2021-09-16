//===-- llvm-driver.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;

#define LLVM_DRIVER_TOOL(tool, entry, key)                                     \
  int entry##_main(int argc, char **argv);
#include "LLVMDriverTools.def"

// This function handles the case of not recognizing the tool requested, or if
// --help or --version are passed directly to the llvm driver.
int UnknownMain(int Argc, char **Argv) {
  cl::OptionCategory LLVMDriverCategory("llvm options");
#define LLVM_DRIVER_TOOL(tool, entry, key)                                     \
  cl::SubCommand key##Subcommand(tool, tool);
#include "LLVMDriverTools.def"

  cl::HideUnrelatedOptions(LLVMDriverCategory);
  cl::ParseCommandLineOptions(Argc, Argv, "llvm compiler driver\n");
  llvm_unreachable("We should never get here, parsing should always exit.");
  return 1;
}

int main(int Argc, char **Argv) {
  llvm::StringRef LaunchedTool = sys::path::stem(Argv[0]);
  // If the driver is launched directly.
  int PassThroughArgC = Argc;
  char **PassThroughArgV = Argv;
  bool ConsumeFirstArg = false;
  if (LaunchedTool == "llvm") {
    LaunchedTool = Argv[1];
    ConsumeFirstArg = true;
  }

  // if it is launched through a symlink that is the tool name.
  typedef int (*MainFunction)(int, char **);
  MainFunction Func = StringSwitch<MainFunction>(LaunchedTool)

#define LLVM_DRIVER_TOOL(tool, entry, key) .Case(tool, entry##_main)
#include "LLVMDriverTools.def"
                          .Default(UnknownMain);
  // If the main function is unknown we don't consume any args, so that we can
  // print the appropriate help spew.
  if (Func != UnknownMain && ConsumeFirstArg) {
    --PassThroughArgC;
    ++PassThroughArgV;
  }

  return Func(PassThroughArgC, PassThroughArgV);
}
