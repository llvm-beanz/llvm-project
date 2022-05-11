//===-- DirectXAsmPrinter.cpp - DirectX assembly writer --------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains AsmPrinters for the DirectX backend.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/DirectXTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

// The DXILAsmPrinter is mostly a stub because DXIL is just LLVM bitcode which
// gets embedded into a DXContainer file.
class DXILAsmPrinter : public AsmPrinter {
public:
  explicit DXILAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "DXIL Assembly Printer"; }

  /*void emitInstruction(const MachineInstr *MI) override {}
  void emitFunctionEntryLabel() override {}
  void emitFunctionHeader() override {}
  void emitFunctionBodyStart() override {}
  void emitFunctionBodyEnd() override {}
  void emitBasicBlockStart(const MachineBasicBlock &MBB) override {}
  void emitBasicBlockEnd(const MachineBasicBlock &MBB) override {}
  void emitGlobalVariable(const GlobalVariable *GV) override {}
  void emitEndOfAsmFile(Module &M) override {}
  bool doInitialization(Module &M) override {}
  void getAnalysisUsage(AnalysisUsage &AU) const override {}*/
};
} // namespace

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXAsmPrinter() {
  RegisterAsmPrinter<DXILAsmPrinter> X(getTheDirectXTarget());
}
