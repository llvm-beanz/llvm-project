//===- FrontendOptions.h - feme driver options ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines `DriverOptions`, the explicit-state struct that feme's `--from`/
// `--to`/`--target` command line is parsed into (see the "Driver" and
// "Command Line Tool(s)" sections of feme/docs/Design.md), and `parseArgs`,
// which builds one from `argc`/`argv` using the `OptTable` in Options.h.
// This is deliberately a plain struct populated by explicit parsing, not a
// collection of `cl::opt` globals, so it can be constructed identically by
// the `feme` CLI and by an embedding driver (see the "Core Architectural
// Principle: No Global State" section of the design doc).
//
// This is currently a stub: only the options needed to round-trip
// `feme`'s planned CLI shape are recognized; no `Driver` yet consumes a
// `DriverOptions`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_FRONTEND_FRONTENDOPTIONS_H
#define FEME_FRONTEND_FRONTENDOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <string>

namespace feme::frontend {

/// Explicit-state options for a single `feme` invocation, populated by
/// `parseArgs` below. Deliberately a plain struct (see file header) rather
/// than `cl::opt` globals.
struct DriverOptions {
  /// Path to the input file, or "-" for standard input.
  std::string InputFilename;

  /// Path to write output to, or "-" for standard output. Empty means "not
  /// specified"; callers should apply their own default.
  std::string OutputFilename;

  /// Input format to translate from (e.g. "dxil", "dxbc", "spirv").
  std::string From;

  /// Output format, or target triple, to translate to.
  std::string To;

  /// Target triple to retarget the translated module to, if any.
  std::string Target;

  /// Whether `--help` was requested; callers should print help and exit
  /// successfully without inspecting the other fields.
  bool ShowHelp = false;

  /// Whether `--version` was requested; callers should print version
  /// information and exit successfully without inspecting the other fields.
  bool ShowVersion = false;
};

/// Parses `Args` (not including the program name) into a `DriverOptions`
/// using the `OptTable` returned by `getOptTable()`. Diagnostics for
/// malformed command lines (unknown options, missing values) are written to
/// `Diags`. Returns `std::nullopt` if parsing failed and a diagnostic was
/// already emitted.
std::optional<DriverOptions> parseArgs(llvm::ArrayRef<const char *> Args,
                                       llvm::raw_ostream &Diags);

} // namespace feme::frontend

#endif // FEME_FRONTEND_FRONTENDOPTIONS_H
