//===- FormatRegistry.cpp - Registry of FeMe's Importers/Exporters -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/FormatRegistry.h"

#include "feme/Export/Exporter.h"
#include "feme/Import/Importer.h"

using namespace feme;

void FormatRegistry::registerImporter(const Importer &Imp) {
  ImportersByName[Imp.getFormatName()] = &Imp;
  Importers.push_back(&Imp);
}

void FormatRegistry::registerExporter(const Exporter &Exp) {
  ExportersByName[Exp.getFormatName()] = &Exp;
}

const Importer *
FormatRegistry::lookupImporter(llvm::StringRef FormatName) const {
  auto It = ImportersByName.find(FormatName);
  return It == ImportersByName.end() ? nullptr : It->second;
}

const Exporter *
FormatRegistry::lookupExporter(llvm::StringRef FormatName) const {
  auto It = ExportersByName.find(FormatName);
  return It == ExportersByName.end() ? nullptr : It->second;
}
