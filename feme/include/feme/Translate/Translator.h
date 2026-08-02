//===- Translator.h - FeMe cross-format translation interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Translator, the interface a pipeline stage that
// converts a Module from one format's in-memory representation into
// another's implements. See the "Pipeline Abstraction" / "Translator"
// section of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_TRANSLATOR_H
#define FEME_TRANSLATE_TRANSLATOR_H

#include "llvm/Support/Error.h"

namespace llvm {
class StringRef;
} // namespace llvm

namespace feme {

class Context;
class Module;

/// Converts a Module from one format's representation into another's (e.g.
/// the `spirv` MLIR dialect into an llvm::Module), without necessarily
/// producing a byte-for-byte serialization of the destination format -- the
/// result can be run through that format's Exporter/Backend, or consumed
/// directly by further passes. Implementors are stateless, statically-linked
/// components: the same Translator instance may be invoked concurrently from
/// multiple threads, each passing its own Context (see the "No Global
/// State" principle in feme/docs/Design.md).
class Translator {
public:
  virtual ~Translator() = default;

  /// Converts \p M (whose representation must match getFromFormatName())
  /// into a new Module in the representation named by getToFormatName(),
  /// using \p Ctx's MLIRContext/LLVMContext for any IR produced. Takes \p M
  /// by rvalue reference to make the ownership transfer explicit: on
  /// success, \p M's underlying representation has been consumed and must
  /// not be used again except to be destroyed or reassigned.
  virtual llvm::Expected<Module> translate(Module &&M, Context &Ctx) const = 0;

  /// Returns the short name of the format this Translator consumes (e.g.
  /// "spirv"), used by Driver/CLI format selection.
  virtual llvm::StringRef getFromFormatName() const = 0;

  /// Returns the short name of the format this Translator produces (e.g.
  /// "llvmir"), used by Driver/CLI format selection.
  virtual llvm::StringRef getToFormatName() const = 0;
};

} // namespace feme

#endif // FEME_TRANSLATE_TRANSLATOR_H
