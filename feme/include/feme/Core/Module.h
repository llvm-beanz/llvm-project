//===- Module.h - FeMe's cross-representation module wrapper --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Module, a small tagged-union wrapper letting
// generic FeMe code (the Driver, pipeline glue) hold "a module" without
// caring whether the underlying representation is an MLIR operation or an
// llvm::Module. See the "feme::Module" section of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_MODULE_H
#define FEME_CORE_MODULE_H

#include "mlir/IR/OwningOpRef.h"

#include <memory>

namespace llvm {
class Module;
} // namespace llvm

namespace feme {

/// A tagged union over the two in-memory representations FeMe's pipeline
/// stages produce: an MLIR operation (e.g. a `spirv.module`) or an
/// llvm::Module. This is intentionally not a new IR, just a thin wrapper.
///
/// Deviation from feme/docs/Design.md: the design sketch has `fromMLIR`
/// take an `mlir::OwningOpRef<mlir::ModuleOp>` specifically. In practice the
/// top-level op varies by format (SPIR-V import produces a
/// `mlir::spirv::ModuleOp`, not a builtin `mlir::ModuleOp`), so `fromMLIR`
/// is a function template accepting any top-level op type and Module stores
/// the type-erased `mlir::OwningOpRef<mlir::Operation *>`; callers that know
/// the concrete format cast the result back with `mlir::cast`/`mlir::dyn_cast`.
class Module {
public:
  enum class Kind { MLIR, LLVMIR };

  template <typename OpTy> static Module fromMLIR(mlir::OwningOpRef<OpTy> M) {
    return Module(mlir::OwningOpRef<mlir::Operation *>(std::move(M)));
  }
  static Module fromLLVMIR(std::unique_ptr<llvm::Module> M);

  // Out-of-line (see Module.cpp) so that unique_ptr<llvm::Module>'s
  // implicit move operations are instantiated where llvm::Module is a
  // complete type.
  Module(Module &&) noexcept;
  Module &operator=(Module &&) noexcept;

  Module(const Module &) = delete;
  Module &operator=(const Module &) = delete;

  ~Module();

  Kind getKind() const { return ModKind; }

  /// Returns the underlying MLIR operation. Asserts getKind() == MLIR.
  mlir::Operation *getMLIROperation() const;

  /// Releases ownership of the underlying MLIR operation to the caller,
  /// e.g. to hand it back to generic MLIR tooling (such as
  /// mlir-translate-style translation registries) that expects to manage
  /// the operation's lifetime itself. Asserts getKind() == MLIR. This
  /// Module must not be used again afterwards except to be destroyed or
  /// reassigned.
  mlir::OwningOpRef<mlir::Operation *> takeMLIROperation();

  /// Returns the underlying llvm::Module. Asserts getKind() == LLVMIR.
  llvm::Module &getLLVMModule() const;

private:
  explicit Module(mlir::OwningOpRef<mlir::Operation *> M);
  explicit Module(std::unique_ptr<llvm::Module> M);

  Kind ModKind;
  mlir::OwningOpRef<mlir::Operation *> MLIRModule;
  std::unique_ptr<llvm::Module> LLVMModule;
};

} // namespace feme

#endif // FEME_CORE_MODULE_H
