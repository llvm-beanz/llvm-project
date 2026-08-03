//===- dxbc-as-fuzzer.cpp - Fuzzer for the DXBC assembly parser ---*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fuzzer for feme::dxbc::parseAssembly (and, on every input the parser
// accepts, feme::dxbc::encodeProgram/wrapInContainer). `dxbc-as` is
// deliberately designed to accept arbitrary, potentially hand-edited text
// (it exists to make it *easy* to hand-author malformed inputs for the
// DXBC importer's own fuzzer/tests), so it must never crash on any byte
// sequence handed to it, matching the "Testing Strategy" requirement in
// feme/docs/Design.md that every FeMe-adjacent binary/text format parser
// ships an llvm-fuzzer-style harness.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/AsmPrinter.h"
#include "feme/DXBC/Assembler/Encoder.h"
#include "feme/DXBC/Assembler/Parser.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme::dxbc;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  llvm::StringRef Source(reinterpret_cast<const char *>(Data), Size);

  llvm::Expected<Program> Parsed = parseAssembly(Source);
  if (!Parsed) {
    llvm::consumeError(Parsed.takeError());
    return 0;
  }

  // Successfully-parsed input should always be encodable and re-printable
  // without crashing; exercise both remaining pipeline stages too, since a
  // syntactically-valid-but-semantically-odd program (e.g. mixing register
  // kinds no real shader would) is exactly the kind of input a fuzzer is
  // best at finding.
  std::string Discard;
  llvm::raw_string_ostream OS(Discard);
  printAssembly(*Parsed, OS);

  llvm::Expected<llvm::SmallVector<uint32_t, 64>> Bytecode =
      encodeProgram(*Parsed);
  if (!Bytecode) {
    llvm::consumeError(Bytecode.takeError());
    return 0;
  }
  llvm::SmallVector<char, 256> Container;
  wrapInContainer(*Bytecode, Container);
  return 0;
}
