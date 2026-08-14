//===- DXILExporter.cpp - Serializes idiomatic LLVM IR back to DXIL ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Export/DXIL/DXILExporter.h"

#include "feme/Core/Module.h"
#include "feme/Target/Backend.h"
#include "feme/Target/TargetMachineBackend.h"

#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using namespace feme;

llvm::Error DXILExporter::exportModule(Module &M, const ExportOptions &Opts,
                                       Context &Ctx,
                                       llvm::raw_pwrite_stream &Out) const {
  // No DXIL-specific ExportOptions exist yet (see feme/Export/Exporter.h);
  // Ctx is unused too -- codegen itself needs no diagnostics/IR-context
  // beyond what M's own llvm::Module already carries.
  (void)Opts;
  (void)Ctx;

  llvm::Module &LLVMModule = M.getLLVMModule();

  // Prefer a DXIL-originated module's own shader-model triple (recovered
  // by feme::dxil::MetadataRaisingPass) over a made-up default, matching
  // feme::Driver's resolveTargetTriple -- see the "dxil" branch there for
  // the full rationale.
  llvm::Triple Existing = LLVMModule.getTargetTriple();
  std::string TargetTriple =
      (Existing.getArch() == llvm::Triple::dxil &&
       Existing.getOS() == llvm::Triple::ShaderModel)
          ? Existing.str()
          : std::string("dxil-unknown-shadermodel6.5-library");

  BackendOptions BackendOpts;
  BackendOpts.TargetTriple = TargetTriple;

  TargetMachineBackend Backend;
  return Backend.run(LLVMModule, BackendOpts, Out);
}

llvm::StringRef DXILExporter::getFormatName() const { return "dxil"; }
