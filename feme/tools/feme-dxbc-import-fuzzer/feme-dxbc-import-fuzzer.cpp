//===- feme-dxbc-import-fuzzer.cpp - Fuzzer for the DXBC importer -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fuzzer for feme::dxsa::deserialize (BinaryParser.cpp), the DXBC importer's
// hand-written token decoder over untrusted binary input. FeMe consumes
// externally-defined binary formats supplied by untrusted sources at driver
// runtime, so fuzzing each importer is a v1 requirement (see "Testing
// Strategy" in feme/docs/Design.md); DXBC's `BinaryParser` is ~3800 lines of
// hand-written decoding and, unlike `dxbc-as-fuzzer` (which fuzzes the
// assembler's *text* parser), this harness exercises the same binary token
// stream a real DXBC-carrying driver input would.
//
//===----------------------------------------------------------------------===//

#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Target/DXSA/BinaryParser.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

using namespace feme;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  // A fresh MLIRContext per input, matching how the SPIR-V/DXIL importer
  // fuzzers use a fresh feme::Context per input: BinaryParser must not rely
  // on any state surviving across calls (see the "No Global State" principle
  // in feme/docs/Design.md). The `dxsa` dialect must be registered before
  // construction, matching feme::registerDXSAImportBinTranslation
  // (TranslateRegistration.cpp), the non-fuzzer entry point to the same
  // deserialize() call.
  mlir::DialectRegistry Registry;
  Registry.insert<dxsa::DXSADialect>();
  mlir::MLIRContext Context(Registry);
  llvm::SourceMgr SourceMgr;
  SourceMgr.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(reinterpret_cast<const char *>(Data), Size),
          "fuzzer-input", /*RequiresNullTerminator=*/false),
      llvm::SMLoc());
  mlir::OwningOpRef<dxsa::ModuleOp> Module =
      dxsa::deserialize(SourceMgr, &Context);
  return 0;
}
