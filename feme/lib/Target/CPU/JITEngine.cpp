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
#include "feme/Optimizer/OptimizerPipeline.h"
#include "feme/Target/CPU/RuntimeCPU.h"
#include "feme/Target/CPU/WaveSize.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/ReferenceEntryWrapper.h"
#include "feme/Transforms/CPU/ReferenceLowering.h"
#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/UnsupportedOps.h"
#include "feme/Transforms/CPU/WaveLowering.h"

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
    if (!F || !F->hasFnAttribute("hlsl.shader"))
      return createStringError(inconvertibleErrorCode(),
                               "no compute entry point named '%s'",
                               EntryPoint.str().c_str());
    return F;
  }
  Function *Found = nullptr;
  for (Function &F : M) {
    if (!F.hasFnAttribute("hlsl.shader"))
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

  if (Error E = checkSupportedRaisedOps(Mod))
    return std::move(E);

  {
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

    ModulePassManager MPM;
    MPM.addPass(PreparePass(Opts.EntryPoint));
    MPM.addPass(ResourceLoweringPass());
    if (Opts.Reference) {
      MPM.addPass(ReferenceLoweringPass());
      MPM.addPass(ReferenceEntryWrapperPass());
    } else {
      MPM.addPass(LinearizePass());
      MPM.addPass(SIMDizePass(WaveSize));
      MPM.addPass(WaveLoweringPass());
      MPM.addPass(EntryWrapperPass());
    }
    MPM.run(Mod, MAM);
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
  ResourceInfo ResolvedInfo =
      Info.value_or(ResourceInfo{EntryName, 0, false, {}});

  std::string WrapperName = getEntrySymbolName(EntryName);
  if (!Mod.getFunction(WrapperName)) {
    if (Opts.Reference)
      return createStringError(
          inconvertibleErrorCode(),
          "feme-cpu-wrap-reference-entry did not produce '%s'; the shader "
          "likely uses a wave intrinsic, which has no meaning one "
          "invocation at a time (--reference)",
          WrapperName.c_str());
    return createStringError(
        inconvertibleErrorCode(),
        "feme-cpu-wrap-entry did not produce '%s'; the shader is likely not "
        "acyclic, uniform control flow (see feme::cpu::SIMDizePass, "
        "roadmap milestone 4)",
        WrapperName.c_str());
  }

  // Link in only the referenced `libFeMeRuntimeCPU` helper definitions (see
  // "Runtime Support Library" in feme/docs/FeMeCPUDesign.md).
  Expected<std::unique_ptr<llvm::Module>> RuntimeMod =
      parseBitcodeFile(getRuntimeCPUBitcode(), Mod.getContext());
  if (!RuntimeMod)
    return RuntimeMod.takeError();
  Linker L(Mod);
  if (L.linkInModule(std::move(*RuntimeMod), Linker::Flags::LinkOnlyNeeded))
    return createStringError(inconvertibleErrorCode(),
                             "failed to link libFeMeRuntimeCPU");

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
  auto *Entry = reinterpret_cast<EntryFnTy>(EntryFn);

  FemeDispatchArgs Args{};
  Args.ResourceHeap = Resources.ResourceHeap.data();
  Args.ResourceHeapCount = static_cast<uint32_t>(Resources.ResourceHeap.size());
  Args.SamplerHeap = Resources.SamplerHeap.data();
  Args.SamplerHeapCount = static_cast<uint32_t>(Resources.SamplerHeap.size());
  Args.RootConstants = Resources.RootConstants.data();
  Args.RootConstantSize = static_cast<uint32_t>(Resources.RootConstants.size());
  Args.GroupCount[0] = GroupCount[0];
  Args.GroupCount[1] = GroupCount[1];
  Args.GroupCount[2] = GroupCount[2];
  // Groupshared allocation is milestone 9; every group runs with none.
  Args.GroupShared = nullptr;

  // Deviation (see the header comment): groups run sequentially on the
  // calling thread rather than across a thread pool.
  for (uint32_t Z = 0; Z != GroupCount[2]; ++Z) {
    for (uint32_t Y = 0; Y != GroupCount[1]; ++Y) {
      for (uint32_t X = 0; X != GroupCount[0]; ++X) {
        Args.GroupID[0] = X;
        Args.GroupID[1] = Y;
        Args.GroupID[2] = Z;
        Entry(&Args);
      }
    }
  }
  return Error::success();
}
