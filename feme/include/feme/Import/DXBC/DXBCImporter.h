//===- DXBCImporter.h - legacy DXBC container importer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::DXBCImporter. See the "DXBC -> new MLIR `dxsa`
// dialect" section of feme/docs/Design.md: a legacy DXBC (Shader Model 5.0
// and earlier) module is a `DXContainer` whose tokenized shader bytecode
// lives in an `SHEX`/`SHDR` part rather than DXIL's `DXIL`/`ILDB` bitcode
// part; importing it is a matter of locating that part and handing its
// bytes to `feme::dxsa::deserialize`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMPORT_DXBC_DXBCIMPORTER_H
#define FEME_IMPORT_DXBC_DXBCIMPORTER_H

#include "feme/Import/Importer.h"

namespace feme {

/// Imports a legacy DXBC module into the `dxsa` MLIR dialect. Accepts a full
/// `DXContainer` (see `llvm/lib/BinaryFormat/DXContainer.h`) carrying an
/// `SHEX` (Shader Model 4.1+, extended opcode tokens) or `SHDR` (Shader
/// Model 4.0, no extended tokens) part -- unlike `DXILImporter`, DXBC has no
/// bare-bytecode-only encoding to also accept, since a real DXBC module is
/// never distributed outside a container.
///
/// The resulting Module wraps a `feme::dxsa::ModuleOp`, still in DXBC's own
/// register-based representation (see the "Per-Format Representation
/// Strategy" section of feme/docs/Design.md); translating it to LLVM IR is
/// `feme::dxsa::DXSAToLLVMIRTranslator`'s job.
class DXBCImporter : public Importer {
public:
  llvm::Expected<Module> import(llvm::MemoryBufferRef Buffer,
                                const ImportOptions &Opts,
                                Context &Ctx) const override;

  llvm::StringRef getFormatName() const override;
};

} // namespace feme

#endif // FEME_IMPORT_DXBC_DXBCIMPORTER_H
