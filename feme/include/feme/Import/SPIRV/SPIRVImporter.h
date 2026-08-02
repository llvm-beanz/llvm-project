//===- SPIRVImporter.h - SPIR-V binary importer ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::SPIRVImporter, a thin Importer wrapper around
// MLIR's existing `spirv` dialect deserializer. See the "SPIR-V import"
// roadmap step and "SPIR-V -> MLIR spirv dialect" section of
// feme/docs/Design.md: FeMe does not define its own SPIR-V representation,
// it reuses mlir::spirv::deserialize.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMPORT_SPIRV_SPIRVIMPORTER_H
#define FEME_IMPORT_SPIRV_SPIRVIMPORTER_H

#include "feme/Import/Importer.h"

namespace feme {

/// Imports a SPIR-V binary module (a stream of 32-bit words per the SPIR-V
/// specification) into an `mlir::spirv::ModuleOp`, via
/// mlir::spirv::deserialize.
class SPIRVImporter : public Importer {
public:
  llvm::Expected<Module> import(llvm::MemoryBufferRef Buffer,
                                const ImportOptions &Opts,
                                Context &Ctx) const override;

  llvm::StringRef getFormatName() const override;
};

} // namespace feme

#endif // FEME_IMPORT_SPIRV_SPIRVIMPORTER_H
