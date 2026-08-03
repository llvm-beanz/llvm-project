//===- AsmPrinter.h - DXBC assembly text emission -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares printAssembly, which re-emits a parsed Program as normalized
// DXBC assembly text. It is the inverse of Parser: re-parsing printAssembly's
// output must produce an identical Program, which is what dxbc-as's
// `--emit asm` mode exists to let tests check.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_ASMPRINTER_H
#define FEME_DXBC_ASSEMBLER_ASMPRINTER_H

#include "feme/DXBC/Assembler/Parser.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace feme {
namespace dxbc {

/// Writes \p Program to \p OS as DXBC assembly text.
void printAssembly(const Program &Program, llvm::raw_ostream &OS);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_ASMPRINTER_H
