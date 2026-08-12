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
#include "feme/Optimizer/OptimizerPipeline.h"
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

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <cstring>

using namespace feme;

Driver::Driver(Context &Ctx) : Ctx(Ctx) {}

namespace {

/// Sniffs \p Buffer's binary format to select which Importer parses it, so
/// `feme` does not need an explicit `--from` flag naming it: DXIL
/// bitcode/DXContainer and SPIR-V binaries each begin with a distinct,
/// well-known magic number (see the "Command Line Tool(s)" section of
/// feme/docs/Design.md). Returns nullptr if \p Buffer's format cannot be
/// determined this way -- this covers both genuinely unrecognized input and
/// formats FeMe does not yet import (e.g. legacy DXBC bytecode, not yet
/// implemented -- see the Roadmap / Milestones section of
/// feme/docs/Design.md), since neither can be told apart from unrecognized
/// input without actually importing it.
const Importer *detectFormat(llvm::MemoryBufferRef Buffer) {
  static const DXILImporter DXIL;
  static const SPIRVImporter SPIRV;

  llvm::StringRef Data = Buffer.getBuffer();

  // A `DXContainer` (magic "DXBC", the format predates the DXIL name --
  // see llvm::object::DXContainer::parseHeader) wraps a DXIL bitcode part
  // in FeMe's supported case; raw DXIL is plain LLVM bitcode, optionally
  // with the standard bitcode wrapper header. Either encoding is
  // `DXILImporter`'s to parse -- including giving a clean diagnostic for a
  // `DXContainer` that turns out not to actually hold a DXIL part.
  if (Data.starts_with("DXBC") ||
      llvm::isBitcode(
          reinterpret_cast<const unsigned char *>(Data.data()),
          reinterpret_cast<const unsigned char *>(Data.data() + Data.size())))
    return &DXIL;

  // SPIR-V binaries are a stream of 32-bit words beginning with a fixed
  // magic number (see the SPIR-V specification's "Physical Layout of a
  // SPIR-V Module and Instruction"), in either byte order depending on the
  // endianness its producer chose.
  if (Data.size() >= sizeof(uint32_t)) {
    uint32_t Magic;
    memcpy(&Magic, Data.data(), sizeof(Magic));
    if (Magic == 0x07230203u || Magic == 0x03022307u)
      return &SPIRV;
  }

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

/// Resolves `Opts.Target` (see DriverOptions' field comments) to the
/// concrete target triple `TargetMachineBackend` should retarget to:
/// `Opts.Target` may itself either name one of FeMe's own input formats
/// ("dxil"/"spirv", each resolving to that format's own LLVM backend so the
/// module round-trips back out through it) or be a target triple directly
/// (e.g. "amdgcn-amd-amdhsa", for real-ISA retargeting).
llvm::Expected<std::string> resolveTargetTriple(const DriverOptions &Opts,
                                                const llvm::Module &M) {
  llvm::StringRef Requested = Opts.Target;
  if (Requested.empty())
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "--target must name an output format or target triple");

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
  const Importer *Imp = detectFormat(Input);
  if (!Imp)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "could not detect input file format (expected a DXIL bitcode file "
        "or DXContainer, or a SPIR-V binary module)");

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
  if (Imp->getFormatName() == "dxil") {
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

  // Runs after every format-specific raising pass above so the optimizer
  // always sees idiomatic LLVM IR, regardless of which frontend produced
  // it (see feme::OptimizerPipeline's header comment) -- and before
  // codegen, matching where `clang`/`opt -O<N> | llc` run the optimizer
  // relative to instruction selection.
  OptimizerPipeline().run(M, OptimizerOptions{Opts.OptLevel});

  DriverResult Result;
  llvm::raw_svector_ostream OS(Result.Output);
  TargetMachineBackend Backend;
  if (llvm::Error Err = Backend.run(M, BackendOpts, OS))
    return std::move(Err);

  return Result;
}
