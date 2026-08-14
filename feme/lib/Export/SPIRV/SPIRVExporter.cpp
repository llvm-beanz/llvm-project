//===- SPIRVExporter.cpp - Serializes idiomatic LLVM IR back to SPIR-V ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Export/SPIRV/SPIRVExporter.h"

#include "feme/Core/Module.h"
#include "feme/Target/Backend.h"
#include "feme/Target/TargetMachineBackend.h"

#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using namespace feme;

llvm::Error SPIRVExporter::exportModule(Module &M, const ExportOptions &Opts,
                                        Context &Ctx,
                                        llvm::raw_pwrite_stream &Out) const {
  // No SPIR-V-specific ExportOptions exist yet (see
  // feme/Export/Exporter.h); Ctx is unused too -- codegen itself needs no
  // diagnostics/IR-context beyond what M's own llvm::Module already
  // carries.
  (void)Opts;
  (void)Ctx;

  llvm::Module &LLVMModule = M.getLLVMModule();

  // Matches feme::Driver's resolveTargetTriple "spirv" branch: prefer a
  // DXIL-originated module's recovered pipeline stage, or a SPIR-V-
  // originated module's own environment, over the SPIR-V "null pipeline"
  // default (see the "Deviation: validating Backend/Translator with a
  // SPIR-V 'null pipeline'" section of feme/docs/Design.md).
  llvm::Triple Existing = LLVMModule.getTargetTriple();
  std::string TargetTriple;
  if (Existing.getArch() == llvm::Triple::dxil &&
      Existing.getOS() == llvm::Triple::ShaderModel)
    TargetTriple =
        ("spirv-unknown-vulkan-" +
         llvm::Triple::getEnvironmentTypeName(Existing.getEnvironment()))
            .str();
  else if (Existing.isSPIRV())
    TargetTriple = Existing.str();
  else
    TargetTriple = "spirv64-unknown-unknown";

  BackendOptions BackendOpts;
  BackendOpts.TargetTriple = TargetTriple;

  TargetMachineBackend Backend;
  return Backend.run(LLVMModule, BackendOpts, Out);
}

llvm::StringRef SPIRVExporter::getFormatName() const { return "spirv"; }
