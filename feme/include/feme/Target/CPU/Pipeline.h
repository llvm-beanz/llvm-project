//===- Pipeline.h - FeMe CPU target lowering pipeline ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::runPipeline, the non-`--reference` CPU
// pipeline (feme/docs/FeMeCPUDesign.md's Phases 1-6:
// feme::cpu::PreparePass, ResourceLoweringPass, LinearizePass, SIMDizePass,
// WaveLoweringPass, EntryWrapperPass, plus linking in the
// `libFeMeRuntimeCPU` helper definitions the pipeline's output calls) as a
// single entry point shared by every caller that needs a raised module
// turned into a dispatchable `feme_cpu_entry_<name>` function: today, both
// `feme::cpu::JITEngine::create` (feme/lib/Target/CPU/JITEngine.cpp) and
// `feme::Driver::run`'s CPU-target retargeting path
// (feme/lib/Driver/Driver.cpp).
//
// Roadmap R27 generalizes this into the stage-aware entry point
// feme/docs/FeMeGraphicsDesign.md's "CPU Lowering Pipeline" section asks
// for: `feme::cpu::StageCompileOptions` names the stage to select
// (`feme::ShaderStage`) alongside the entry point and wave size, and the
// `StageCompileOptions`-taking `runPipeline` overload is what a graphics
// caller should use. The original `runPipeline(llvm::Module &,
// llvm::StringRef, unsigned)` signature remains, unchanged in behavior, as
// the compute-only compatibility overload that section explicitly asks to
// keep -- every existing caller (`JITEngine`/`CompiledStage`, `Driver`)
// still selects `feme::ShaderStage::Compute` through it.
//
// For a non-`Compute` stage, `runPipeline` also runs
// `feme::graphics::ValidateStagePass` -- the "pre-mutation graphics
// validation" step that same section calls for -- against the incoming
// module before `feme::cpu::PreparePass` (or anything else) mutates it, so
// a `feme.stage.*` operand/signature/stage-legality violation is diagnosed
// against the shader exactly as authored rather than after structurization
// has already rewritten its control flow.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_PIPELINE_H
#define FEME_TARGET_CPU_PIPELINE_H

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace feme::cpu {

/// Options controlling `runPipeline`'s stage-aware overload: which stage's
/// entry point to select and compile, alongside the existing entry-point
/// name and wave size knobs. See the file comment above.
struct StageCompileOptions {
  /// The pipeline stage the selected entry point must declare (see
  /// `feme::ShaderStage`).
  ShaderStage Stage = ShaderStage::Compute;

  /// The entry point to select, or empty to require the module to have
  /// exactly one entry point of `Stage` (see `feme::cpu::PreparePass`).
  llvm::StringRef EntryPoint;

  /// The wave size to widen to (see `feme::cpu::resolveWaveSize`); callers
  /// are expected to have already resolved this, exactly as the
  /// compute-only overload requires.
  unsigned WaveSize = 0;

  /// Optimization level used by `feme::cpu::CompiledStage::create`. The
  /// pipeline-only `runPipeline` entry point ignores it.
  llvm::CodeGenOptLevel OptLevel = llvm::CodeGenOptLevel::Default;

  /// Accepted for forward compatibility with the full design. No CPU-target
  /// pipeline phase consults it yet.
  bool EnableRobustness = true;
};

/// The two symbol names `runPipeline` produces or consumes, needed by
/// callers that dispatch the result themselves (`JITEngine`) rather than
/// merely retargeting it to an object file (`Driver`, which only needs to
/// know the pipeline succeeded).
struct PipelineResult {
  /// The selected entry point's original name, i.e. \p EntryPoint if given,
  /// else the module's sole entry point of the requested stage -- still
  /// valid to look up after `runPipeline` returns, unlike the `Function *`
  /// itself (`SIMDizePass` replaces the entry point's `Function` object
  /// entirely, see its own comment).
  std::string EntryName;

  /// The exported ABI symbol name (`feme::cpu::getEntrySymbolName`)
  /// `EntryWrapperPass` produces for `EntryName`.
  std::string WrapperName;

  /// The stage `EntryName` was selected and compiled as. Always
  /// `feme::ShaderStage::Compute` for the compute-only overload.
  ShaderStage Stage = ShaderStage::Compute;
};

/// Runs the full non-`--reference` CPU lowering pipeline on \p M per
/// \p Opts: selecting \p Opts.EntryPoint (or the module's sole entry point
/// of \p Opts.Stage) as the entry point, widening to \p Opts.WaveSize, and
/// linking in the `libFeMeRuntimeCPU` helper definitions the pipeline's
/// output calls. See the file comment above for scope, including the
/// pre-mutation graphics validation a non-`Compute` \p Opts.Stage runs.
///
/// Callers are expected to have already resolved \p Opts.WaveSize (see
/// `feme::cpu::resolveWaveSize`) and rejected any raised operation the CPU
/// target does not support (see `feme::cpu::checkSupportedRaisedOps`) --
/// this only runs the lowering pipeline itself.
llvm::Expected<PipelineResult> runPipeline(llvm::Module &M,
                                           const StageCompileOptions &Opts);

/// Compute-only compatibility overload, equivalent to calling the
/// `StageCompileOptions` overload with `Stage == ShaderStage::Compute`,
/// \p EntryPoint and \p WaveSize otherwise unchanged. See the file comment
/// above.
llvm::Expected<PipelineResult>
runPipeline(llvm::Module &M, llvm::StringRef EntryPoint, unsigned WaveSize);

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_PIPELINE_H
