//===- LexerTest.cpp - Unit tests for feme::dxbc::Lexer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/Lexer.h"
#include "gtest/gtest.h"

using namespace feme::dxbc;

namespace {

std::vector<Token> lexAll(llvm::StringRef Source) {
  Lexer Lex(Source);
  std::vector<Token> Tokens;
  while (true) {
    Token T = Lex.next();
    Tokens.push_back(T);
    if (T.Kind == TokenKind::Eof)
      break;
  }
  return Tokens;
}

TEST(LexerTest, EmptyInputIsJustEof) {
  std::vector<Token> Tokens = lexAll("");
  ASSERT_EQ(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::Eof);
}

TEST(LexerTest, IdentifierAndRegister) {
  std::vector<Token> Tokens = lexAll("mov r0");
  ASSERT_EQ(Tokens.size(), 3u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::Identifier);
  EXPECT_EQ(Tokens[0].Spelling, "mov");
  EXPECT_EQ(Tokens[1].Kind, TokenKind::Identifier);
  EXPECT_EQ(Tokens[1].Spelling, "r0");
  EXPECT_EQ(Tokens[2].Kind, TokenKind::Eof);
}

TEST(LexerTest, PunctuationAndSwizzle) {
  std::vector<Token> Tokens = lexAll("r0.xyzw, -r1, |r2|");
  std::vector<TokenKind> Kinds;
  for (const Token &T : Tokens)
    Kinds.push_back(T.Kind);
  std::vector<TokenKind> Expected = {
      TokenKind::Identifier, TokenKind::Dot,   TokenKind::Identifier,
      TokenKind::Comma,      TokenKind::Minus, TokenKind::Identifier,
      TokenKind::Comma,      TokenKind::Pipe,  TokenKind::Identifier,
      TokenKind::Pipe,       TokenKind::Eof,
  };
  EXPECT_EQ(Kinds, Expected);
}

TEST(LexerTest, ColonAndEquals) {
  // `fxc` spells SM5.1 register ranges as `[lo:hi]` and named trailing
  // fields as `space=<n>`.
  std::vector<Token> Tokens = lexAll("T0[3:7], space=0");
  std::vector<TokenKind> Kinds;
  for (const Token &T : Tokens)
    Kinds.push_back(T.Kind);
  std::vector<TokenKind> Expected = {
      TokenKind::Identifier, TokenKind::LBracket,   TokenKind::Integer,
      TokenKind::Colon,      TokenKind::Integer,    TokenKind::RBracket,
      TokenKind::Comma,      TokenKind::Identifier, TokenKind::Equals,
      TokenKind::Integer,    TokenKind::Eof,
  };
  EXPECT_EQ(Kinds, Expected);
}

TEST(LexerTest, HexIntegerLiterals) {
  std::vector<Token> Tokens = lexAll("0x3F800000 0XdeadBEEF 0x0");
  ASSERT_EQ(Tokens.size(), 4u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::Integer);
  EXPECT_EQ(Tokens[0].Spelling, "0x3F800000");
  EXPECT_EQ(Tokens[1].Kind, TokenKind::Integer);
  EXPECT_EQ(Tokens[1].Spelling, "0XdeadBEEF");
  EXPECT_EQ(Tokens[2].Kind, TokenKind::Integer);
  EXPECT_EQ(Tokens[2].Spelling, "0x0");
}

TEST(LexerTest, PlusAndBraces) {
  std::vector<Token> Tokens = lexAll("+{}");
  ASSERT_EQ(Tokens.size(), 4u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::Plus);
  EXPECT_EQ(Tokens[1].Kind, TokenKind::LBrace);
  EXPECT_EQ(Tokens[2].Kind, TokenKind::RBrace);
}

TEST(LexerTest, FloatAndIntegerLiterals) {
  std::vector<Token> Tokens = lexAll("1 1.5 .5 1e3 1.0e-2 1.0f");
  ASSERT_EQ(Tokens.size(), 7u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::Integer);
  EXPECT_EQ(Tokens[1].Kind, TokenKind::Float);
  EXPECT_EQ(Tokens[2].Kind, TokenKind::Float);
  EXPECT_EQ(Tokens[3].Kind, TokenKind::Float);
  EXPECT_EQ(Tokens[4].Kind, TokenKind::Float);
  EXPECT_EQ(Tokens[5].Kind, TokenKind::Float);
  EXPECT_EQ(Tokens[5].Spelling, "1.0f");
}

TEST(LexerTest, NewlineIsEndOfStatement) {
  std::vector<Token> Tokens = lexAll("ret\nret");
  ASSERT_EQ(Tokens.size(), 4u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::Identifier);
  EXPECT_EQ(Tokens[1].Kind, TokenKind::EndOfStatement);
  EXPECT_EQ(Tokens[2].Kind, TokenKind::Identifier);
  EXPECT_EQ(Tokens[3].Kind, TokenKind::Eof);
}

TEST(LexerTest, CommentsAreSkipped) {
  std::vector<Token> Tokens = lexAll("// a comment\nret ; another\n");
  ASSERT_EQ(Tokens.size(), 4u);
  EXPECT_EQ(Tokens[0].Kind, TokenKind::EndOfStatement);
  EXPECT_EQ(Tokens[1].Kind, TokenKind::Identifier);
  EXPECT_EQ(Tokens[1].Spelling, "ret");
  EXPECT_EQ(Tokens[2].Kind, TokenKind::EndOfStatement);
  EXPECT_EQ(Tokens[3].Kind, TokenKind::Eof);
}

TEST(LexerTest, UnknownCharacterIsReportedNotCrashed) {
  std::vector<Token> Tokens = lexAll("mov r0, @");
  ASSERT_EQ(Tokens.back().Kind, TokenKind::Eof);
  bool SawUnknown = false;
  for (const Token &T : Tokens)
    if (T.Kind == TokenKind::Unknown)
      SawUnknown = true;
  EXPECT_TRUE(SawUnknown);
}

TEST(LexerTest, TokenPositionsPointAtTokenStart) {
  std::vector<Token> Tokens = lexAll("mov r0\nadd r1");
  // "add" starts the second line, column 1.
  const Token *Add = nullptr;
  for (const Token &T : Tokens)
    if (T.Spelling == "add")
      Add = &T;
  ASSERT_NE(Add, nullptr);
  EXPECT_EQ(Add->Line, 2u);
  EXPECT_EQ(Add->Column, 1u);
}

} // namespace
