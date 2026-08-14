//===- Driver.cpp - FeMe full-toolchain orchestration --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Driver/Driver.h"

#include "feme/Core/Module.h"
#include "feme/Import/DXBC/DXBCImporter.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Import/Importer.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "feme/Optimizer/OptimizerPipeline.h"
#include "feme/Target/Backend.h"
#include "feme/Target/CPU/Pipeline.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Target/TargetMachineBackend.h"
#include "feme/Transforms/AMDGPU/RaisedLowering.h"
#include "feme/Transforms/AMDGPU/ResourceLowering.h"
#include "feme/Transforms/DXIL/IntrinsicExpansion.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Transforms/SPIRV/RaisedLowering.h"
#include "feme/Translate/DXSA/DXSATranslator.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"
#include "feme/Translate/Translator.h"

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

#include <cstring>

using namespace feme;

Driver::Driver(Context &Ctx) : Ctx(Ctx) {}

namespace {

/// Sniffs \p Buffer's binary format to select which Importer parses it, so
/// `feme` does not need an explicit `--from` flag naming it: DXIL
/// bitcode/DXContainer, legacy DXBC DXContainer, and SPIR-V binaries each
/// begin with a distinct, well-known magic number (see the "Command Line
/// Tool(s)" section of feme/docs/Design.md). Returns nullptr if \p Buffer's
/// format cannot be determined this way -- this covers input FeMe does not
/// yet import as well as genuinely unrecognized input, since neither can be
/// told apart from the other without actually importing it.
const Importer *detectFormat(llvm::MemoryBufferRef Buffer) {
  static const DXILImporter DXIL;
  static const DXBCImporter DXBC;
  static const SPIRVImporter SPIRV;

  llvm::StringRef Data = Buffer.getBuffer();

  // A `DXContainer` (magic "DXBC", the format predates the DXIL name --
  // see llvm::object::DXContainer::parseHeader) wraps either a DXIL
  // bitcode part (DXILImporter's to parse) or, for a legacy Shader Model
  // 5.0-and-earlier module, a raw tokenized shader bytecode part named
  // `SHEX`/`SHDR` (DXBCImporter's to parse) -- both formats share this
  // same outer magic, so telling them apart needs a peek inside the
  // container. Raw DXIL bitcode, with no container at all, is always
  // DXILImporter's: legacy DXBC is never distributed outside a container.
  if (Data.starts_with("DXBC")) {
    llvm::Expected<llvm::object::DXContainer> Container =
        llvm::object::DXContainer::create(Buffer);
    if (Container) {
      // Guard against llvm::object::DXContainer::PartIterator's
      // constructor unconditionally reading PartOffsets.back() when
      // begin() already equals end(), which only a zero-part container
      // reaches (see feme::DXBCImporter's own getShaderBytecode).
      if (Container->getHeader().PartCount != 0)
        for (const auto &Part : *Container) {
          llvm::StringRef Name = Part.Part.getName();
          if (Name == "SHEX" || Name == "SHDR")
            return &DXBC;
        }
    } else {
      // Consume the error: an unparseable container is still DXILImporter's
      // to reject with its own diagnostic, matching this function's
      // "detect, don't validate" contract.
      llvm::consumeError(Container.takeError());
    }
    return &DXIL;
  }

  if (llvm::isBitcode(
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
/// import already produces one directly, while SPIR-V and DXBC import each
/// produce an MLIR operation (`mlir::spirv::ModuleOp`/`feme::dxsa::ModuleOp`
/// respectively) that still needs its own Translator.
llvm::Expected<Module>
translateToLLVMIR(Module &&Imported, llvm::StringRef FormatName, Context &Ctx) {
  if (Imported.getKind() == Module::Kind::LLVMIR)
    return std::move(Imported);

  if (FormatName == "dxbc") {
    feme::dxsa::DXSAToLLVMIRTranslator ToLLVMIR;
    return ToLLVMIR.translate(std::move(Imported), Ctx);
  }

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

/// Whether \p TheTriple is the FeMe CPU target, i.e. any target triple that
/// is not one of FeMe's other, GPU-shaped retargeting destinations
/// (re-serialized DXIL/SPIR-V, or AMDGPU). This is what "Wave Size
/// Selection" in feme/docs/FeMeCPUDesign.md means by "non-CPU targets":
/// `--wave-size` only has meaning for an actual host retarget.
bool isCPUTarget(const llvm::Triple &TheTriple) {
  return !TheTriple.isDXIL() && !TheTriple.isSPIRV() && !TheTriple.isAMDGCN();
}

/// The shader's declared wave size requirement, if any: the `"hlsl.wavesize"`
/// attribute (see feme::dxil::MetadataRaisingPass) of the first function
/// that carries one. A module with multiple entry points disagreeing about
/// their required wave size is future work (today's front ends only ever
/// raise a single compute entry point per module, see
/// feme/docs/FeMeCPUDesign.md's Roadmap); this simply takes the first
/// declared requirement it finds.
std::optional<feme::cpu::ShaderWaveSizeRequirement>
getShaderWaveSizeRequirement(const llvm::Module &M) {
  for (const llvm::Function &F : M)
    if (F.hasFnAttribute("hlsl.wavesize"))
      if (std::optional<feme::cpu::ShaderWaveSizeRequirement> Req =
              feme::cpu::parseShaderWaveSizeAttr(
                  F.getFnAttribute("hlsl.wavesize").getValueAsString()))
        return Req;
  return std::nullopt;
}

/// The host's vector register width in bits, used only for "Wave Size
/// Selection"'s host-derived default (see feme::cpu::resolveWaveSize). Best
/// effort: a target that isn't registered in this build, or that
/// `TargetTransformInfo` can't usefully answer for (e.g. an empty module),
/// falls back to a conservative 128 bits (SSE/NEON-width) rather than
/// failing outright -- correctness never depends on this value (see "Wave
/// Size Selection"), only which default `W` a shader with no opinion of its
/// own gets built at.
unsigned getHostVectorBits(const llvm::Triple &TheTriple, llvm::Module &M) {
  constexpr unsigned Fallback = 128;
  std::string LookupError;
  const llvm::Target *TheTarget =
      llvm::TargetRegistry::lookupTarget(TheTriple, LookupError);
  if (!TheTarget)
    return Fallback;
  std::unique_ptr<llvm::TargetMachine> TM(TheTarget->createTargetMachine(
      TheTriple, /*CPU=*/"", /*Features=*/"", llvm::TargetOptions(),
      /*RM=*/std::nullopt));
  if (!TM || M.empty())
    return Fallback;
  llvm::TargetTransformInfo TTI = TM->getTargetTransformInfo(*M.begin());
  llvm::TypeSize Bits =
      TTI.getRegisterBitWidth(llvm::TargetTransformInfo::RGK_FixedWidthVector);
  return Bits.isNonZero() ? static_cast<unsigned>(Bits.getFixedValue())
                          : Fallback;
}

} // namespace

llvm::Expected<DriverResult> Driver::run(llvm::MemoryBufferRef Input,
                                         const DriverOptions &Opts) const {
  const Importer *Imp = detectFormat(Input);
  if (!Imp)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "could not detect input file format (expected a DXIL bitcode file "
        "or DXContainer, a legacy DXBC DXContainer, or a SPIR-V binary "
        "module)");

  ImportOptions ImportOpts;
  llvm::Expected<Module> Imported = Imp->import(Input, ImportOpts, Ctx);
  if (!Imported)
    return Imported.takeError();

  llvm::Expected<Module> AsLLVMIR =
      translateToLLVMIR(std::move(*Imported), Imp->getFormatName(), Ctx);
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  llvm::Module &M = AsLLVMIR->getLLVMModule();

  // A DXIL-imported module is still in its already-lowered `dx.op.*` calling
  // convention (see feme::DXILImporter's header comment); a DXBC-derived one
  // is in that same calling convention too, since `feme::dxsa::
  // translateToLLVMIR` deliberately targets it directly (see the DXBC
  // section of feme/docs/Design.md) rather than idiomatic LLVM IR. LLVM's
  // DirectX target's own IR pipeline (in particular
  // `DXILShaderFlagsAnalysis`) requires starting from *idiomatic*,
  // pre-lowering IR -- it asserts if it ever sees a `dx.op.*` declaration, on
  // the assumption that `DXILOpLowering` (part of that same pipeline) is
  // what produces those -- so retargeting to any target, DXIL included,
  // needs `OpRaisingPass` to undo that first. `MetadataRaisingPass` then
  // recovers the module's shader model, entry points, and thread group
  // dimensions from the `dx.*` named metadata they live in into the triple
  // and `hlsl.*` function attributes every later stage reads them from; it
  // runs second because `OpRaisingPass` consumes the `!dx.resources`
  // metadata it drops. See the DXIL section of feme/docs/Design.md.
  if (Imp->getFormatName() == "dxil" || Imp->getFormatName() == "dxbc") {
    llvm::ModuleAnalysisManager MAM;
    feme::dxil::OpRaisingPass().run(M, MAM);
    feme::dxil::MetadataRaisingPass().run(M, MAM);
  }

  llvm::Expected<std::string> TargetTriple = resolveTargetTriple(Opts, M);
  if (!TargetTriple)
    return TargetTriple.takeError();

  llvm::Triple TheTriple(llvm::Triple::normalize(*TargetTriple));

  // "Wave Size Selection" (feme/docs/FeMeCPUDesign.md) only has meaning for
  // the CPU target: resolve and record it there, and diagnose (without
  // failing the build) a `--wave-size` given for any other target, where it
  // is simply ignored. `ResolvedWaveSize` is threaded through to
  // `feme::cpu::runPipeline` below rather than re-resolved there, matching
  // `feme::cpu::JITEngine::create`'s own resolve-once-then-thread-through
  // shape.
  unsigned ResolvedWaveSize = 0;
  if (isCPUTarget(TheTriple)) {
    llvm::Expected<unsigned> WaveSize = feme::cpu::resolveWaveSize(
        Opts.WaveSize, getShaderWaveSizeRequirement(M),
        getHostVectorBits(TheTriple, M));
    if (!WaveSize)
      return WaveSize.takeError();
    ResolvedWaveSize = *WaveSize;
    // Recorded as a function attribute on every entry point too, so a
    // module dumped between this point and `feme::cpu::runPipeline`
    // still shows it.
    for (llvm::Function &F : M)
      if (F.hasFnAttribute("hlsl.shader"))
        F.addFnAttr("feme.cpu.wavesize", std::to_string(ResolvedWaveSize));
  } else if (Opts.WaveSize) {
    llvm::errs() << "feme: warning: --wave-size is ignored for target '"
                 << *TargetTriple << "' (not the FeMe CPU target)\n";
  }

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

  // A raised module isn't valid input to a real CPU `TargetMachine` either:
  // like AMDGPU, it still uses format-agnostic `llvm.dx.*`/`llvm.spv.*`
  // resource/builtin intrinsics (and, unlike AMDGPU, a `target("dx.")`
  // resource handle type LLVM's generic codegen has no notion of at all --
  // see `llvm::MVT::getVT`) no in-tree CPU backend understands. Unlike
  // AMDGPU's single translation pass, the CPU target needs its own full
  // SPMD-to-scalar/vector lowering pipeline (`feme::cpu::runPipeline`, see
  // feme/docs/FeMeCPUDesign.md); it is the same pipeline
  // `feme::cpu::JITEngine::create` runs before JIT-dispatching a shader,
  // reused here since retargeting to an object file needs exactly the same
  // lowering, just handed to a `TargetMachine` afterwards instead of ORC.
  if (isCPUTarget(TheTriple)) {
    // `TargetMachineBackend::run` normally sets these right before codegen,
    // but `feme::cpu::runPipeline` links in `libFeMeRuntimeCPU` (see its
    // own comment) before that point is reached; linking two modules that
    // each carry a *different, non-empty* target triple/data layout (the
    // still-DXIL-flavored one this module inherited from import, vs. the
    // runtime bitcode's host one) is otherwise merely a diagnosed warning,
    // not an error, but not one any of this pipeline's own output should
    // ever provoke.
    std::string LookupError;
    if (const llvm::Target *TheTarget =
            llvm::TargetRegistry::lookupTarget(TheTriple, LookupError)) {
      if (std::unique_ptr<llvm::TargetMachine> TM(
              TheTarget->createTargetMachine(
                  TheTriple, /*CPU=*/"", /*Features=*/"", llvm::TargetOptions(),
                  /*RM=*/std::nullopt));
          TM) {
        M.setTargetTriple(TheTriple);
        M.setDataLayout(TM->createDataLayout());
      }
    }

    llvm::Expected<feme::cpu::PipelineResult> Result =
        feme::cpu::runPipeline(M, /*EntryPoint=*/"", ResolvedWaveSize);
    if (!Result)
      return Result.takeError();
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
