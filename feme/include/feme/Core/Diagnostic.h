//===- Diagnostic.h - FeMe library diagnostics -------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Diagnostic and feme::DiagnosticHandlerTy, the
// vocabulary types feme::Context::diagnose (see feme/Core/Context.h) uses to
// deliver warnings/notes that don't abort an operation to the caller. See
// the "Diagnostics and Error Handling" section of feme/docs/Design.md: a
// fallible operation still returns llvm::Expected/llvm::Error, but
// diagnostics that accompany a *successful* result (e.g. "this flag is
// ignored for this target") have nowhere else to go.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_DIAGNOSTIC_H
#define FEME_CORE_DIAGNOSTIC_H

#include <functional>
#include <string>

namespace feme {

/// The severity of a Diagnostic. Deliberately excludes an "error" level:
/// anything that aborts the operation producing it is instead reported as
/// an llvm::Error from that operation (see "Diagnostics and Error
/// Handling" in feme/docs/Design.md), never through this path.
enum class DiagnosticSeverity {
  Warning,
  Note,
};

/// A single diagnostic message, as delivered to a Context's
/// DiagnosticHandler.
struct Diagnostic {
  DiagnosticSeverity Severity;
  std::string Message;
};

/// A callback a Context's owner supplies to receive Diagnostics raised by
/// library code using that Context. There is no default that prints
/// anywhere: per "Diagnostics are delivered through an explicit,
/// caller-supplied callback ... never printed directly to errs() by
/// library code" (feme/docs/Design.md's "No Global State" section), a
/// Context with no handler installed silently drops diagnostics, and only
/// a caller (e.g. the `feme` CLI tool) that wants them printed installs a
/// handler that does so.
using DiagnosticHandlerTy = std::function<void(const Diagnostic &)>;

} // namespace feme

#endif // FEME_CORE_DIAGNOSTIC_H
