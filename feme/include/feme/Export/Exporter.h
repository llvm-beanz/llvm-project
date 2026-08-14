//===- Exporter.h - FeMe format-export interface ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Exporter, the interface a per-format binary
// exporter implements: the symmetric half of feme::Importer
// (feme/Import/Importer.h). See the "Pipeline Abstraction" / "Exporter"
// section of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_EXPORT_EXPORTER_H
#define FEME_EXPORT_EXPORTER_H

#include "llvm/Support/Error.h"

namespace llvm {
class StringRef;
class raw_pwrite_stream;
} // namespace llvm

namespace feme {

class Context;
class Module;

/// Options controlling how an Exporter serializes its output. Currently
/// empty; expected to grow one field per format-specific knob (e.g. a
/// target DXIL validator version), mirroring ImportOptions' single-plain-
/// struct rationale in feme/Import/Importer.h -- FeMe does not use RTTI
/// (see feme/.instructions.md), so a polymorphic per-format options
/// hierarchy could not be safely downcast.
struct ExportOptions {};

/// Serializes a Module back to a format's binary encoding: the inverse of
/// an Importer. Not every format needs to support export (DXBC export is
/// not a current use case, see the "Exporter" section of
/// feme/docs/Design.md), so not every format has one. Implementors are
/// stateless, statically-linked components: the same Exporter instance
/// may be invoked concurrently from multiple threads, each passing its own
/// Context (see the "No Global State" principle in feme/docs/Design.md).
class Exporter {
public:
  virtual ~Exporter() = default;

  /// Serializes \p M (which must be in the representation this Exporter's
  /// format expects -- for FeMe's current Exporters, idiomatic LLVM IR
  /// already raised/lowered for this format, see DXILExporter/
  /// SPIRVExporter) according to \p Opts, writing the resulting bytes to
  /// \p Out. Returns an Error if \p M cannot be serialized this way (e.g.
  /// no registered LLVM target backs this format in the current build).
  virtual llvm::Error exportModule(Module &M, const ExportOptions &Opts,
                                   Context &Ctx,
                                   llvm::raw_pwrite_stream &Out) const = 0;

  /// Returns the short name of the format this Exporter produces (e.g.
  /// "spirv"), used by Driver/CLI format selection and
  /// feme::FormatRegistry.
  virtual llvm::StringRef getFormatName() const = 0;
};

} // namespace feme

#endif // FEME_EXPORT_EXPORTER_H
