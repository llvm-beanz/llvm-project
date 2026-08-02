//===- DXILImporter.h - DXIL bitcode/DXContainer importer -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::DXILImporter. See the "DXIL -> stay in LLVM IR;
// raise DXIL ops back to idiomatic form" section of feme/docs/Design.md:
// DXIL is LLVM bitcode, optionally wrapped in a `DXContainer`, so importing
// it is a matter of unwrapping the container (if present) and handing the
// embedded bitcode to LLVM's own bitcode reader.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMPORT_DXIL_DXILIMPORTER_H
#define FEME_IMPORT_DXIL_DXILIMPORTER_H

#include "feme/Import/Importer.h"

namespace feme {

/// Imports a DXIL module into an `llvm::Module`. Accepts either of the two
/// encodings DXIL is distributed in: a `DXContainer` (see
/// `llvm/lib/BinaryFormat/DXContainer.h`) with an embedded DXIL bitcode
/// part, or the raw LLVM bitcode for that part on its own (with or without
/// the standard bitcode wrapper header).
///
/// Unlike SPIRVImporter, this does not yet raise `dx.op.*` calls back into
/// idiomatic LLVM IR constructs (see the DXIL section of
/// feme/docs/Design.md); the resulting `llvm::Module` is exactly what LLVM's
/// bitcode reader produces, DXIL calling convention and all. Op raising is
/// expected to land as a later, separate FeMe pass.
class DXILImporter : public Importer {
public:
  llvm::Expected<Module> import(llvm::MemoryBufferRef Buffer,
                                const ImportOptions &Opts,
                                Context &Ctx) const override;

  llvm::StringRef getFormatName() const override;
};

} // namespace feme

#endif // FEME_IMPORT_DXIL_DXILIMPORTER_H
