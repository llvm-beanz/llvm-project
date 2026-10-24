//===- dxil-tblgen.cpp - DXIL TableGen driver
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CodeGenIntrinsics.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/TableGen/Main.h"

static llvm::cl::opt<bool>
    GenIntrinsic("gen-intrinsic", llvm::cl::desc("Generate DXIL intrinsics"),
                 llvm::cl::Required);

int main(int argc, char **argv) {
  llvm::InitLLVM X(argc, argv);
  llvm::cl::ParseCommandLineOptions(argc, argv, "DXIL TableGen\n");
  return llvm::TableGenMain(
      argv[0], [](llvm::raw_ostream &OS, const llvm::RecordKeeper &Records) {
        dxil::EmitIntrinsics(Records, OS);
        return false;
      });
}
