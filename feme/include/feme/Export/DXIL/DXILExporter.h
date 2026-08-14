//===- DXILExporter.h - Serializes idiomatic LLVM IR back to DXIL -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::DXILExporter. See the "Exporter" section of
// feme/docs/Design.md: DXIL "export" was previously spelled entirely as a
// generic feme::Backend invocation (see feme::TargetMachineBackend); this
// is the same LLVM DirectX-target codegen, wrapped in the symmetric
// Exporter interface so DXIL round-trips through the same
// Importer/Exporter shape every other format does.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_EXPORT_DXIL_DXILEXPORTER_H
#define FEME_EXPORT_DXIL_DXILEXPORTER_H

#include "feme/Export/Exporter.h"

namespace feme {

/// Lowers a raised, DXIL-flavored `llvm::Module` (see the DXIL section of
/// feme/docs/Design.md) to a `DXContainer` object file using LLVM's
/// DirectX target. Requires that target to have been initialized in the
/// current process (see feme::TargetMachineBackend's header comment,
/// which this class delegates its actual codegen to); this class does not
/// itself register any targets.
class DXILExporter : public Exporter {
public:
  llvm::Error exportModule(Module &M, const ExportOptions &Opts, Context &Ctx,
                           llvm::raw_pwrite_stream &Out) const override;

  llvm::StringRef getFormatName() const override;
};

} // namespace feme

#endif // FEME_EXPORT_DXIL_DXILEXPORTER_H
