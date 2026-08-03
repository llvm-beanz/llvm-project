//===- Parser.h - DXBC assembler parser ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares Parser, which turns a token stream from Lexer into the
// instruction stack (a `std::vector<Instruction>`) Encoder/AsmPrinter
// consume. This is the second stage of dxbc-as's lex -> parse -> encode
// pipeline (see feme/docs/Design.md's "dxbc-as" section).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_PARSER_H
#define FEME_DXBC_ASSEMBLER_PARSER_H

#include "feme/DXBC/Assembler/Instruction.h"
#include "feme/DXBC/Assembler/Lexer.h"
#include "feme/DXBC/Assembler/Token.h"
#include "llvm/Support/Error.h"

#include <vector>

namespace feme {
namespace dxbc {

/// Parses \p Source (Microsoft/`fxc`-style DXBC assembly text) into a flat
/// list of Instructions, one per non-empty source line.
///
/// Never crashes on malformed input: any syntax error (unknown mnemonic,
/// wrong operand count, malformed register/immediate syntax, etc.) is
/// reported as an llvm::Error describing the problem and its line/column,
/// not a crash or assertion -- `dxbc-as` must tolerate arbitrary,
/// potentially fuzzer-generated text (see feme-dxbc-as-fuzzer).
llvm::Expected<std::vector<Instruction>> parseAssembly(llvm::StringRef Source);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_PARSER_H
