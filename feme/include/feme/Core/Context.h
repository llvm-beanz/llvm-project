//===- Context.h - FeMe session context ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Context, the top-level object scoping a single
// FeMe "session". See the "feme::Context" section of feme/docs/Design.md
// for the rationale behind this type and the "No Global State" principle it
// exists to support.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_CONTEXT_H
#define FEME_CORE_CONTEXT_H

#include <memory>

namespace llvm {
class LLVMContext;
} // namespace llvm

namespace mlir {
class MLIRContext;
} // namespace mlir

namespace feme {

/// Options controlling how a Context is constructed. Currently empty; this
/// is expected to grow (e.g. flags controlling which MLIR dialects to
/// register) as later roadmap steps land.
struct ContextOptions {};

/// Context is the top-level object scoping a single FeMe session: it owns
/// the LLVMContext/MLIRContext that all IR produced during that session
/// lives in. Every FeMe entry point takes an explicit Context so that no
/// FeMe library code relies on process-global state; callers that need
/// concurrency create one Context per thread (see the "Core Architectural
/// Principle: No Global State" section of feme/docs/Design.md).
///
/// Context is movable but not copyable: it owns unique resources (the
/// underlying LLVMContext/MLIRContext) that must not be aliased.
class Context {
public:
  explicit Context(ContextOptions Options = {});
  ~Context();

  Context(Context &&) = default;
  Context &operator=(Context &&) = default;

  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;

  /// Returns the LLVMContext backing this session. All llvm::Modules
  /// produced while using this Context are owned by this LLVMContext.
  llvm::LLVMContext &getLLVMContext();

  /// Returns the MLIRContext backing this session. All mlir::ModuleOps
  /// produced while using this Context are owned by this MLIRContext.
  mlir::MLIRContext &getMLIRContext();

private:
  std::unique_ptr<llvm::LLVMContext> LLVMCtx;
  std::unique_ptr<mlir::MLIRContext> MLIRCtx;
};

} // namespace feme

#endif // FEME_CORE_CONTEXT_H
