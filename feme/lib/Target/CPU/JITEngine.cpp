//===- JITEngine.cpp - FeMe CPU target JIT dispatch engine ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/JITEngine.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Optimizer/OptimizerPipeline.h"
#include "feme/Target/CPU/Pipeline.h"
#include "feme/Target/CPU/RuntimeCPU.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/BoundResourceNormalization.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/ReferenceEntryWrapper.h"
#include "feme/Transforms/CPU/ReferenceLowering.h"
#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/UnsupportedOps.h"

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
using namespace feme::cpu;

namespace {

/// Serializes \p M to bitcode and re-parses it against \p NewCtx, giving an
/// equivalent module owned by a different `LLVMContext`. `JITEngine` needs
/// this because the incoming module lives in `feme::Context`'s
/// `LLVMContext` (a plain, non-thread-safe one), while ORC's
/// `ThreadSafeModule` needs a module owned by an `orc::ThreadSafeContext`
/// -- see the "JIT Flow" section of feme/docs/FeMeCPUDesign.md.
Expected<std::unique_ptr<Module>> cloneIntoContext(Module &M,
                                                   LLVMContext &NewCtx) {
  SmallVector<char, 0> Buffer;
  raw_svector_ostream OS(Buffer);
  WriteBitcodeToFile(M, OS);
  auto MemBuf =
      std::make_unique<SmallVectorMemoryBuffer>(std::move(Buffer), "clone");
  return parseBitcodeFile(MemBuf->getMemBufferRef(), NewCtx);
}

/// The host's default vector width for "Wave Size Selection"'s host-derived
/// default (see feme::Driver's `getHostVectorBits`, whose actual
/// `TargetTransformInfo`-based query is deliberately not duplicated here:
/// `JITEngine` always retargets to the running host, so a conservative
/// 128-bit fallback is exactly as informative here as it is there when no
/// party expresses a wave-size opinion).
constexpr unsigned DefaultHostVectorBits = 128;

std::optional<feme::cpu::ShaderWaveSizeRequirement>
getShaderWaveSizeRequirement(const llvm::Module &M) {
  for (const Function &F : M)
    if (F.hasFnAttribute("hlsl.wavesize"))
      if (std::optional<ShaderWaveSizeRequirement> Req =
              parseShaderWaveSizeAttr(
                  F.getFnAttribute("hlsl.wavesize").getValueAsString()))
        return Req;
  return std::nullopt;
}

std::array<uint32_t, 3> getThreadGroupSize(const Function &F) {
  std::array<uint32_t, 3> Size{1, 1, 1};
  if (!F.hasFnAttribute("hlsl.numthreads"))
    return Size;
  StringRef NumThreads = F.getFnAttribute("hlsl.numthreads").getValueAsString();
  SmallVector<StringRef, 3> Components;
  NumThreads.split(Components, ',');
  if (Components.size() != 3)
    return Size;
  std::array<uint32_t, 3> Result;
  for (unsigned I = 0; I != 3; ++I)
    if (!llvm::to_integer(Components[I], Result[I], 10))
      return Size;
  return Result;
}

/// Finds the sole (or named) `hlsl.shader="compute"` function in \p M,
/// mirroring `feme::cpu::PreparePass`'s own selection rule -- needed here
/// because this runs *before* that pass, to resolve the wave size that gets
/// stamped onto the entry point's attributes before the CPU pipeline runs.
Expected<Function *> selectEntryPoint(llvm::Module &M, StringRef EntryPoint) {
  if (!EntryPoint.empty()) {
    Function *F = M.getFunction(EntryPoint);
    if (!F || !feme::isShaderEntryPoint(*F))
      return createStringError(inconvertibleErrorCode(),
                               "no compute entry point named '%s'",
                               EntryPoint.str().c_str());
    return F;
  }
  Function *Found = nullptr;
  for (Function &F : M) {
    if (!feme::isShaderEntryPoint(F))
      continue;
    if (Found)
      return createStringError(
          inconvertibleErrorCode(),
          "module has more than one compute entry point; select one with "
          "JITOptions::EntryPoint");
    Found = &F;
  }
  if (!Found)
    return createStringError(inconvertibleErrorCode(),
                             "module has no compute entry point");
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

} // namespace

namespace feme::cpu::detail {

/// `FeMeRuntimeCPU.c`'s externally-visible helpers are given their
/// canonical dotted `feme.cpu.resource.*`/`feme.cpu.rt.*` names via a GNU
/// `asm` label (see that file's top comment), since a dotted name is not a
/// valid C identifier. On Mach-O targets, Clang spells an `asm`-labeled
/// symbol's LLVM IR name with a leading `'\1'` (SOH) byte, a convention the
/// AsmPrinter recognizes as "emit this name verbatim, without the
/// platform's usual global-symbol mangling" (i.e. without Mach-O's leading
/// underscore) -- see `Mangler::getNameWithPrefix`. That byte is part of
/// the `GlobalValue`'s actual name, though, so it also defeats the
/// exact-name matching `Linker::linkInModule(..., LinkOnlyNeeded)` uses
/// below: the plain (unescaped) declaration `feme::cpu::ResourceCalls`
/// creates in the shader module never matches the runtime module's
/// `'\1'`-prefixed definition, so the helper never gets linked in, leaving
/// the declaration to fail JIT symbol resolution instead. Strip that
/// leading byte from every global in the freshly-parsed runtime module so
/// its names line up with the plain canonical names regardless of host
/// object format.
void stripAsmLabelManglingEscape(llvm::Module &M) {
  for (GlobalValue &GV : M.global_values()) {
    StringRef Name = GV.getName();
    if (Name.starts_with('\1'))
      GV.setName(Name.drop_front());
  }
}

/// `FeMeRuntimeCPU.c` is compiled to bitcode by an unadorned `clang -c
/// -emit-llvm` invocation (see feme/runtime/CPU/CMakeLists.txt), with no
/// explicit `-target`, so its module carries whichever triple Clang treats
/// as its default for the build host -- which need not be textually
/// identical to the (already normalized) triple `feme::Driver::run`
/// resolved from `--target`/`%feme_host_triple` for the *shader* module,
/// even when both name the very same target (e.g. Clang's Mach-O default
/// spells its OS component "macosx<ver>" where an explicit "--target=...
/// -darwin<ver>" triple spells it "darwin<ver>"). `RuntimeMod` is plain
/// freestanding C with no target-specific codegen of its own, so it is
/// always safe to retarget to the shader module's triple -- doing so before
/// linking avoids `Linker::linkInModule` emitting a spurious "Linking two
/// modules of different target triples" warning for what is, in truth, the
/// same target.
void alignRuntimeModuleTriple(llvm::Module &RuntimeMod, const llvm::Module &M) {
  RuntimeMod.setTargetTriple(M.getTargetTriple());
}

} // namespace feme::cpu::detail

JITEngine::JITEngine(std::unique_ptr<orc::LLJIT> JIT, void *EntryFn,
                     ResourceInfo Info, unsigned WaveSize,
                     std::array<uint32_t, 3> GroupSize)
    : JIT(std::move(JIT)), EntryFn(EntryFn), Info(std::move(Info)),
      WaveSize(WaveSize), GroupSize(GroupSize) {}

JITEngine::~JITEngine() = default;
JITEngine::JITEngine(JITEngine &&) noexcept = default;
JITEngine &JITEngine::operator=(JITEngine &&) noexcept = default;

Expected<std::unique_ptr<JITEngine>>
JITEngine::create(Context &Ctx, feme::Module M, const JITOptions &Opts) {
  static llvm::once_flag InitFlag;
  llvm::call_once(InitFlag, [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
  });

  if (M.getKind() != feme::Module::Kind::LLVMIR)
    return createStringError(inconvertibleErrorCode(),
                             "JITEngine::create expects an already-translated "
                             "llvm::Module (see feme::Driver)");

  // Transplant the module into a fresh `ThreadSafeContext` before doing
  // anything else, so every later step (wave-size resolution, the whole CPU
  // pipeline, linking, optimization) runs directly on the module ORC will
  // eventually own -- see `cloneIntoContext`'s comment.
  orc::ThreadSafeContext TSCtx(std::make_unique<LLVMContext>());
  Expected<std::unique_ptr<llvm::Module>> Cloned =
      TSCtx.withContextDo([&](LLVMContext *NewCtx) {
        return cloneIntoContext(M.getLLVMModule(), *NewCtx);
      });
  if (!Cloned)
    return Cloned.takeError();

  llvm::Module &Mod = **Cloned;

  Expected<Function *> Entry = selectEntryPoint(Mod, Opts.EntryPoint);
  if (!Entry)
    return Entry.takeError();
  std::string EntryName = (*Entry)->getName().str();

  // `--reference` resolves no wave size at all: it never widens anything
  // (see the "CFG restructurization test suite" section of
  // feme/docs/FeMeCPUDesign.md), so there is no `<W x T>` for one to
  // describe.
  unsigned WaveSize = 1;
  if (!Opts.Reference) {
    Expected<unsigned> ResolvedWaveSize = resolveWaveSize(
        Opts.WaveSize ? std::optional<unsigned>(Opts.WaveSize) : std::nullopt,
        getShaderWaveSizeRequirement(Mod), DefaultHostVectorBits);
    if (!ResolvedWaveSize)
      return ResolvedWaveSize.takeError();
    WaveSize = *ResolvedWaveSize;
    (*Entry)->addFnAttr("feme.cpu.wavesize", std::to_string(WaveSize));
  }

  std::string WrapperName;
  if (Opts.Reference) {
    // `--reference` runs its own, simpler pipeline shape (Prepare +
    // BoundResourceNormalization + ResourceLowering, then the reference
    // lowering/wrapper passes instead of Linearize/SIMDize/WaveLowering/
    // EntryWrapper) -- see `feme::cpu::runPipeline`'s file comment for why
    // that pipeline is factored out on its own rather than covering this
    // shape too.
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
    Normalize.addPass(PreparePass(Opts.EntryPoint));
    Normalize.addPass(BoundResourceNormalizationPass());
    Normalize.run(Mod, MAM);

    // See `checkSupportedRaisedOps`'s call site in `feme::cpu::runPipeline`
    // for why this runs after normalization rather than before it.
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
          "feme-cpu-wrap-reference-entry did not produce '%s'; the shader "
          "likely uses a wave intrinsic, which has no meaning one "
          "invocation at a time (--reference)",
          WrapperName.c_str());

    // Link in only the referenced `libFeMeRuntimeCPU` helper definitions
    // (see "Runtime Support Library" in feme/docs/FeMeCPUDesign.md).
    Expected<std::unique_ptr<llvm::Module>> RuntimeMod =
        parseBitcodeFile(getRuntimeCPUBitcode(), Mod.getContext());
    if (!RuntimeMod)
      return RuntimeMod.takeError();
    detail::stripAsmLabelManglingEscape(**RuntimeMod);
    detail::alignRuntimeModuleTriple(**RuntimeMod, Mod);
    Linker L(Mod);
    if (L.linkInModule(std::move(*RuntimeMod), Linker::Flags::LinkOnlyNeeded))
      return createStringError(inconvertibleErrorCode(),
                               "failed to link libFeMeRuntimeCPU");
  } else {
    Expected<PipelineResult> Result =
        runPipeline(Mod, Opts.EntryPoint, WaveSize);
    if (!Result)
      return Result.takeError();
    WrapperName = std::move(Result->WrapperName);
  }

  // `*Entry` was captured before the pipeline above ran; `SIMDizePass`
  // replaces the entry point's `Function` object entirely (see its own
  // comment), so `*Entry` is dangling by now -- look the (still
  // same-named, per `Function::takeName`) function back up by name
  // instead.
  Function *WaveBody = Mod.getFunction(EntryName);
  if (!WaveBody)
    return createStringError(inconvertibleErrorCode(),
                             "entry point '%s' did not survive the CPU "
                             "pipeline",
                             EntryName.c_str());

  std::array<uint32_t, 3> GroupSize = getThreadGroupSize(*WaveBody);
  if (GroupSize[0] * GroupSize[1] * GroupSize[2] == 0)
    return createStringError(inconvertibleErrorCode(),
                             "shader declares an empty thread group");

  std::optional<ResourceInfo> Info = ResourceInfo::fromModule(Mod, EntryName);
  ResourceInfo ResolvedInfo = Info.value_or([&] {
    ResourceInfo Default;
    Default.EntryName = EntryName;
    return Default;
  }());

  OptimizerPipeline().run(Mod,
                          OptimizerOptions{toOptimizationLevel(Opts.OptLevel)});

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

  return std::unique_ptr<JITEngine>(
      new JITEngine(std::move(JIT), EntryAddr->toPtr<void *>(),
                    std::move(ResolvedInfo), WaveSize, GroupSize));
}

Error JITEngine::dispatch(const DispatchResources &Resources,
                          std::array<uint32_t, 3> GroupCount) const {
  using EntryFnTy = void (*)(const FemeDispatchArgs *);
  runDispatch(reinterpret_cast<EntryFnTy>(EntryFn), Info, Resources,
              GroupCount);
  return Error::success();
}
