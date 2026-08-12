//===- OptimizerPipeline.h - FeMe IR optimization pipeline ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::OptimizerPipeline, the pipeline stage that runs
// LLVM's standard per-module optimization pipeline over a Module at a
// requested feme::frontend::DriverOptions::OptLevel (see
// feme/include/feme/Frontend/FrontendOptions.h and the `-O0`/`-O1`/`-O2`/
// `-O3`/`-Od` options in feme/include/feme/Frontend/Options.td). Like
// feme::Backend (feme/include/feme/Target/Backend.h), this is deliberately
// not format-specific: once a program is `llvm::Module`, optimizing it
// reuses standard `llvm::PassBuilder` infrastructure regardless of which
// frontend produced it. See the "Retargeting to Native ISA" section of
// feme/docs/Design.md, which this pipeline stage runs ahead of.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_OPTIMIZER_OPTIMIZERPIPELINE_H
#define FEME_OPTIMIZER_OPTIMIZERPIPELINE_H

#include "llvm/Passes/OptimizationLevel.h"

namespace llvm {
class Module;
class TargetMachine;
} // namespace llvm

namespace feme {

/// Options controlling how OptimizerPipeline optimizes a Module. A plain
/// struct, mirroring BackendOptions' rationale (see feme/include/feme/
/// Target/Backend.h): FeMe does not use RTTI (see feme/.instructions.md),
/// so a single struct that grows over time is preferred over a polymorphic
/// options hierarchy.
struct OptimizerOptions {
  /// The optimization level to run, mirroring `clang`/`opt`'s `-O0`..`-O3`
  /// command line flags (and `feme`'s own `-Od` alias for `-O0` -- see
  /// Options.td). Defaults to `O0` ("disable as many optimizations as
  /// possible"), matching `feme`'s default when no `-O` flag is given.
  llvm::OptimizationLevel Level = llvm::OptimizationLevel::O0;
};

/// Runs LLVM's standard per-module optimization pipeline
/// (`llvm::PassBuilder::buildPerModuleDefaultPipeline`) over a Module. This
/// class does not itself select which passes run at which level -- that
/// selection is entirely LLVM's own, reused verbatim rather than
/// reimplemented, matching how feme::TargetMachineBackend reuses
/// `llvm::TargetMachine`'s own codegen pipeline rather than reimplementing
/// target-specific lowering.
class OptimizerPipeline {
public:
  /// Optimizes \p M in place at \p Opts.Level. \p TM, if non-null, is used
  /// to register target-specific analyses (e.g. `TargetIRAnalysis`) so
  /// target-aware optimizations (vectorization, etc.) have accurate cost
  /// information, matching how `opt`'s new pass manager driver configures
  /// its PassBuilder when a TargetMachine is available. Passing nullptr
  /// (the default) still runs a fully target-independent pipeline.
  void run(llvm::Module &M, const OptimizerOptions &Opts,
           llvm::TargetMachine *TM = nullptr) const;
};

} // namespace feme

#endif // FEME_OPTIMIZER_OPTIMIZERPIPELINE_H
