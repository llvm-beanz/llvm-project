//===- feme-dxil-import-fuzzer.cpp - Fuzzer for DXILImporter ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fuzzer for feme::DXILImporter. FeMe consumes externally-defined binary
// formats supplied by untrusted sources at driver runtime, so fuzzing each
// Importer is a v1 requirement (see "Testing Strategy" in
// feme/docs/Design.md), matching how other LLVM binary-format parsers
// (e.g. llvm-dis-fuzzer for bitcode) are fuzzed. DXILImporter in particular
// runs both a DXContainer parser and LLVM's bitcode reader over untrusted
// input, so it exercises more attack surface than a typical importer.
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"

using namespace feme;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  // A fresh Context per input, matching how other in-tree fuzzers (e.g.
  // llvm-dis-fuzzer) use a fresh LLVMContext per input: Importers must not
  // rely on any state surviving across calls (see the "No Global State"
  // principle in feme/docs/Design.md).
  Context Ctx;
  DXILImporter Importer;
  llvm::MemoryBufferRef Buffer(
      llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
      "fuzzer-input");
  llvm::consumeError(Importer.import(Buffer, ImportOptions{}, Ctx).takeError());
  return 0;
}
