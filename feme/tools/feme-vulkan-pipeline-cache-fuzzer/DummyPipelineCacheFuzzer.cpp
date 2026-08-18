//===- DummyPipelineCacheFuzzer.cpp - Sanity-check entry point -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of main so this fuzz target can be built and run over a
// set of input files without linking libFuzzer/oss-fuzz, matching every
// other FeMe fuzzer's own Dummy*Fuzzer.cpp (e.g.
// feme-spirv-import-fuzzer/DummyImporterFuzzer.cpp).
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/FuzzerCLI.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

int main(int argc, char *argv[]) {
  return llvm::runFuzzerOnInputs(argc, argv, LLVMFuzzerTestOneInput);
}
