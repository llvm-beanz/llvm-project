//===- Token.h - DXBC assembler lexer token -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the Token type produced by Lexer and consumed by Parser.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_TOKEN_H
#define FEME_DXBC_ASSEMBLER_TOKEN_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace feme {
namespace dxbc {

enum class TokenKind {
  Identifier,     // mnemonics, register names, keywords
  Integer,        // 123
  Float,          // 1.0, .5, 1e-3
  Comma,          // ,
  Dot,            // .
  LParen,         // (
  RParen,         // )
  LBracket,       // [
  RBracket,       // ]
  Minus,          // -
  Pipe,           // |
  EndOfStatement, // newline (statements are one per line)
  Eof,
  Unknown,
};

/// A single lexical token. Numeric literals keep their raw source spelling
/// (\c Spelling) rather than a pre-parsed value: Parser converts them to
/// the width/representation each grammar production needs (e.g. an unsigned
/// register index vs. a float32 immediate bit pattern), and keeping the raw
/// text here keeps Lexer format-agnostic.
struct Token {
  TokenKind Kind = TokenKind::Unknown;
  llvm::StringRef Spelling;
  unsigned Line = 0;
  unsigned Column = 0;
};

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_TOKEN_H
