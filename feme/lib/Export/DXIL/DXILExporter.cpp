//===- DXILExporter.cpp - Serializes idiomatic LLVM IR back to DXIL ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Export/DXIL/DXILExporter.h"

#include "feme/Core/Module.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Target/Backend.h"
#include "feme/Target/TargetMachineBackend.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>

using namespace feme;

namespace {

/// The pipeline stage a module's entry point declares, in the spelling a
/// target triple's environment component uses for it -- see
/// `getEntryPointStageName` in Driver.cpp, whose own comment this mirrors
/// (duplicated here rather than shared, matching this file's existing
/// "Driver.cpp's own copy of this exact fallback" precedent below).
std::optional<llvm::StringRef> getEntryPointStageName(const llvm::Module &M) {
  for (const llvm::Function &F : M)
    if (std::optional<ShaderStage> Stage = getShaderStage(F))
      return llvm::Triple::getEnvironmentTypeName(
          getEnvironmentForShaderStage(*Stage));
  return std::nullopt;
}

} // namespace

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
  // the full rationale. A SPIR-V-derived module's entry point still knows
  // its own pipeline stage (see `getEntryPointStageName`); LLVM's DirectX
  // codegen rejects a stage-specific op like `llvm.dx.thread.id` outright for
  // the stage-less "library" default, so that fallback only applies once
  // nothing else is known.
  llvm::Triple Existing = LLVMModule.getTargetTriple();
  std::string TargetTriple;
  if (Existing.getArch() == llvm::Triple::dxil &&
      Existing.getOS() == llvm::Triple::ShaderModel)
    TargetTriple = Existing.str();
  else if (std::optional<llvm::StringRef> Stage =
               getEntryPointStageName(LLVMModule))
    TargetTriple = ("dxil-unknown-shadermodel6.5-" + *Stage).str();
  else
    TargetTriple = "dxil-unknown-shadermodel6.5-library";

  BackendOptions BackendOpts;
  BackendOpts.TargetTriple = TargetTriple;

  TargetMachineBackend Backend;
  return Backend.run(LLVMModule, BackendOpts, Out);
}

llvm::StringRef DXILExporter::getFormatName() const { return "dxil"; }
