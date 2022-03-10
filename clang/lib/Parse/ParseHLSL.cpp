//===--- ParseHLSL.cpp - HLSL-specific parsing support --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Parse/Parser.h"

using namespace clang;

void Parser::ParseHLSLSemantics(ParsedAttributes &Attrs,
                                SourceLocation *EndLoc) {
  assert(Tok.is(tok::colon) && "Not a HLSL Semantic");
  ConsumeToken();

  IdentifierInfo *II = Tok.getIdentifierInfo();
  SourceLocation Loc = ConsumeToken();
  if (EndLoc)
    *EndLoc = Tok.getLocation();
  ParsedAttr::Kind AttrKind =
      ParsedAttr::getParsedKind(II, nullptr, ParsedAttr::AS_HLSLSemantic);
  // II->getNameStart()
  if (AttrKind == ParsedAttr::UnknownAttribute ||
      AttrKind == ParsedAttr::IgnoredAttribute) {
    ConsumeToken();
    return;
  }
  Attrs.addNew(II, Loc, nullptr, SourceLocation(), nullptr, 0,
               ParsedAttr::AS_HLSLSemantic);
}
