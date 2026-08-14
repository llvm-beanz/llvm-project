//===- DXSAToLLVMIRTranslator.h - dxsa dialect -> DXIL ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the translation from the `dxsa` lifting dialect (a decoded DXBC
// tokenized program) to DXIL-shaped LLVM IR, i.e. the DXBC -> DXIL edge of
// the translation matrix in feme/docs/Design.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSLATE_DXSA_DXSATOLLVMIRTRANSLATOR_H
#define FEME_TRANSLATE_DXSA_DXSATOLLVMIRTRANSLATOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace mlir {
class ModuleOp;
} // namespace mlir

namespace feme {
namespace dxsa {

/// One element of a real `ISGN`/`OSGN`/`ISG1`/`OSG1` signature read out of a
/// full `DXContainer` (see `feme/docs/Design.md`'s "Building complete legacy
/// DXBC containers for testing"), passed to `translateToLLVMIR` to override
/// the signature it would otherwise synthesize from a bare `.dxasm`
/// fixture's `dcl_input`/`dcl_output` declarations. `SystemValue` and
/// `CompType` are the raw `dxbc::D3DSystemValue`/`dxbc::SigComponentType`
/// encodings, kept as plain integers here so this header does not need to
/// depend on `llvm/BinaryFormat/DXContainer.h`.
struct ContainerSignatureElement {
  std::string Name;
  unsigned Index = 0;
  unsigned Register = 0;
  unsigned Mask = 0;
  unsigned SystemValue = 0;
  unsigned CompType = 0;
};

/// Translates the single `dxsa.module` nested in \p Source into DXIL-shaped
/// LLVM IR: a `void @main()` entry point whose body is the shader's
/// instruction stream lowered to scalar, per-component `dx.op.*` calls and
/// native LLVM instructions, plus the `!dx.*` module metadata a DXIL
/// consumer expects.
///
/// \p RealInputSignature and \p RealOutputSignature, when non-empty,
/// override the signature the translator would otherwise synthesize from
/// declarations, using real element names/indices/types read from a full
/// `DXContainer`.
///
/// Returns null and emits diagnostics through \p Source's MLIRContext if the
/// module uses a construct that is not translated yet.
std::unique_ptr<llvm::Module> translateToLLVMIR(
    mlir::ModuleOp Source, llvm::LLVMContext &Context,
    llvm::ArrayRef<ContainerSignatureElement> RealInputSignature = {},
    llvm::ArrayRef<ContainerSignatureElement> RealOutputSignature = {});

} // namespace dxsa
} // namespace feme

#endif // FEME_TRANSLATE_DXSA_DXSATOLLVMIRTRANSLATOR_H
