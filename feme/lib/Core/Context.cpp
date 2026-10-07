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
      // (roadmap L392) Every owned `mlir::MLIRContext` backs exactly one
      // short-lived, single-threaded shader-stage compile (see
      // `feme::cpu::CompiledStage::create`'s callers, e.g.
      // `Vulkan/Pipeline.cpp`'s `compileGraphicsStage`/
      // `compileComputePipeline`): a single pipeline stage's worth of IR,
      // compiled synchronously on the calling thread. MLIR's own
      // pass-manager/verifier parallelism (`mlir::parallelForEach`, used by
      // e.g. `mlir::verify`'s per-region dominance checks) has nothing to
      // gain from multiple threads here -- there is only ever one
      // top-level op being processed -- but still unconditionally lazily
      // constructs a whole `llvm::ThreadPool` (MLIR's default
      // `Threading::ENABLED`) the first time any pass needs one. Spinning
      // up (and tearing back down) an entire OS thread pool per compiled
      // shader stage is pure overhead for this single-shader, synchronous
      // use case, and the resulting thread churn under a large/sustained
      // compile workload (many pipelines created back-to-back, as CTS
      // does) was confirmed via live tracing to be the cause of the
      // runaway `llvm-worker-N` thread growth described in this entry.
      // Disabling multithreading keeps every pass/verifier call on the
      // calling thread instead, matching this constructor's actual,
      // single-shader workload.
      OwnedMLIRCtx(std::make_unique<mlir::MLIRContext>(
          mlir::MLIRContext::Threading::DISABLED)),
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

FormatRegistry &Context::getFormatRegistry() { return Registry; }

const FormatRegistry &Context::getFormatRegistry() const { return Registry; }
