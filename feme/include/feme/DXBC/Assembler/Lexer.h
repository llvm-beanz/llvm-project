//===- Lexer.h - DXBC assembler lexer -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares Lexer, the tokenizer for the Microsoft/`fxc` DXBC disassembly
// text syntax (see feme/docs/Design.md's "dxbc-as" section). This is the
// first stage of dxbc-as's lex -> parse -> encode pipeline.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_LEXER_H
#define FEME_DXBC_ASSEMBLER_LEXER_H

#include "feme/DXBC/Assembler/Token.h"
#include "llvm/ADT/StringRef.h"

namespace feme {
namespace dxbc {

/// Tokenizes DXBC assembly source text. Never fails: malformed input (e.g.
/// a stray character not part of any token) is reported to the caller as a
/// TokenKind::Unknown token rather than an error, so a single Lexer object
/// can be driven straight from untrusted/fuzzer-supplied bytes without ever
/// aborting -- Parser is responsible for turning an Unknown token into a
/// diagnostic (see the "must not crash on untrusted input" principle in
/// feme/docs/Design.md's Diagnostics section, which applies just as much to
/// this standalone testing tool's own input).
class Lexer {
public:
  explicit Lexer(llvm::StringRef Source);

  /// Returns the next token and advances past it. Returns a TokenKind::Eof
  /// token forever once the end of \p Source is reached.
  Token next();

private:
  char peek(unsigned Offset = 0) const;
  char advance();
  bool isAtEnd() const;
  void skipLineCommentAndWhitespace();

  Token makeToken(TokenKind Kind, const char *Start) const;
  Token lexNumber();
  Token lexIdentifier();

  llvm::StringRef Source;
  size_t Pos = 0;
  unsigned Line = 1;
  unsigned Column = 1;
  /// Position of the token currently being lexed, recorded before scanning
  /// so Token::Line/Column point at a token's first character rather than
  /// (as Line/Column do once scanning finishes) its last.
  unsigned StartLine = 1;
  unsigned StartColumn = 1;
};

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_LEXER_H
