//===- Importer.h - FeMe format-import interface ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Importer, the interface a per-format binary
// importer implements. See the "Pipeline Abstraction" / "Importer" section
// of feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMPORT_IMPORTER_H
#define FEME_IMPORT_IMPORTER_H

#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"

namespace llvm {
class StringRef;
} // namespace llvm

namespace feme {

class Context;
class Module;

/// Options controlling how an Importer parses its input. Currently holds
/// only the one flag SPIR-V import needs; as more formats gain import-time
/// options this is expected to grow (e.g. into per-format nested structs)
/// rather than becoming a polymorphic hierarchy, since FeMe does not use
/// RTTI (see feme/.instructions.md) and so cannot safely downcast a base
/// ImportOptions reference to a format-specific subclass.
struct ImportOptions {
  /// SPIR-V only: structurize control flow into `spirv.mlir.selection`/
  /// `spirv.mlir.loop` ops during deserialization. See
  /// mlir::spirv::DeserializationOptions.
  ///
  /// Defaults to `false` -- see "SPIR-V import prerequisites" in
  /// feme/docs/FeMeVulkanDesign.md for the roadmap decision this codifies.
  /// MLIR's structurizer rejects some legal SPIR-V control flow graphs
  /// outright (notably an `OpPhi` in a loop merge block, which any loop
  /// with a value-producing `break` emits), which `SPIRVFallBackToUnstructuredControlFlow`
  /// alone would be enough to route around. But a real compiler's *simple*
  /// loops (no early exit at all) structurize successfully and still crash
  /// downstream: MLIR's own `spirv.mlir.loop` -> `llvm.br`/`llvm.cond_br`
  /// conversion pattern (`LoopPattern` in
  /// mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp) asserts on a loop
  /// whose merge block carries a value, which every loop with a
  /// loop-carried induction variable does. That failure happens in a
  /// later, independent pass (`feme::spirv::createConvertSPIRVToLLVMPass`),
  /// not during deserialization, so no retry here can catch it. FeMe's CPU
  /// pipeline already has to restructure DXIL's naturally unstructured
  /// CFGs (`feme::cpu::PreparePass`), so there is no benefit to attempting
  /// structured reconstruction for SPIR-V at all: unstructured
  /// deserialization -- plain block arguments and branches -- both
  /// succeeds unconditionally and converts to LLVM IR without going
  /// through the buggy structured patterns.
  bool SPIRVEnableControlFlowStructurization = false;

  /// SPIR-V only: if structurized deserialization fails, retry with
  /// structurization disabled rather than reporting the failure. Only
  /// consulted when a caller explicitly opts back into
  /// `SPIRVEnableControlFlowStructurization`; see that flag's comment for
  /// why structurization is off by default. Ignored when
  /// `SPIRVEnableControlFlowStructurization` is already false.
  bool SPIRVFallBackToUnstructuredControlFlow = true;
};

/// Parses a format's binary encoding into an in-memory Module. Implementors
/// are stateless, statically-linked components: the same Importer instance
/// may be invoked concurrently from multiple threads, each passing its own
/// Context (see the "No Global State" principle in feme/docs/Design.md).
class Importer {
public:
  virtual ~Importer() = default;

  /// Parses \p Buffer according to this Importer's format, using \p Ctx's
  /// MLIRContext/LLVMContext for any IR produced. Returns an Error if
  /// \p Buffer does not contain well-formed input for this format; FeMe
  /// must not crash on malformed/untrusted input (see "Diagnostics and
  /// Error Handling" in feme/docs/Design.md).
  virtual llvm::Expected<Module> import(llvm::MemoryBufferRef Buffer,
                                        const ImportOptions &Opts,
                                        Context &Ctx) const = 0;

  /// Returns the short name of the format this Importer parses (e.g.
  /// "spirv"), used by Driver/CLI format selection.
  virtual llvm::StringRef getFormatName() const = 0;
};

} // namespace feme

#endif // FEME_IMPORT_IMPORTER_H
