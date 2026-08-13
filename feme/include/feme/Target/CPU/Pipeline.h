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
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_PIPELINE_H
#define FEME_TARGET_CPU_PIPELINE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace feme::cpu {

/// The two symbol names `runPipeline` produces or consumes, needed by
/// callers that dispatch the result themselves (`JITEngine`) rather than
/// merely retargeting it to an object file (`Driver`, which only needs to
/// know the pipeline succeeded).
struct PipelineResult {
  /// The selected compute entry point's original name, i.e. \p EntryPoint
  /// if given, else the module's sole `hlsl.shader` function -- still
  /// valid to look up after `runPipeline` returns, unlike the `Function *`
  /// itself (`SIMDizePass` replaces the entry point's `Function` object
  /// entirely, see its own comment).
  std::string EntryName;

  /// The exported ABI symbol name (`feme::cpu::getEntrySymbolName`)
  /// `EntryWrapperPass` produces for `EntryName`.
  std::string WrapperName;
};

/// Runs the full non-`--reference` CPU lowering pipeline on \p M, selecting
/// \p EntryPoint as the compute entry point (empty selects the module's
/// sole one), widening to \p WaveSize, and links in the
/// `libFeMeRuntimeCPU` helper definitions the pipeline's output calls. See
/// the file comment above for scope.
///
/// Callers are expected to have already resolved \p WaveSize (see
/// `feme::cpu::resolveWaveSize`) and rejected any raised operation the CPU
/// target does not support (see `feme::cpu::checkSupportedRaisedOps`) --
/// this only runs the lowering pipeline itself.
llvm::Expected<PipelineResult>
runPipeline(llvm::Module &M, llvm::StringRef EntryPoint, unsigned WaveSize);

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_PIPELINE_H
