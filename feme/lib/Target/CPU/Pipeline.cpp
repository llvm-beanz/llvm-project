//===- Pipeline.cpp - FeMe CPU target lowering pipeline ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/Pipeline.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Target/CPU/RuntimeCPU.h"
#include "feme/Transforms/CPU/BoundResourceNormalization.h"
#include "feme/Transforms/CPU/DomainWrapper.h"
#include "feme/Transforms/CPU/EntryWrapper.h"
#include "feme/Transforms/CPU/FragmentWrapper.h"
#include "feme/Transforms/CPU/GeometryWrapper.h"
#include "feme/Transforms/CPU/HullWrapper.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "feme/Transforms/CPU/MeshOutputWrapper.h"
#include "feme/Transforms/CPU/PatchConstantWrapper.h"
#include "feme/Transforms/CPU/Prepare.h"
#include "feme/Transforms/CPU/ResourceLowering.h"
#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/SPIRVBuiltinFolding.h"
#include "feme/Transforms/CPU/SPIRVPushConstantLowering.h"
#include "feme/Transforms/CPU/SPIRVResourceLowering.h"
#include "feme/Transforms/CPU/SPIRVSubpassLowering.h"
#include "feme/Transforms/CPU/TaskPayloadWrapper.h"
#include "feme/Transforms/CPU/UnsupportedOps.h"
#include "feme/Transforms/CPU/VertexWrapper.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/Graphics/CanonicalizeStage.h"
#include "feme/Transforms/Graphics/ValidateStage.h"

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

/// RAII installation of a diagnostic handler that records whether any
/// `DS_Error`-severity diagnostic fired while it is alive, restoring \p M's
/// previous handler callback on destruction. Several CPU-pipeline passes
/// (`feme::cpu::PreparePass`, `feme::cpu::LinearizePass`,
/// `feme::cpu::SIMDizePass`, ...) report an unsupported shape by calling
/// `LLVMContext::emitError` rather than through an `Error`-returning `run`
/// (a `ModulePassManager` has none to propagate one through -- see
/// `feme::cpu::checkSupportedRaisedOps`'s own comment for the same
/// constraint); left unchecked, `ModulePassManager::run`'s `void` result
/// means such a diagnostic prints to stderr but otherwise lets the pipeline
/// carry on to link and JIT-dispatch whatever the diagnosing pass left
/// behind (see the "divergent branch inside a loop" P0 item in
/// feme/docs/Roadmap.md's §1.6: `feme::cpu::LinearizePass` diagnoses and
/// leaves such a shape completely untouched, and without this guard the
/// unwidened, still-divergent function it left behind would reach the JIT
/// and run forever rather than fail). Mirrors the same pattern
/// `tools/feme-opt/feme-opt.cpp`'s `runLLVMIRMode` already uses to turn a
/// diagnostic into its own "print and fail" exit path.
class ErrorDiagnosticGuard {
public:
  explicit ErrorDiagnosticGuard(LLVMContext &Ctx)
      : Ctx(Ctx), PrevCallback(Ctx.getDiagnosticHandlerCallBack()),
        PrevContext(Ctx.getDiagnosticContext()) {
    Ctx.setDiagnosticHandlerCallBack(&handle, this);
  }

  ~ErrorDiagnosticGuard() {
    Ctx.setDiagnosticHandlerCallBack(PrevCallback, PrevContext);
  }

  bool sawError() const { return SawError; }

private:
  static void handle(const DiagnosticInfo *DI, void *Context) {
    auto *Self = static_cast<ErrorDiagnosticGuard *>(Context);
    if (DI->getSeverity() == DS_Error)
      Self->SawError = true;
    errs() << LLVMContext::getDiagnosticMessagePrefix(DI->getSeverity())
           << ": ";
    DiagnosticPrinterRawOStream Printer(errs());
    DI->print(Printer);
    errs() << "\n";
  }

  LLVMContext &Ctx;
  DiagnosticHandler::DiagnosticHandlerTy PrevCallback;
  void *PrevContext;
  bool SawError = false;
};

/// Finds the sole (or named) \p Stage entry point in \p M -- see
/// `feme::cpu::PreparePass`'s own (equivalent, but `Error`-reporting-via-
/// diagnostic-handler rather than `Expected`-returning) selection rule,
/// which this mirrors so `runPipeline` can name-check its own precondition
/// before handing \p EntryPoint to that pass.
Expected<Function *> selectEntryPoint(Module &M, StringRef EntryPoint,
                                      feme::ShaderStage Stage) {
  StringRef StageName = feme::getShaderStageName(Stage);
  auto declaresStage = [Stage](const Function &F) {
    return feme::getShaderStage(F) == Stage;
  };

  if (!EntryPoint.empty()) {
    Function *F = M.getFunction(EntryPoint);
    if (!F || !declaresStage(*F))
      return createStringError(
          inconvertibleErrorCode(), "no %s entry point named '%s'",
          StageName.str().c_str(), EntryPoint.str().c_str());
    return F;
  }
  Function *Found = nullptr;
  for (Function &F : M) {
    if (!declaresStage(F))
      continue;
    if (Found)
      return createStringError(
          inconvertibleErrorCode(),
          "module has more than one %s entry point; select one by "
          "name",
          StageName.str().c_str());
    Found = &F;
  }
  if (!Found)
    return createStringError(inconvertibleErrorCode(),
                             "module has no %s entry point",
                             StageName.str().c_str());
  return Found;
}

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
/// below: the plain (unescaped) declaration a raised shader module creates
/// never matches the runtime module's `'\1'`-prefixed definition, so the
/// helper never gets linked in, leaving the declaration to fail symbol
/// resolution instead. Strip that leading byte from every global in the
/// freshly-parsed runtime module so its names line up with the plain
/// canonical names regardless of host object format.
void stripAsmLabelManglingEscape(Module &M) {
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
/// resolved from `--target`/`%feme_host_triple` for \p M, even when both
/// name the very same target (e.g. Clang's Mach-O default spells its OS
/// component "macosx<ver>" where an explicit "--target=...-darwin<ver>"
/// triple spells it "darwin<ver>"). `RuntimeMod` is plain freestanding C
/// with no target-specific codegen of its own, so it is always safe to
/// retarget to \p M's triple -- doing so before linking avoids
/// `Linker::linkInModule` emitting a spurious "Linking two modules of
/// different target triples" warning for what is, in truth, the same
/// target.
void alignRuntimeModuleTriple(Module &RuntimeMod, const Module &M) {
  RuntimeMod.setTargetTriple(M.getTargetTriple());
}

} // namespace

namespace feme::cpu {

Expected<PipelineResult> runPipeline(Module &M,
                                     const StageCompileOptions &Opts) {
  Expected<Function *> Entry = selectEntryPoint(M, Opts.EntryPoint, Opts.Stage);
  if (!Entry)
    return Entry.takeError();
  std::string EntryName = (*Entry)->getName().str();

  // "CPU Lowering Pipeline" in feme/docs/FeMeGraphicsDesign.md draws this
  // as one "Graphics canonicalization and validation" box ahead of
  // `feme::cpu::PreparePass`, but until now only the validation half was
  // ever wired into this function -- `feme::graphics::CanonicalizeStagePass`
  // (which rewrites a DXIL `loadInput`/`storeOutput` call or a raw SPIR-V
  // `Input`/`Output`-storage-class global load/store into the
  // `feme.stage.*` calls `ValidateStagePass` (immediately below) and
  // `LinearizePass`/`SIMDizePass` (further down this pipeline) all expect)
  // was never run at all outside the separate Vulkan graphics pipeline
  // (`feme::vulkan::compileGraphicsPipeline`/GraphicsPipeline.cpp). A
  // shader imported straight off disk -- rather than routed through that
  // Vulkan-specific path first -- therefore reached `SIMDizePass` with its
  // stage IO still a plain, un-canonicalized memory access, hitting that
  // pass's vector-decomposition diagnostic (or an unmasked-side-effect gap
  // for a scalar output) not because widening itself was incomplete, but
  // because the value being widened was never routed into the mechanism
  // that already knows how to widen it (roadmap C8's stage-IO-raising
  // finding). Running it here, before `ValidateStagePass`, closes that gap
  // for both DXIL and SPIR-V import uniformly and matches
  // `ValidateStagePass`'s own header comment ("however they got there,
  // whether from `CanonicalizeStagePass` or written by hand"), which
  // already assumed this ordering. Every existing (compute) caller selects
  // `ShaderStage::Compute`, which has no `feme.stage.*` operations (nor any
  // stage-IO global in address space 7/8) to canonicalize or validate, so
  // this remains a no-op for them.
  if (Opts.Stage != feme::ShaderStage::Compute) {
    PassBuilder ValidatePB;
    ModuleAnalysisManager ValidateMAM;
    ValidatePB.registerModuleAnalyses(ValidateMAM);
    ErrorDiagnosticGuard ValidateGuard(M.getContext());
    ModulePassManager ValidateMPM;
    ValidateMPM.addPass(feme::graphics::CanonicalizeStagePass());
    ValidateMPM.addPass(feme::graphics::ValidateStagePass());
    ValidateMPM.run(M, ValidateMAM);
    if (ValidateGuard.sawError())
      return createStringError(
          inconvertibleErrorCode(),
          "feme-cpu pipeline: graphics validation failed for '%s' (see "
          "stderr)",
          EntryName.c_str());
  }

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

    // See `ErrorDiagnosticGuard`'s comment: every pass below reports an
    // unsupported shape through `LLVMContext::emitError` rather than an
    // `Error` a `ModulePassManager::run` could propagate, so this guard is
    // what turns "printed a diagnostic" into "this function fails" instead
    // of silently continuing to link and JIT-dispatch whatever the
    // diagnosing pass left behind.
    ErrorDiagnosticGuard DiagGuard(M.getContext());

    // `feme::cpu::BoundResourceNormalizationPass` must run before
    // `checkSupportedRaisedOps` -- a finite-range bound handle it can
    // normalize is not a raised operation the CPU target rejects, unlike an
    // unbounded or conflicting one, which it leaves for that check to
    // reject exactly as before that pass existed (see "Bound-resource
    // normalization" in feme/docs/FeMeCPUDesign.md). This splits the
    // pipeline into two `ModulePassManager` runs around that check instead
    // of running it from within a callback pass.
    ModulePassManager Normalize;
    // A SPIR-V-sourced module's builtin (thread/group ID) access always
    // materializes the whole 3-component vector before extracting the one
    // lane actually used (see `feme::cpu::SPIRVBuiltinFoldingPass`'s header
    // comment); folding that here, before any other pass, keeps the rest of
    // the pipeline seeing the same directly-scalar shape a DXIL-sourced
    // module's `llvm.dx.thread.id` already is. A no-op for a DXIL-sourced
    // module, which has no such construct to fold.
    Normalize.addPass(SPIRVBuiltinFoldingPass());
    Normalize.addPass(PreparePass(Opts.EntryPoint, Opts.Stage));
    Normalize.addPass(BoundResourceNormalizationPass());
    // Lowers the one register-bound constant buffer "Root constants" in
    // feme/docs/FeMeCPUDesign.md carves out an exception for, before
    // `checkSupportedRaisedOps` below would otherwise reject its
    // `handlefrombinding` call the same as any other register-bound
    // handle `feme::cpu::BoundResourceNormalizationPass` didn't (or
    // couldn't) normalize into a heap access.
    Normalize.addPass(RootConstantLoweringPass());
    // SPIR-V has no bindless-heap counterpart to normalize into (see
    // `feme::cpu::SPIRVResourceLoweringPass`'s header comment), so it lowers
    // a bound `spirv.VulkanBuffer` handle directly into the same canonical
    // `feme.cpu.resource.*` calls this pipeline's later stages already
    // expect, rather than feeding `feme::cpu::ResourceLoweringPass` a
    // `handlefromheap` call the way the DXIL pass above does.
    Normalize.addPass(SPIRVResourceLoweringPass());
    // V3: lowers a SPIR-V push-constant block access for a function with no
    // bound-resource access of its own (the combined case is
    // `feme::cpu::SPIRVResourceLoweringPass`'s own responsibility, run just
    // above -- see that pass's header comment).
    Normalize.addPass(SPIRVPushConstantLoweringPass());
    // Roadmap F8a: gives a fragment shader using `feme.stage.subpass.load`
    // (a SPIR-V subpassInput local read, created directly by
    // `feme::spirv::SubpassLoadPattern` -- not a bound resource, so
    // `SPIRVResourceLoweringPass` above never sees it) its own
    // `subpass_input_heap`/`subpass_input_heap_count` ABI parameters.
    Normalize.addPass(SPIRVSubpassLoweringPass());
    Normalize.run(M, MAM);
    if (DiagGuard.sawError())
      return createStringError(inconvertibleErrorCode(),
                               "feme-cpu pipeline: a diagnostic was reported "
                               "while preparing '%s' (see stderr)",
                               EntryName.c_str());

    if (Error E = checkSupportedRaisedOps(M))
      return std::move(E);

    // Each pass runs in its own `ModulePassManager`, checked immediately
    // after: `feme::cpu::LinearizePass` (and, per §1.6's new gap this
    // milestone closes, `feme::cpu::SIMDizePass`) can diagnose a shape and
    // deliberately leave it completely untouched rather than fail outright
    // (a `ModulePassManager::run` has no `Error` to propagate one through),
    // and a later pass in this same pipeline (`feme::cpu::EntryWrapperPass`
    // in particular) assumes every earlier one actually produced its
    // documented postcondition -- running it anyway on a function a prior
    // pass only diagnosed, rather than transformed, hits an
    // `llvm_unreachable` instead of a clean failure. Bailing between each
    // pass, not just once after the whole pipeline, is what makes this
    // guard actually prevent that rather than merely detect it too late.
    auto runAndCheck = [&](StringRef Stage, auto &&Pass) -> Error {
      ModulePassManager StageMPM;
      StageMPM.addPass(std::forward<decltype(Pass)>(Pass));
      StageMPM.run(M, MAM);
      if (DiagGuard.sawError())
        return createStringError(
            inconvertibleErrorCode(),
            "feme-cpu pipeline: a diagnostic was reported while %s '%s' "
            "(see stderr)",
            Stage.str().c_str(), EntryName.c_str());
      return Error::success();
    };
    if (Error E = runAndCheck("lowering resources for", ResourceLoweringPass()))
      return std::move(E);
    if (Error E = runAndCheck("linearizing", LinearizePass()))
      return std::move(E);
    if (Error E = runAndCheck("widening", SIMDizePass(Opts.WaveSize)))
      return std::move(E);
    if (Error E = runAndCheck("lowering waves for", WaveLoweringPass()))
      return std::move(E);
    switch (Opts.Stage) {
    case feme::ShaderStage::Compute:
      if (Error E = runAndCheck("wrapping", EntryWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Vertex:
      if (Error E = runAndCheck("wrapping", VertexWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Fragment:
      if (Error E = runAndCheck("wrapping", FragmentWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Hull:
      if (Error E = runAndCheck("wrapping", HullWrapperPass()))
        return std::move(E);
      if (Error E = runAndCheck("wrapping", PatchConstantWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Domain:
      if (Error E = runAndCheck("wrapping", DomainWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Geometry:
      if (Error E = runAndCheck("wrapping", GeometryWrapperPass()))
        return std::move(E);
      break;
    // Roadmap H6c: a mesh or task (amplification) entry point dispatches
    // as a bounded workgroup exactly like compute (`VK_EXT_mesh_shader`'s
    // own `LocalSize`/`gl_WorkGroupID` model, not geometry's per-primitive
    // batch), so `EntryWrapperPass` -- the compute wrapper's group loop,
    // groupshared allocation and barrier-region splitting -- builds
    // `feme_cpu_entry_<name>` for either exactly the way it already does
    // for compute, unmodified: `FemeMeshArgs`/`FemeTaskArgs` (RuntimeABI.h)
    // share `FemeDispatchArgs`'s own field layout for every field this pass
    // reads (`Resources`, `GroupID`, `GroupShared`), so no stage-specific
    // wrapper is needed for that part of the job at all.
    //
    // Roadmap H6c-a-a: a mesh entry's own per-vertex/per-primitive
    // `feme.stage.output.store` (canonicalized with a dynamic `Vertex`
    // operand, roadmap H6b) is not one of those shared fields, so
    // `MeshOutputWrapperPass` runs first, appending `FemeMeshArgs`'s
    // output-array fields to the wave body by name and lowering every
    // masked output store into an address into `VertexOutputs`/
    // `PrimitiveOutputs` -- see MeshOutputWrapper.h. `EntryWrapperPass`
    // then builds the group-loop wrapper around the result exactly as
    // before, just against `getMeshArgsType`'s longer struct instead of
    // `getDispatchArgsType`'s (see `feme::cpu::buildWrapperEnv`'s own
    // `IsMesh` handling, EntryWrapper.cpp).
    //
    // What was *not* wired here until roadmap H6s: `EmitMeshTasksEXT`.
    // `SetMeshOutputsEXT` itself converts directly into a
    // `feme.stage.set_mesh_outputs` call at the MLIR SPIR-V-to-LLVM
    // conversion level (`SetMeshOutputsEXTConversionPattern`,
    // SPIRVToLLVMPatterns.cpp), and `MeshOutputWrapperPass` lowers it
    // alongside the output stores above, writing `FemeMeshArgs::
    // ActualVertexCount`/`ActualPrimitiveCount` (roadmap H6c-a-a-i).
    // `EmitMeshTasksEXT` now likewise converts directly into a
    // `feme.stage.emit_mesh_tasks` call (`EmitMeshTasksEXTConversionPattern`,
    // SPIRVToLLVMPatterns.cpp) -- see `TaskPayloadWrapperPass`'s own
    // handling of it below, since it belongs to the task (amplification)
    // stage, not the mesh stage.
    //
    // Roadmap H6c-a-b: an amplification (task) entry's own bounded
    // payload write (`feme.stage.task.payload.store`, canonicalized with a
    // constant byte offset, roadmap H6i) is likewise not one of
    // `FemeDispatchArgs`'s shared fields, so `TaskPayloadWrapperPass` runs
    // first here too, appending `FemeTaskArgs::Payload`/`MaxPayloadBytes`
    // to the wave body by name and lowering every masked payload store
    // into a store at `task_payload + offset` -- see
    // TaskPayloadWrapper.h. Roadmap H6s extends this same pass to also
    // lower a canonicalized `feme.stage.emit_mesh_tasks` call, writing the
    // requested mesh dispatch's 3D group count through
    // `FemeTaskArgs::MeshGroupCount` (`task_mesh_group_count`).
    // `EntryWrapperPass` then builds the group-loop
    // wrapper around the result exactly as before, just against
    // `getTaskArgsType`'s longer struct instead of `getDispatchArgsType`'s
    // (see `feme::cpu::buildWrapperEnv`'s own `IsTask` handling,
    // EntryWrapper.cpp, mirroring `IsMesh`).
    case feme::ShaderStage::Amplification:
      if (Error E = runAndCheck("wrapping", TaskPayloadWrapperPass()))
        return std::move(E);
      if (Error E = runAndCheck("wrapping", EntryWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Mesh:
      if (Error E = runAndCheck("wrapping", MeshOutputWrapperPass()))
        return std::move(E);
      if (Error E = runAndCheck("wrapping", EntryWrapperPass()))
        return std::move(E);
      break;
    case feme::ShaderStage::Library:
    case feme::ShaderStage::RayGeneration:
    case feme::ShaderStage::AnyHit:
    case feme::ShaderStage::ClosestHit:
    case feme::ShaderStage::Miss:
    case feme::ShaderStage::Intersection:
    case feme::ShaderStage::Callable:
    case feme::ShaderStage::NumStages:
      llvm_unreachable(
          "unsupported stage reached runPipeline wrapper selection");
    }
  }

  std::string WrapperName = getEntrySymbolName(EntryName);
  if (!M.getFunction(WrapperName))
    return createStringError(
        inconvertibleErrorCode(),
        "feme-cpu-wrap-entry did not produce '%s'; the shader is likely not "
        "acyclic, uniform control flow (see feme::cpu::SIMDizePass, "
        "roadmap milestone 4)",
        WrapperName.c_str());

  // Link in only the referenced `libFeMeRuntimeCPU` helper definitions (see
  // "Runtime Support Library" in feme/docs/FeMeCPUDesign.md).
  Expected<std::unique_ptr<Module>> RuntimeMod =
      parseBitcodeFile(getRuntimeCPUBitcode(), M.getContext());
  if (!RuntimeMod)
    return RuntimeMod.takeError();
  stripAsmLabelManglingEscape(**RuntimeMod);
  alignRuntimeModuleTriple(**RuntimeMod, M);
  Linker L(M);
  if (L.linkInModule(std::move(*RuntimeMod), Linker::Flags::LinkOnlyNeeded))
    return createStringError(inconvertibleErrorCode(),
                             "failed to link libFeMeRuntimeCPU");

  return PipelineResult{std::move(EntryName), std::move(WrapperName),
                        Opts.Stage};
}

Expected<PipelineResult> runPipeline(Module &M, StringRef EntryPoint,
                                     unsigned WaveSize) {
  StageCompileOptions Opts;
  Opts.Stage = feme::ShaderStage::Compute;
  Opts.EntryPoint = EntryPoint;
  Opts.WaveSize = WaveSize;
  return runPipeline(M, Opts);
}

} // namespace feme::cpu
