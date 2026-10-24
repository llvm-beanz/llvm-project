//===- LLLexer.h - Lexer for LLVM Assembly Files ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class represents the Lexer for .ll files.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_LIB_ASMPARSER_LLLEXER_H
#define DXIL_LIB_ASMPARSER_LLLEXER_H

#include "LLToken.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/SourceMgr.h"
#include <string>

namespace dxil {
  class Type;
  class LLVMContext;

  class LLLexer {
    const char *CurPtr;
    llvm::StringRef CurBuf;
    llvm::SMDiagnostic &ErrorInfo;
    llvm::SourceMgr &SM;
    LLVMContext &Context;

    // Information about the current token.
    const char *TokStart;
    lltok::Kind CurKind;
    std::string StrVal;
    unsigned UIntVal;
    Type *TyVal;
    llvm::APFloat APFloatVal;
    llvm::APSInt APSIntVal;

  public:
    explicit LLLexer(llvm::StringRef StartBuf, llvm::SourceMgr &SM,
                     llvm::SMDiagnostic &, LLVMContext &C);

    lltok::Kind Lex() {
      return CurKind = LexToken();
    }

    typedef llvm::SMLoc LocTy;
    LocTy getLoc() const { return llvm::SMLoc::getFromPointer(TokStart); }
    lltok::Kind getKind() const { return CurKind; }
    const std::string &getStrVal() const { return StrVal; }
    Type *getTyVal() const { return TyVal; }
    unsigned getUIntVal() const { return UIntVal; }
    const llvm::APSInt &getAPSIntVal() const { return APSIntVal; }
    const llvm::APFloat &getAPFloatVal() const { return APFloatVal; }

    bool Error(LocTy L, const llvm::Twine &Msg) const;
    bool Error(const llvm::Twine &Msg) const { return Error(getLoc(), Msg); }

    void Warning(LocTy WarningLoc, const llvm::Twine &Msg) const;
    void Warning(const llvm::Twine &Msg) const {
      return Warning(getLoc(), Msg);
    }

  private:
    lltok::Kind LexToken();

    int getNextChar();
    void SkipLineComment();
    lltok::Kind ReadString(lltok::Kind kind);
    bool ReadVarName();

    lltok::Kind LexIdentifier();
    lltok::Kind LexDigitOrNegative();
    lltok::Kind LexPositive();
    lltok::Kind LexAt();
    lltok::Kind LexDollar();
    lltok::Kind LexExclaim();
    lltok::Kind LexPercent();
    lltok::Kind LexVar(lltok::Kind Var, lltok::Kind VarID);
    lltok::Kind LexQuote();
    lltok::Kind Lex0x();
    lltok::Kind LexHash();

    uint64_t atoull(const char *Buffer, const char *End);
    uint64_t HexIntToVal(const char *Buffer, const char *End);
    void HexToIntPair(const char *Buffer, const char *End, uint64_t Pair[2]);
    void FP80HexToIntPair(const char *Buff, const char *End, uint64_t Pair[2]);
  };
  } // end namespace dxil

#endif
