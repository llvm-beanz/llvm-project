//===- Context.cpp - FeMe session context --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"

#include "mlir/IR/MLIRContext.h"
#include "llvm/IR/LLVMContext.h"

using namespace feme;

Context::Context(ContextOptions Options)
    : LLVMCtx(std::make_unique<llvm::LLVMContext>()),
      OwnedMLIRCtx(std::make_unique<mlir::MLIRContext>()),
      MLIRCtx(OwnedMLIRCtx.get()) {
  // TODO: Eagerly register the MLIR dialects FeMe needs (spirv, llvm, func,
  // gpu, target-specific dialects, feme's own dialects) once those pipeline
  // stages exist; see the "feme::Context" section of feme/docs/Design.md.
  (void)Options;
}

Context::Context(mlir::MLIRContext &ExternalMLIRCtx)
    : LLVMCtx(std::make_unique<llvm::LLVMContext>()),
      MLIRCtx(&ExternalMLIRCtx) {}

// Out-of-line so that the destructor and move operations are emitted where
// llvm::LLVMContext and mlir::MLIRContext are complete types, per the
// pimpl-with-unique_ptr idiom.
Context::~Context() = default;
Context::Context(Context &&) noexcept = default;
Context &Context::operator=(Context &&) noexcept = default;

llvm::LLVMContext &Context::getLLVMContext() { return *LLVMCtx; }

mlir::MLIRContext &Context::getMLIRContext() { return *MLIRCtx; }

void Context::setDiagnosticHandler(DiagnosticHandlerTy Handler) {
  DiagHandler = std::move(Handler);
}

void Context::diagnose(Diagnostic D) const {
  if (DiagHandler)
    DiagHandler(D);
}
