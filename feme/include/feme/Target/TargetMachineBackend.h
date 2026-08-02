//===- TargetMachineBackend.h - Generic TargetMachine Backend --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::TargetMachineBackend, a target-agnostic Backend
// built directly on llvm::TargetRegistry/llvm::TargetMachine. See the
// deviation note on SPIR-V retargeting in feme/docs/Design.md: this same
// Backend implementation is used both for the SPIR-V "null pipeline"
// (retargeting to LLVM's own SPIRV target) and for eventual real-ISA
// retargeting (X86, AArch64, ...) -- FeMe's own contribution is the thin
// glue selecting/configuring the right TargetMachine, not target-specific
// codegen, which this class is the embodiment of.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_TARGETMACHINEBACKEND_H
#define FEME_TARGET_TARGETMACHINEBACKEND_H

#include "feme/Target/Backend.h"

namespace feme {

/// A Backend that looks up \c Opts.TargetTriple in LLVM's TargetRegistry,
/// creates a default-configured llvm::TargetMachine for it, and runs that
/// TargetMachine's standard codegen pipeline to emit \c Opts.FileType.
/// Requires the named target to have been initialized (e.g. via
/// llvm::InitializeAllTargets()/InitializeAllTargetMCs()/
/// InitializeAllAsmPrinters(), as tools like llc do) before use; this class
/// does not itself register any targets, to avoid forcing every FeMe
/// consumer to link every target's codegen library.
class TargetMachineBackend : public Backend {
public:
  llvm::Error run(llvm::Module &M, const BackendOptions &Opts,
                  llvm::raw_pwrite_stream &Out) const override;
};

} // namespace feme

#endif // FEME_TARGET_TARGETMACHINEBACKEND_H
