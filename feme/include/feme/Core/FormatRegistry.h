//===- FormatRegistry.h - FeMe format Importer/Exporter registry -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::FormatRegistry, the registry of available
// Importers/Exporters that `feme::Context::getFormatRegistry` exposes. See
// the "feme::Context" section of feme/docs/Design.md and the
// "FormatRegistry" row of the "Core library plumbing" table in
// feme/docs/Roadmap.md §1.1.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_FORMATREGISTRY_H
#define FEME_CORE_FORMATREGISTRY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <vector>

namespace feme {

class Importer;
class Exporter;

/// A registry mapping format names (e.g. "dxil", "spirv") to the
/// Importer/Exporter instances that handle them. FormatRegistry itself
/// lives in FeMeCore alongside Context so that Context can own one without
/// FeMeCore having to depend on every format library (FeMeImportDXIL,
/// FeMeExportDXIL, ...) -- see the "Deviation: FormatRegistry population"
/// note in the "Status: feme::Driver" section of feme/docs/Design.md for
/// who actually populates a Context's registry and why.
///
/// FormatRegistry stores non-owning pointers: registered Importers/
/// Exporters are expected to be statically-linked, stateless components
/// (see "Core Architectural Principle: No Global State" in
/// feme/docs/Design.md) that outlive every Context, matching how
/// `feme::detectFormat` already used file-local `static const` Importer
/// instances before this registry existed.
class FormatRegistry {
public:
  /// Registers \p Imp under its own getFormatName(). Re-registering the
  /// same format name replaces the previous lookupImporter() entry (but
  /// not the importers() list, which keeps both), matching
  /// llvm::StringMap's own insertion semantics; callers are not expected
  /// to rely on this, but it is not a use-after-registration bug either.
  void registerImporter(const Importer &Imp);

  /// Registers \p Exp under its own getFormatName(). See
  /// registerImporter's comment on re-registration.
  void registerExporter(const Exporter &Exp);

  /// Returns the Importer registered for \p FormatName, or nullptr if
  /// none has been.
  const Importer *lookupImporter(llvm::StringRef FormatName) const;

  /// Returns the Exporter registered for \p FormatName, or nullptr if
  /// none has been.
  const Exporter *lookupExporter(llvm::StringRef FormatName) const;

  /// Returns every registered Importer, in registration order. Used by
  /// Driver's input-format sniffing (feme::detectFormat), which needs to
  /// try each in turn rather than look one up by a name it doesn't yet
  /// know.
  llvm::ArrayRef<const Importer *> importers() const { return Importers; }

  /// Whether any Importer/Exporter has been registered yet. Driver uses
  /// this to populate a Context's registry lazily, at most once per
  /// Context (see the Deviation note referenced on this class).
  bool empty() const { return Importers.empty() && ExportersByName.empty(); }

private:
  llvm::StringMap<const Importer *> ImportersByName;
  std::vector<const Importer *> Importers;
  llvm::StringMap<const Exporter *> ExportersByName;
};

} // namespace feme

#endif // FEME_CORE_FORMATREGISTRY_H
