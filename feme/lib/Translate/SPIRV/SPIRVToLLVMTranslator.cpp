//===- SPIRVToLLVMTranslator.cpp - spirv dialect -> LLVM IR --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "feme/Core/Module.h"
#include "feme/Translate/LLVMIR/LLVMDialectToLLVMIRTranslator.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMDialectTranslator.h"
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

  LLVMDialectToLLVMIRTranslator ToLLVMIR;
  return ToLLVMIR.translate(std::move(*LLVMDialectModule), Ctx);
}

llvm::StringRef SPIRVToLLVMTranslator::getFromFormatName() const {
  return "spirv";
}

llvm::StringRef SPIRVToLLVMTranslator::getToFormatName() const {
  return "llvmir";
}
