//===- Driver.cpp - FeMe full-toolchain orchestration --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Driver/Driver.h"

#include "feme/Core/Module.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Import/Importer.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "feme/Target/Backend.h"
#include "feme/Target/TargetMachineBackend.h"
#include "feme/Transforms/AMDGPU/RaisedLowering.h"
#include "feme/Transforms/AMDGPU/ResourceLowering.h"
#include "feme/Transforms/DXIL/IntrinsicExpansion.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Transforms/SPIRV/RaisedLowering.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"
#include "feme/Translate/Translator.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace feme;

Driver::Driver(Context &Ctx) : Ctx(Ctx) {}

namespace {

/// Looks up the Importer named by \p From. Returns nullptr for any name
/// other than "dxil"/"spirv" -- DXBC import is not yet implemented (see the
/// Roadmap / Milestones section of feme/docs/Design.md).
const Importer *lookupImporter(llvm::StringRef From) {
  static const DXILImporter DXIL;
  static const SPIRVImporter SPIRV;
  if (From == DXIL.getFormatName())
    return &DXIL;
  if (From == SPIRV.getFormatName())
    return &SPIRV;
  return nullptr;
}

/// Converts \p Imported (in whichever representation its format's Importer
/// produces, see the "Per-Format Representation Strategy" section of
/// feme/docs/Design.md) into a Module holding a plain llvm::Module: DXIL
/// import already produces one directly, while SPIR-V import produces an
/// `mlir::spirv::ModuleOp` that still needs `SPIRVToLLVMTranslator`.
llvm::Expected<Module> translateToLLVMIR(Module &&Imported, Context &Ctx) {
  if (Imported.getKind() == Module::Kind::LLVMIR)
    return std::move(Imported);

  SPIRVToLLVMTranslator ToLLVMIR;
  return ToLLVMIR.translate(std::move(Imported), Ctx);
}

/// Resolves `Opts.Target`/`Opts.To` (see DriverOptions' field comments) to
/// the concrete target triple `TargetMachineBackend` should retarget to:
/// `Opts.Target` wins if set (an explicit ISA retarget, e.g.
/// "amdgcn-amd-amdhsa"); otherwise `Opts.To` is consulted, which may itself
/// either name one of FeMe's own input formats ("dxil"/"spirv", each
/// resolving to that format's own LLVM backend so the module round-trips
/// back out through it) or be a target triple directly.
llvm::Expected<std::string> resolveTargetTriple(const DriverOptions &Opts,
                                                const llvm::Module &M) {
  llvm::StringRef Requested = !Opts.Target.empty()
                                  ? llvm::StringRef(Opts.Target)
                                  : llvm::StringRef(Opts.To);
  if (Requested.empty())
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "one of --to or --target must name an output format or target "
        "triple");

  if (Requested == "dxil") {
    // A DXIL input compiled by a modern toolchain (targeting a
    // "dxil-unknown-shadermodelX.Y-<stage>" triple) already carries the
    // shader model/pipeline stage its original toolchain compiled it for;
    // prefer that over a made-up default so re-emitting DXIL preserves it.
    // DXIL bitcode's own frozen module triple ("dxil-ms-dx", see the DXIL
    // section of feme/docs/Design.md) is not one of these -- `getOS()` is
    // not `ShaderModel` -- so fall back to a default shader-model triple in
    // that case (and for any other translated, non-DXIL-originated module).
    llvm::Triple Existing = M.getTargetTriple();
    if (Existing.getArch() == llvm::Triple::dxil &&
        Existing.getOS() == llvm::Triple::ShaderModel)
      return Existing.str();
    return std::string("dxil-unknown-shadermodel6.5-library");
  }

  if (Requested == "spirv") {
    // A module raised from DXIL knows which pipeline stage it implements (see
    // feme::dxil::MetadataRaisingPass), and SPIR-V spells that as the
    // environment component of a Vulkan triple -- so preserve it rather than
    // emitting a stage-less kernel-flavored module.
    llvm::Triple Existing = M.getTargetTriple();
    if (Existing.getArch() == llvm::Triple::dxil &&
        Existing.getOS() == llvm::Triple::ShaderModel)
      return ("spirv-unknown-vulkan-" +
              llvm::Triple::getEnvironmentTypeName(Existing.getEnvironment()))
          .str();
    // A SPIR-V *input* already knows exactly which SPIR-V environment it was
    // written for: feme::spirv::createConvertSPIRVToLLVMPass recovers that
    // from the `spirv.module`'s addressing and execution models and records
    // it on the module. Re-emitting SPIR-V should keep it rather than
    // flattening every shader into a kernel-flavored module.
    if (Existing.isSPIRV())
      return Existing.str();
    // Otherwise match the SPIR-V "null pipeline" validation path (see the
    // "Deviation: validating Backend/Translator with a SPIR-V 'null
    // pipeline'" section of feme/docs/Design.md): LLVM's own in-tree SPIRV
    // target derives the emitted module's execution environment from the
    // llvm::Module it is handed, not from any original SPIR-V header FeMe
    // would otherwise need to reconstruct.
    return std::string("spirv64-unknown-unknown");
  }

  return Requested.str();
}

} // namespace

llvm::Expected<DriverResult> Driver::run(llvm::MemoryBufferRef Input,
                                         const DriverOptions &Opts) const {
  const Importer *Imp = lookupImporter(Opts.From);
  if (!Imp)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "unsupported --from format '" + Opts.From +
                                       "' (expected 'dxil' or 'spirv')");

  ImportOptions ImportOpts;
  llvm::Expected<Module> Imported = Imp->import(Input, ImportOpts, Ctx);
  if (!Imported)
    return Imported.takeError();

  llvm::Expected<Module> AsLLVMIR =
      translateToLLVMIR(std::move(*Imported), Ctx);
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  llvm::Module &M = AsLLVMIR->getLLVMModule();

  // A DXIL-imported module is still in its already-lowered `dx.op.*` calling
  // convention (see feme::DXILImporter's header comment): LLVM's DirectX
  // target's own IR pipeline (in particular `DXILShaderFlagsAnalysis`)
  // requires starting from *idiomatic*, pre-lowering IR -- it asserts if it
  // ever sees a `dx.op.*` declaration, on the assumption that
  // `DXILOpLowering` (part of that same pipeline) is what produces those --
  // so retargeting to any target, DXIL included, needs `OpRaisingPass` to
  // undo that first. `MetadataRaisingPass` then recovers the module's shader
  // model, entry points, and thread group dimensions from the `dx.*` named
  // metadata they live in into the triple and `hlsl.*` function attributes
  // every later stage reads them from; it runs second because
  // `OpRaisingPass` consumes the `!dx.resources` metadata it drops. See the
  // DXIL section of feme/docs/Design.md.
  if (Opts.From == "dxil") {
    llvm::ModuleAnalysisManager MAM;
    feme::dxil::OpRaisingPass().run(M, MAM);
    feme::dxil::MetadataRaisingPass().run(M, MAM);
  }

  llvm::Expected<std::string> TargetTriple = resolveTargetTriple(Opts, M);
  if (!TargetTriple)
    return TargetTriple.takeError();

  llvm::Triple TheTriple(llvm::Triple::normalize(*TargetTriple));

  // Raised IR still uses `llvm.dx.*` intrinsics for the HLSL-specific
  // operations DXIL has dedicated ops for. LLVM's DirectX backend selects
  // those directly, so leave them alone when re-emitting DXIL; every other
  // target needs them expanded into standard LLVM IR first.
  if (!TheTriple.isDXIL()) {
    llvm::ModuleAnalysisManager MAM;
    feme::dxil::IntrinsicExpansionPass().run(M, MAM);
  }

  if (TheTriple.isSPIRV()) {
    llvm::ModuleAnalysisManager MAM;
    feme::spirv::RaisedLoweringPass().run(M, MAM);
  }

  // A raised module still uses format-agnostic `llvm.dx.*`/`llvm.spv.*`
  // intrinsics that only the AMDGPU-specific lowering pass currently
  // understands (see "Raised LLVM IR -> AMDGPU" in feme/docs/Design.md).
  if (TheTriple.isAMDGCN()) {
    llvm::ModuleAnalysisManager MAM;
    // Resource lowering runs first: it rewrites each entry point's signature
    // to take its bindings as kernel arguments, which the rest of the AMDGPU
    // lowering then sees as an ordinary function.
    feme::amdgpu::ResourceLoweringPass().run(M, MAM);
    feme::amdgpu::RaisedLoweringPass().run(M, MAM);
  }

  BackendOptions BackendOpts;
  BackendOpts.TargetTriple = *TargetTriple;

  DriverResult Result;
  llvm::raw_svector_ostream OS(Result.Output);
  TargetMachineBackend Backend;
  if (llvm::Error Err = Backend.run(M, BackendOpts, OS))
    return std::move(Err);

  return Result;
}
