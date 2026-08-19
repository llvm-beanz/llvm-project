//===- RaisedIRVerifier.h - Diagnose leftover raised-IR conventions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::verifyNoRaisedIRRemains, which checks that a
// module handed to a real-ISA target's TargetMachine no longer uses FeMe's
// format-agnostic `llvm.dx.*`/`llvm.spv.*` intrinsics or `target("dx.")`/
// `target("spirv.")` resource handle types (see feme/docs/Design.md's
// "Per-Format Representation Strategy"). See the .cpp file for why this
// check exists.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_CORE_RAISEDIRVERIFIER_H
#define FEME_CORE_RAISEDIRVERIFIER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
class Module;
} // namespace llvm

namespace feme {

/// Checks that \p M contains no leftover format-agnostic `llvm.dx.*`/
/// `llvm.spv.*` intrinsic call, no leftover raw, not-yet-raised DXIL
/// calling-convention op (`dx.op.*`, see `feme::dxil::OpRaisingPass`'s own
/// known gaps), and no value/parameter of a `target("dx.")`/
/// `target("spirv.")` resource handle type. Returns an `llvm::Error` naming
/// the first such call/type found and the function it appears in, or
/// `Error::success()` if none remain.
///
/// A real-ISA target's own resource/raised-op lowering passes (e.g.
/// `feme::amdgpu::ResourceLoweringPass`/`RaisedLoweringPass`,
/// `feme::nvptx`'s counterparts) deliberately leave a binding or op they
/// cannot model entirely unrewritten rather than partially rewriting it
/// (see `ResourceLoweringPass`'s own class comment) -- but the raised
/// intrinsics/types those leave behind are not valid input to a real
/// `llvm::TargetMachine`: `AMDGPU`/`NVPTX`'s own ISel has no notion of
/// either, and `llvm::MVT::getVT` has no case for a `target("dx.")`/
/// `target("spirv.")` type, so it `llvm_unreachable`s outright once
/// instruction selection actually needs that value's type -- which,
/// depending on the specific op and subtarget, may not happen until deep in
/// codegen, well past this pipeline's own passes. Calling this right after
/// those lowering passes run turns that non-deterministic, hard-to-attribute
/// crash into the clean "unsupported" diagnostic feme/docs/CommandGuide/
/// feme.md's "Current limitations" section already promises: "a shader
/// using those will fail at this stage rather than at any point specific
/// to feme's own Driver/CLI logic".
llvm::Error verifyNoRaisedIRRemains(const llvm::Module &M,
                                    llvm::StringRef TargetDescription);

} // namespace feme

#endif // FEME_CORE_RAISEDIRVERIFIER_H
