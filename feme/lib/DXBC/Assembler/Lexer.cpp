//===- Lexer.cpp - DXBC assembler lexer ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Lexer.h"

using namespace feme::dxbc;

Lexer::Lexer(llvm::StringRef Source) : Source(Source) {}

bool Lexer::isAtEnd() const { return Pos >= Source.size(); }

char Lexer::peek(unsigned Offset) const {
  size_t Index = Pos + Offset;
  if (Index >= Source.size())
    return '\0';
  return Source[Index];
}

char Lexer::advance() {
  char C = Source[Pos++];
  if (C == '\n') {
    ++Line;
    Column = 1;
  } else {
    ++Column;
  }
  return C;
}

Token Lexer::makeToken(TokenKind Kind, const char *Start) const {
  Token T;
  T.Kind = Kind;
  T.Spelling = llvm::StringRef(Start, Source.data() + Pos - Start);
  T.Line = StartLine;
  T.Column = StartColumn;
  return T;
}

void Lexer::skipLineCommentAndWhitespace() {
  while (!isAtEnd()) {
    char C = peek();
    // Newlines are significant (statement separators); only skip
    // non-newline whitespace here.
    if (C == ' ' || C == '\t' || C == '\r') {
      advance();
      continue;
    }
    // Both '//' and ';' are used as comment markers across the DXBC
    // assembly dialects `fxc /dumpbin` and vkd3d-shader emit.
    if (C == '/' && peek(1) == '/') {
      while (!isAtEnd() && peek() != '\n')
        advance();
      continue;
    }
    if (C == ';') {
      while (!isAtEnd() && peek() != '\n')
        advance();
      continue;
    }
    break;
  }
}

Token Lexer::lexNumber() {
  const char *Start = Source.data() + Pos;
  bool IsFloat = false;

  // Hexadecimal literals are always integers; `fxc`-style disassembly uses
  // them for raw bit patterns (e.g. `l(0x3F800000)`), where a decimal
  // spelling would lose the distinction between a float and its encoding.
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X') &&
      isxdigit(static_cast<unsigned char>(peek(2)))) {
    advance(); // '0'
    advance(); // 'x'
    while (isxdigit(static_cast<unsigned char>(peek())))
      advance();
    return makeToken(TokenKind::Integer, Start);
  }

  while (isdigit(static_cast<unsigned char>(peek())))
    advance();
  if (peek() == '.' && isdigit(static_cast<unsigned char>(peek(1)))) {
    IsFloat = true;
    advance(); // '.'
    while (isdigit(static_cast<unsigned char>(peek())))
      advance();
  }
  if (peek() == 'e' || peek() == 'E') {
    char Next = peek(1);
    char NextNext = peek(2);
    bool ExponentDigitsFollow = isdigit(static_cast<unsigned char>(Next)) ||
                                ((Next == '+' || Next == '-') &&
                                 isdigit(static_cast<unsigned char>(NextNext)));
    if (ExponentDigitsFollow) {
      IsFloat = true;
      advance(); // 'e'/'E'
      if (peek() == '+' || peek() == '-')
        advance();
      while (isdigit(static_cast<unsigned char>(peek())))
        advance();
    }
  }
  // A trailing 'f' suffix (e.g. "1.0f") is common in hand-written DXBC
  // assembly; consume it without affecting the parsed value.
  if (IsFloat && (peek() == 'f' || peek() == 'F'))
    advance();

  return makeToken(IsFloat ? TokenKind::Float : TokenKind::Integer, Start);
}

Token Lexer::lexIdentifier() {
  const char *Start = Source.data() + Pos;
  while (isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
    advance();
  return makeToken(TokenKind::Identifier, Start);
}

Token Lexer::next() {
  skipLineCommentAndWhitespace();
  StartLine = Line;
  StartColumn = Column;

  if (isAtEnd())
    return makeToken(TokenKind::Eof, Source.data() + Pos);

  char C = peek();
  if (C == '\n') {
    const char *Start = Source.data() + Pos;
    advance();
    return makeToken(TokenKind::EndOfStatement, Start);
  }

  if (isdigit(static_cast<unsigned char>(C)) ||
      (C == '.' && isdigit(static_cast<unsigned char>(peek(1)))))
    return lexNumber();

  if (isalpha(static_cast<unsigned char>(C)) || C == '_')
    return lexIdentifier();

  const char *Start = Source.data() + Pos;
  switch (C) {
  case ',':
    advance();
    return makeToken(TokenKind::Comma, Start);
  case '.':
    advance();
    return makeToken(TokenKind::Dot, Start);
  case '(':
    advance();
    return makeToken(TokenKind::LParen, Start);
  case ')':
    advance();
    return makeToken(TokenKind::RParen, Start);
  case '[':
    advance();
    return makeToken(TokenKind::LBracket, Start);
  case ']':
    advance();
    return makeToken(TokenKind::RBracket, Start);
  case '-':
    advance();
    return makeToken(TokenKind::Minus, Start);
  case '+':
    advance();
    return makeToken(TokenKind::Plus, Start);
  case '{':
    advance();
    return makeToken(TokenKind::LBrace, Start);
  case '}':
    advance();
    return makeToken(TokenKind::RBrace, Start);
  case '|':
    advance();
    return makeToken(TokenKind::Pipe, Start);
  default:
    // Unrecognized character: report it as a single-character Unknown
    // token rather than failing outright, so callers (Parser) can produce
    // a clean diagnostic instead of the Lexer itself needing to handle
    // errors -- see the class comment in Lexer.h.
    advance();
    return makeToken(TokenKind::Unknown, Start);
  }
}
