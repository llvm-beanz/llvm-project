//===- TranslateRegistration.cpp - feme-translate hooks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/DXSA/TranslateRegistration.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Translate/DXSA/DXSAToLLVMIRTranslator.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace mlir;

namespace {
// A cl::opt scoped to this testing-only feme-translate hook: acceptable per
// the "narrowly-scoped, testing-only entrypoints" exception to feme's "No
// Global State" cl::opt ban (feme/docs/Design.md); feme::Backend and
// feme::BackendOptions never use cl::opt.
llvm::cl::opt<std::string> DXBCContainerOpt(
    "dxbc-container",
    llvm::cl::desc(
        "path to a full DXContainer to read real ISGN/OSGN signature "
        "element names/types from, overriding the names this translation "
        "would otherwise synthesize from the input dxsa.module's "
        "declarations (see feme/docs/Design.md's \"Building complete "
        "legacy DXBC containers for testing\")"));

/// Converts \p Sig's elements into `feme::dxsa::ContainerSignatureElement`s.
llvm::SmallVector<feme::dxsa::ContainerSignatureElement>
toContainerSignature(const llvm::object::DirectX::LegacySignature &Sig) {
  llvm::SmallVector<feme::dxsa::ContainerSignatureElement> Elements;
  for (const auto &Param : Sig)
    Elements.push_back({Sig.getName(Param.NameOffset).str(), Param.Index,
                       Param.Register, Param.Mask,
                       static_cast<unsigned>(Param.SystemValue),
                       static_cast<unsigned>(Param.CompType)});
  return Elements;
}
} // namespace

void feme::registerDXSAToLLVMIRTranslation() {
  TranslateFromMLIRRegistration Registration{
      "dxsa-to-llvmir",
      "Translate a decoded DXBC program to DXIL-shaped LLVM IR",
      [](ModuleOp Source, raw_ostream &Output) -> LogicalResult {
        llvm::SmallVector<feme::dxsa::ContainerSignatureElement> RealInputs;
        llvm::SmallVector<feme::dxsa::ContainerSignatureElement> RealOutputs;
        if (!DXBCContainerOpt.empty()) {
          auto BufferOrErr =
              llvm::MemoryBuffer::getFile(DXBCContainerOpt);
          if (!BufferOrErr) {
            Source.emitError("could not open '" + DXBCContainerOpt +
                             "': " + BufferOrErr.getError().message());
            return failure();
          }
          llvm::Expected<llvm::object::DXContainer> ContainerOrErr =
              llvm::object::DXContainer::create(
                  (*BufferOrErr)->getMemBufferRef());
          if (!ContainerOrErr) {
            Source.emitError("failed to parse '" + DXBCContainerOpt +
                             "': " +
                             llvm::toString(ContainerOrErr.takeError()));
            return failure();
          }
          RealInputs =
              toContainerSignature(ContainerOrErr->getLegacyInputSignature());
          RealOutputs = toContainerSignature(
              ContainerOrErr->getLegacyOutputSignature());
        }

        llvm::LLVMContext Context;
        std::unique_ptr<llvm::Module> Translated = feme::dxsa::translateToLLVMIR(
            Source, Context, RealInputs, RealOutputs);
        if (!Translated)
          return failure();
        Translated->print(Output, /*AAW=*/nullptr);
        return success();
      },
      [](DialectRegistry &Registry) {
        Registry.insert<feme::dxsa::DXSADialect>();
      }};
}
