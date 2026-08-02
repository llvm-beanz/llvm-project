//===- Backend.h - FeMe ISA-retargeting interface --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Backend, the interface a pipeline stage that
// lowers an llvm::Module to a target's binary encoding via LLVM's
// TargetMachine/codegen infrastructure implements. See the "Pipeline
// Abstraction" / "Backend (retargeting)" section of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_BACKEND_H
#define FEME_TARGET_BACKEND_H

#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
class Module;
class raw_pwrite_stream;
} // namespace llvm

namespace feme {

/// Options controlling how a Backend lowers a Module. Grows over time as
/// more targets need configuring (CPU/feature strings, relocation model,
/// ...), mirroring ImportOptions' single-plain-struct rationale: FeMe does
/// not use RTTI (see feme/.instructions.md), so a polymorphic per-target
/// options hierarchy could not be safely downcast.
struct BackendOptions {
  /// The LLVM target triple to lower to, e.g. "x86_64-unknown-unknown" or
  /// "spirv64-unknown-unknown" (the latter being the SPIR-V "null pipeline"
  /// validation path -- see feme/docs/Design.md's Retargeting to Native ISA
  /// section). Must name a target registered with LLVM's TargetRegistry.
  std::string TargetTriple;

  /// The kind of file to emit: an object file by default, or assembly text
  /// (useful for lit/FileCheck testing without a binary disassembler).
  llvm::CodeGenFileType FileType = llvm::CodeGenFileType::ObjectFile;
};

/// Lowers an llvm::Module to a target's binary encoding using existing LLVM
/// target infrastructure (llvm::TargetMachine). This is deliberately not
/// format-specific: once a program is llvm::Module, retargeting reuses
/// standard TargetMachine/codegen infrastructure regardless of which
/// frontend it came from. Implementors are stateless, statically-linked
/// components: the same Backend instance may be invoked concurrently from
/// multiple threads (see the "No Global State" principle in
/// feme/docs/Design.md).
class Backend {
public:
  virtual ~Backend() = default;

  /// Lowers \p M according to \p Opts, writing the resulting bytes to
  /// \p Out. Returns an Error if \p Opts.TargetTriple does not name a
  /// registered target, or if codegen for \p Opts.FileType is unsupported
  /// by that target.
  virtual llvm::Error run(llvm::Module &M, const BackendOptions &Opts,
                          llvm::raw_pwrite_stream &Out) const = 0;
};

} // namespace feme

#endif // FEME_TARGET_BACKEND_H
