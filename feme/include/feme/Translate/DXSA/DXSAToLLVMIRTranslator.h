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

#include <memory>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace mlir {
class ModuleOp;
} // namespace mlir

namespace feme {
namespace dxsa {

/// Translates the single `dxsa.module` nested in \p Source into DXIL-shaped
/// LLVM IR: a `void @main()` entry point whose body is the shader's
/// instruction stream lowered to scalar, per-component `dx.op.*` calls and
/// native LLVM instructions, plus the `!dx.*` module metadata a DXIL
/// consumer expects.
///
/// Returns null and emits diagnostics through \p Source's MLIRContext if the
/// module uses a construct that is not translated yet.
std::unique_ptr<llvm::Module> translateToLLVMIR(mlir::ModuleOp Source,
                                                llvm::LLVMContext &Context);

} // namespace dxsa
} // namespace feme

#endif // FEME_TRANSLATE_DXSA_DXSATOLLVMIRTRANSLATOR_H
