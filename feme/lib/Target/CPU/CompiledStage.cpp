//===- CompiledStage.cpp - FeMe CPU target compiled-code object ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/CompiledStage.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"
#include "feme/Optimizer/OptimizerPipeline.h"
#include "feme/Target/CPU/JITEngine.h"
#include "feme/Target/CPU/RuntimeCPU.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/BoundResourceNormalization.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/ReferenceEntryWrapper.h"
#include "feme/Transforms/CPU/ReferenceLowering.h"
#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/UnsupportedOps.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <array>

using namespace llvm;
using namespace feme;
using namespace feme::cpu;

namespace {

Expected<std::unique_ptr<llvm::Module>> cloneIntoContext(llvm::Module &M,
                                                         LLVMContext &NewCtx) {
  SmallVector<char, 0> Buffer;
  raw_svector_ostream OS(Buffer);
  WriteBitcodeToFile(M, OS);
  auto MemBuf =
      std::make_unique<SmallVectorMemoryBuffer>(std::move(Buffer), "clone");
  return parseBitcodeFile(MemBuf->getMemBufferRef(), NewCtx);
}

constexpr unsigned DefaultHostVectorBits = 128;

std::optional<ShaderWaveSizeRequirement>
getShaderWaveSizeRequirement(const llvm::Module &M) {
  for (const Function &F : M)
    if (F.hasFnAttribute("hlsl.wavesize"))
      if (std::optional<ShaderWaveSizeRequirement> Req =
              parseShaderWaveSizeAttr(
                  F.getFnAttribute("hlsl.wavesize").getValueAsString()))
        return Req;
  return std::nullopt;
}

Expected<Function *> selectEntryPoint(llvm::Module &M, StringRef EntryPoint,
                                      ShaderStage Stage) {
  auto isStage = [Stage](const Function &F) {
    return feme::getShaderStage(F) == Stage;
  };

  if (!EntryPoint.empty()) {
    Function *F = M.getFunction(EntryPoint);
    if (!F || !isStage(*F))
      return createStringError(
          inconvertibleErrorCode(), "no %s entry point named '%s'",
          getShaderStageName(Stage).str().c_str(), EntryPoint.str().c_str());
    return F;
  }

  Function *Found = nullptr;
  for (Function &F : M) {
    if (!isStage(F))
      continue;
    if (Found)
      return createStringError(
          inconvertibleErrorCode(),
          "module has more than one %s entry point; select one",
          getShaderStageName(Stage).str().c_str());
    Found = &F;
  }
  if (!Found)
    return createStringError(inconvertibleErrorCode(),
                             "module has no %s entry point",
                             getShaderStageName(Stage).str().c_str());
  return Found;
}

llvm::OptimizationLevel toOptimizationLevel(CodeGenOptLevel Level) {
  switch (Level) {
  case CodeGenOptLevel::None:
    return llvm::OptimizationLevel::O0;
  case CodeGenOptLevel::Less:
    return llvm::OptimizationLevel::O1;
  case CodeGenOptLevel::Default:
    return llvm::OptimizationLevel::O2;
  case CodeGenOptLevel::Aggressive:
    return llvm::OptimizationLevel::O3;
  }
  llvm_unreachable("unknown CodeGenOptLevel");
}

Expected<std::unique_ptr<CompiledStage>>
createStage(Context &Ctx, feme::Module M, ShaderStage Stage,
            StringRef EntryPoint, unsigned RequestedWaveSize,
            CodeGenOptLevel OptLevel, bool Reference) {
  static llvm::once_flag InitFlag;
  llvm::call_once(InitFlag, [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
  });

  if (M.getKind() != feme::Module::Kind::LLVMIR)
    return createStringError(
        inconvertibleErrorCode(),
        "CompiledStage::create expects an already-translated llvm::Module");

  orc::ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());
  Expected<std::unique_ptr<llvm::Module>> Cloned =
      TSCtx.withContextDo([&](LLVMContext *NewCtx) {
        return cloneIntoContext(M.getLLVMModule(), *NewCtx);
      });
  if (!Cloned)
    return Cloned.takeError();

  llvm::Module &Mod = **Cloned;
  Expected<Function *> Entry = selectEntryPoint(Mod, EntryPoint, Stage);
  if (!Entry)
    return Entry.takeError();
  std::string EntryName = (*Entry)->getName().str();

  GroupSharedRequirements GroupSharedReqs = getGroupSharedRequirements(Mod);
  uint32_t SideEffectFlags = computeSideEffectFlags(**Entry);
  // (Roadmap H4g) An entry point with no `!feme.signature` metadata at all
  // (e.g. a genuine SPIR-V entry with no stage-IO varyings of its own,
  // `dEQP-VK.tessellation.winding.*`'s own empty `void main (void) {}`
  // vertex shader, or `splitTessellationControlEntry`'s synthesized
  // trivial control-point phase for an `OutputVertices == 1` no-barrier
  // tessellation-control entry) is treated identically to one carrying an
  // explicitly empty `EntrySignature`, matching
  // `CanonicalizeStagePass::run`'s own "an absent signature is treated as
  // an empty one" rewriting convention -- so `getStageSignature`
  // (GraphicsPipeline.cpp) always finds *some* serialized reflection to
  // parse, rather than erroring out with "compiled stage carries no
  // signature reflection" for a stage that legitimately declares zero
  // elements.
  EntrySignature Sig =
      feme::dxil::getEntrySignature(**Entry).value_or(EntrySignature{});
  std::vector<uint8_t> Signature = serializeSignature(Sig);

  unsigned WaveSize = 1;
  if (!Reference) {
    Expected<unsigned> ResolvedWaveSize = resolveWaveSize(
        RequestedWaveSize ? std::optional<unsigned>(RequestedWaveSize)
                          : std::nullopt,
        getShaderWaveSizeRequirement(Mod), DefaultHostVectorBits);
    if (!ResolvedWaveSize)
      return ResolvedWaveSize.takeError();
    WaveSize = *ResolvedWaveSize;
    (*Entry)->addFnAttr("feme.cpu.wavesize", std::to_string(WaveSize));
  }

  std::string WrapperName;
  if (Reference) {
    PassBuilder PB;
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager Normalize;
    Normalize.addPass(PreparePass(EntryPoint, Stage));
    Normalize.addPass(BoundResourceNormalizationPass());
    Normalize.run(Mod, MAM);

    if (Error E = checkSupportedRaisedOps(Mod))
      return std::move(E);

    ModulePassManager MPM;
    MPM.addPass(ResourceLoweringPass());
    MPM.addPass(ReferenceLoweringPass());
    MPM.addPass(ReferenceEntryWrapperPass());
    MPM.run(Mod, MAM);

    WrapperName = getEntrySymbolName(EntryName);
    if (!Mod.getFunction(WrapperName))
      return createStringError(
          inconvertibleErrorCode(),
          "feme-cpu-wrap-reference-entry did not produce '%s'",
          WrapperName.c_str());

    Expected<std::unique_ptr<llvm::Module>> RuntimeMod =
        parseBitcodeFile(getRuntimeCPUBitcode(), Mod.getContext());
    if (!RuntimeMod)
      return RuntimeMod.takeError();
    feme::cpu::detail::stripAsmLabelManglingEscape(**RuntimeMod);
    feme::cpu::detail::alignRuntimeModuleTriple(**RuntimeMod, Mod);
    Linker L(Mod);
    if (L.linkInModule(std::move(*RuntimeMod), Linker::Flags::LinkOnlyNeeded))
      return createStringError(inconvertibleErrorCode(),
                               "failed to link libFeMeRuntimeCPU");
  } else {
    StageCompileOptions PipeOpts;
    PipeOpts.Stage = Stage;
    PipeOpts.EntryPoint = EntryPoint;
    PipeOpts.WaveSize = WaveSize;
    PipeOpts.OptLevel = OptLevel;
    Expected<PipelineResult> Result = runPipeline(Mod, PipeOpts);
    if (!Result)
      return Result.takeError();
    WrapperName = std::move(Result->WrapperName);
  }

  Function *WaveBody = Mod.getFunction(EntryName);
  if (!WaveBody)
    return createStringError(
        inconvertibleErrorCode(),
        "entry point '%s' did not survive the CPU pipeline", EntryName.c_str());

  std::array<uint32_t, 3> GroupSize = getDeclaredGroupSize(*WaveBody);
  std::optional<ResourceInfo> Info = ResourceInfo::fromModule(Mod, EntryName);
  ResourceInfo ResolvedInfo = Info.value_or([&] {
    ResourceInfo Default;
    Default.EntryName = EntryName;
    return Default;
  }());

  OptimizerPipeline().run(Mod, OptimizerOptions{toOptimizationLevel(OptLevel)});
  if (verifyModule(Mod, &errs()))
    return createStringError(inconvertibleErrorCode(),
                             "JIT module failed verification");

  auto JIT = cantFail(orc::LLJITBuilder().create());
  orc::ThreadSafeModule TSM(std::move(*Cloned), TSCtx);
  if (Error E = JIT->addIRModule(std::move(TSM)))
    return std::move(E);

  Expected<orc::ExecutorAddr> EntryAddr = JIT->lookup(WrapperName);
  if (!EntryAddr)
    return EntryAddr.takeError();

  return std::unique_ptr<CompiledStage>(new CompiledStage(
      std::move(JIT), EntryAddr->toPtr<void *>(), Stage,
      std::move(ResolvedInfo), WaveSize, GroupSize, GroupSharedReqs,
      SideEffectFlags, std::move(Signature)));
}

} // namespace

namespace feme::cpu::detail {

void stripAsmLabelManglingEscape(llvm::Module &M) {
  for (GlobalValue &GV : M.global_values()) {
    StringRef Name = GV.getName();
    if (Name.starts_with('\1'))
      GV.setName(Name.drop_front());
  }
}

void alignRuntimeModuleTriple(llvm::Module &RuntimeMod, const llvm::Module &M) {
  RuntimeMod.setTargetTriple(M.getTargetTriple());
}

} // namespace feme::cpu::detail

CompiledStage::CompiledStage(std::unique_ptr<orc::LLJIT> JIT, void *EntryFn,
                             ShaderStage Stage, ResourceInfo Info,
                             unsigned WaveSize,
                             std::array<uint32_t, 3> GroupSize,
                             GroupSharedRequirements GroupSharedReqs,
                             uint32_t SideEffectFlags,
                             std::vector<uint8_t> Signature)
    : JIT(std::move(JIT)), EntryFn(EntryFn), Stage(Stage),
      Info(std::move(Info)), WaveSize(WaveSize), GroupSize(GroupSize),
      GroupSharedReqs(GroupSharedReqs), SideEffectFlags(SideEffectFlags),
      Signature(std::move(Signature)) {}

CompiledStage::~CompiledStage() = default;
CompiledStage::CompiledStage(CompiledStage &&) noexcept = default;
CompiledStage &CompiledStage::operator=(CompiledStage &&) noexcept = default;

Expected<std::unique_ptr<CompiledStage>>
CompiledStage::create(Context &Ctx, feme::Module M, const JITOptions &Opts) {
  if (Opts.Reference)
    return createStage(Ctx, std::move(M), ShaderStage::Compute, Opts.EntryPoint,
                       /*RequestedWaveSize=*/0, Opts.OptLevel,
                       /*Reference=*/true);

  return createStage(Ctx, std::move(M), ShaderStage::Compute, Opts.EntryPoint,
                     Opts.WaveSize, Opts.OptLevel, /*Reference=*/false);
}

Expected<std::unique_ptr<CompiledStage>>
CompiledStage::create(Context &Ctx, feme::Module M,
                      const StageCompileOptions &Opts) {
  return createStage(Ctx, std::move(M), Opts.Stage, Opts.EntryPoint,
                     Opts.WaveSize, Opts.OptLevel, /*Reference=*/false);
}

Error CompiledStage::invokeGroup(const PreparedDispatch &Prepared,
                                 std::array<uint32_t, 3> GroupID,
                                 MutableArrayRef<uint8_t> GroupShared) const {
  if (Stage != ShaderStage::Compute)
    return createStringError(inconvertibleErrorCode(),
                             "invokeGroup is only legal for compute stages");
  feme::cpu::invokeGroup(reinterpret_cast<EntryPointFn>(EntryFn), Prepared,
                         GroupID, GroupShared);
  return Error::success();
}

Error CompiledStage::invokeVertices(const PreparedVertexBatch &Prepared) const {
  if (Stage != ShaderStage::Vertex)
    return createStringError(inconvertibleErrorCode(),
                             "invokeVertices is only legal for vertex stages");
  FemeVertexArgs Args = Prepared.args();
  reinterpret_cast<VertexEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokeFragments(
    const PreparedFragmentBatch &Prepared) const {
  if (Stage != ShaderStage::Fragment)
    return createStringError(
        inconvertibleErrorCode(),
        "invokeFragments is only legal for fragment stages");
  FemeFragmentArgs Args = Prepared.args();
  reinterpret_cast<FragmentEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokePatch(const PreparedPatchBatch &Prepared) const {
  if (Stage != ShaderStage::Hull)
    return createStringError(inconvertibleErrorCode(),
                             "invokePatch is only legal for hull stages");
  FemePatchArgs Args = Prepared.args();
  reinterpret_cast<PatchEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokePatchConstant(
    const PreparedPatchConstantBatch &Prepared) const {
  if (Stage != ShaderStage::Hull)
    return createStringError(
        inconvertibleErrorCode(),
        "invokePatchConstant is only legal for hull stages");
  FemePatchConstantArgs Args = Prepared.args();
  reinterpret_cast<PatchConstantEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokeDomain(const PreparedDomainBatch &Prepared) const {
  if (Stage != ShaderStage::Domain)
    return createStringError(inconvertibleErrorCode(),
                             "invokeDomain is only legal for domain stages");
  FemeDomainArgs Args = Prepared.args();
  reinterpret_cast<DomainEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokeGeometry(
    const PreparedGeometryBatch &Prepared) const {
  if (Stage != ShaderStage::Geometry)
    return createStringError(
        inconvertibleErrorCode(),
        "invokeGeometry is only legal for geometry stages");
  FemeGeometryArgs Args = Prepared.args();
  reinterpret_cast<GeometryEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokeMesh(const PreparedMeshBatch &Prepared) const {
  if (Stage != ShaderStage::Mesh)
    return createStringError(inconvertibleErrorCode(),
                             "invokeMesh is only legal for mesh stages");
  FemeMeshArgs Args = Prepared.args();
  reinterpret_cast<MeshEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

Error CompiledStage::invokeTask(const PreparedTaskBatch &Prepared) const {
  if (Stage != ShaderStage::Amplification)
    return createStringError(inconvertibleErrorCode(),
                             "invokeTask is only legal for task stages");
  FemeTaskArgs Args = Prepared.args();
  reinterpret_cast<TaskEntryPointFn>(EntryFn)(&Args);
  return Error::success();
}

StageArtifactInfo CompiledStage::getArtifactInfo() const {
  StageArtifactInfo Artifact = StageArtifactInfo::fromResourceInfo(Info);
  Artifact.Stage = Stage;
  Artifact.WaveSize = WaveSize;
  Artifact.GroupSize[0] = GroupSize[0];
  Artifact.GroupSize[1] = GroupSize[1];
  Artifact.GroupSize[2] = GroupSize[2];
  Artifact.GroupSharedSize = static_cast<uint32_t>(GroupSharedReqs.Size);
  Artifact.GroupSharedAlign = static_cast<uint32_t>(GroupSharedReqs.Alignment);
  Artifact.Flags |= SideEffectFlags;
  Artifact.Signature = Signature;
  return Artifact;
}
