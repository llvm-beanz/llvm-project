//===- Driver.h - FeMe full-toolchain orchestration -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::Driver, the orchestration layer described in the
// "Driver" section of feme/docs/Design.md: given a source format, a
// destination format/target triple, and an input buffer, it computes and
// runs the whole import -> translate -> raise/retarget chain, the same way
// Clang's driver builds compile+assemble+link jobs from a requested
// input/output pair rather than requiring callers to invoke each stage by
// hand. `Driver` is a thin layer on top of the `Importer`/`Translator`/
// `Backend` primitives (feme/include/feme/Import/Importer.h,
// feme/include/feme/Translate/Translator.h,
// feme/include/feme/Target/Backend.h) and FeMe's own raising passes
// (feme/include/feme/Transforms/...); it contains no format-specific parsing
// or codegen logic of its own.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DRIVER_DRIVER_H
#define FEME_DRIVER_DRIVER_H

#include "feme/Frontend/FrontendOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"

namespace feme {

class Context;

/// `Driver`'s `DriverOptions` is the same explicit-state struct `feme`'s
/// command line is parsed into (see feme/include/feme/Frontend/
/// FrontendOptions.h): the design's "Library API Shape" section calls for
/// the CLI and an eventual C API to share one `DriverOptions` shape, whether
/// populated by `feme::frontend::parseArgs` or directly by an embedding
/// library consumer.
using DriverOptions = feme::frontend::DriverOptions;

/// The result of a successful `Driver::run`: the final output bytes (an
/// object file, or a re-serialized binary format, depending on
/// `DriverOptions::Target`).
struct DriverResult {
  llvm::SmallVector<char, 0> Output;
};

/// Computes and runs the full `Importer` -> raising pass(es) -> `Translator`
/// -> `Backend` chain needed to go from `Opts.From` to `Opts.Target`. See
/// the "Driver" section of feme/docs/Design.md.
///
/// Currently supported `Opts.From` values are "dxil" and "spirv" (DXBC
/// import is not yet implemented -- see the Roadmap / Milestones section of
/// feme/docs/Design.md). `Opts.Target` may independently name "dxil",
/// "spirv" (re-serializing back to that format via its own LLVM backend),
/// or any other LLVM target triple registered with the `TargetRegistry`
/// (e.g. "amdgcn-amd-amdhsa") for real-ISA retargeting.
class Driver {
public:
  explicit Driver(Context &Ctx);

  llvm::Expected<DriverResult> run(llvm::MemoryBufferRef Input,
                                   const DriverOptions &Opts) const;

private:
  Context &Ctx;
};

} // namespace feme

#endif // FEME_DRIVER_DRIVER_H
