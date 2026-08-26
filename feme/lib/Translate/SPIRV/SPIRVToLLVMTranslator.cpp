//===- SPIRVToLLVMTranslator.cpp - spirv dialect -> LLVM IR --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "feme/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"
#include "feme/Core/Module.h"
#include "feme/Translate/LLVMIR/LLVMDialectToLLVMIRTranslator.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMDialectTranslator.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"

using namespace feme;

// feme::SPIRVToLLVMTranslator is a convenience composition of the two
// stages "spirv dialect -> llvm dialect"
// (feme::SPIRVToLLVMDialectTranslator) and "llvm dialect -> llvm::Module"
// (feme::LLVMDialectToLLVMIRTranslator), kept as a single Translator for
// callers (e.g. feme-translate's `--spirv-to-llvmir` flag) that want the
// end-to-end result without caring about the intermediate `llvm` dialect
// stage. See feme/docs/Design.md's "SPIR-V -> MLIR llvm dialect -> LLVM IR"
// section for why that intermediate stage is also exposed on its own.
llvm::Expected<Module> SPIRVToLLVMTranslator::translate(Module &&M,
                                                        Context &Ctx) const {
  SPIRVToLLVMDialectTranslator ToLLVMDialect;
  llvm::Expected<Module> LLVMDialectModule =
      ToLLVMDialect.translate(std::move(M), Ctx);
  if (!LLVMDialectModule)
    return LLVMDialectModule.takeError();

  // A non-builtin `Input`/`Output` variable's decorations survive the
  // conversion above as an ad hoc `llvm.mlir.global` attribute (see
  // feme::spirv::getStageIODecorationsAttrName); collect them before that
  // MLIR module is consumed below, so they can be re-attached as real LLVM
  // metadata once translateModuleToLLVMIR (inside
  // LLVMDialectToLLVMIRTranslator) has produced a genuine llvm::Module to
  // attach them to.
  feme::spirv::StageIODecorationsMap Decorations =
      feme::spirv::collectStageIODecorations(
          LLVMDialectModule->getMLIROperation());
  // A builtin interface block's (e.g. `gl_PerVertex`) own per-member
  // decorations survive the same way, under a distinct attribute (roadmap
  // H2c) -- see feme::spirv::getStageIOMemberDecorationsAttrName.
  feme::spirv::StageIOMemberDecorationsMap MemberDecorations =
      feme::spirv::collectStageIOMemberDecorations(
          LLVMDialectModule->getMLIROperation());

  LLVMDialectToLLVMIRTranslator ToLLVMIR;
  llvm::Expected<Module> LLVMIRModule =
      ToLLVMIR.translate(std::move(*LLVMDialectModule), Ctx);
  if (!LLVMIRModule)
    return LLVMIRModule.takeError();

  feme::spirv::attachStageIODecorations(Decorations,
                                        LLVMIRModule->getLLVMModule());
  feme::spirv::attachStageIOMemberDecorations(MemberDecorations,
                                              LLVMIRModule->getLLVMModule());
  return LLVMIRModule;
}

llvm::StringRef SPIRVToLLVMTranslator::getFromFormatName() const {
  return "spirv";
}

llvm::StringRef SPIRVToLLVMTranslator::getToFormatName() const {
  return "llvmir";
}
