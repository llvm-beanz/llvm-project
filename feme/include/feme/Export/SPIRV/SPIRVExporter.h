//===- SPIRVExporter.h - Serializes idiomatic LLVM IR back to SPIR-V -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::SPIRVExporter. See the "Exporter" section of
// feme/docs/Design.md and the "Deviation: validating Backend/Translator
// with a SPIR-V 'null pipeline'" note under "Retargeting to Native ISA":
// SPIR-V "export" was previously spelled entirely as a generic
// feme::Backend invocation (see feme::TargetMachineBackend); this is the
// same LLVM SPIRV-target codegen, wrapped in the symmetric Exporter
// interface so SPIR-V round-trips through the same Importer/Exporter
// shape every other format does.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_EXPORT_SPIRV_SPIRVEXPORTER_H
#define FEME_EXPORT_SPIRV_SPIRVEXPORTER_H

#include "feme/Export/Exporter.h"

namespace feme {

/// Lowers a raised `llvm::Module` (see the SPIR-V section of
/// feme/docs/Design.md) to a SPIR-V binary module using LLVM's in-tree
/// SPIRV target. Requires that target to have been initialized in the
/// current process (see feme::TargetMachineBackend's header comment,
/// which this class delegates its actual codegen to); this class does not
/// itself register any targets.
class SPIRVExporter : public Exporter {
public:
  llvm::Error exportModule(Module &M, const ExportOptions &Opts, Context &Ctx,
                           llvm::raw_pwrite_stream &Out) const override;

  llvm::StringRef getFormatName() const override;
};

} // namespace feme

#endif // FEME_EXPORT_SPIRV_SPIRVEXPORTER_H
