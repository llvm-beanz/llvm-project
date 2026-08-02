//===- TargetMachineBackend.cpp - Generic TargetMachine Backend ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/TargetMachineBackend.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

using namespace feme;

llvm::Error TargetMachineBackend::run(llvm::Module &M,
                                      const BackendOptions &Opts,
                                      llvm::raw_pwrite_stream &Out) const {
  llvm::Triple TheTriple(llvm::Triple::normalize(Opts.TargetTriple));

  std::string LookupError;
  const llvm::Target *TheTarget =
      llvm::TargetRegistry::lookupTarget(TheTriple, LookupError);
  if (!TheTarget)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "no registered LLVM target for triple '" +
                                       Opts.TargetTriple + "': " + LookupError);

  // Uses default CPU/features/relocation-model/codegen options: FeMe's v1
  // retargeting use cases (see feme/docs/Design.md) don't need per-target
  // tuning yet, and BackendOptions is the place that would grow such knobs
  // if/when a concrete client needs them.
  std::unique_ptr<llvm::TargetMachine> Target(TheTarget->createTargetMachine(
      TheTriple, /*CPU=*/"", /*Features=*/"", llvm::TargetOptions(),
      /*RM=*/std::nullopt));
  if (!Target)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "could not allocate a TargetMachine for triple '" + Opts.TargetTriple +
            "'");

  M.setTargetTriple(TheTriple);
  M.setDataLayout(Target->createDataLayout());

  llvm::legacy::PassManager PM;
  if (Target->addPassesToEmitFile(PM, Out, /*DwoOut=*/nullptr, Opts.FileType))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "target '" + Opts.TargetTriple +
                                       "' does not support emitting the "
                                       "requested file type");

  PM.run(M);
  return llvm::Error::success();
}
