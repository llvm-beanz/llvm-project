//===- AsmPrinter.h - DXBC assembly text emission -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares printAssembly, which renders a parsed instruction stack back to
// the textual DXBC assembly syntax Parser accepts. Used by `dxbc-as`'s
// `--emit=asm` mode, primarily to let tests/users sanity-check how the
// parser understood an input (e.g. default swizzles/write masks made
// explicit) without needing to decode the binary encoding.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_ASMPRINTER_H
#define FEME_DXBC_ASSEMBLER_ASMPRINTER_H

#include "feme/DXBC/Assembler/Instruction.h"
#include "llvm/ADT/ArrayRef.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace feme {
namespace dxbc {

/// Prints \p Program to \p OS as DXBC assembly text, one instruction per
/// line, in the same syntax parseAssembly accepts (i.e. printing then
/// re-parsing is a round trip).
void printAssembly(llvm::ArrayRef<Instruction> Program, llvm::raw_ostream &OS);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_ASMPRINTER_H
