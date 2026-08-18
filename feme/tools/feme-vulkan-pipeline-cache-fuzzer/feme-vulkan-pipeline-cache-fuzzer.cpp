//===- feme-vulkan-pipeline-cache-fuzzer.cpp - Fuzzer for the blob parser ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fuzzer for feme::vulkan::parsePipelineCacheBlob: the blob
// `vkCreatePipelineCache`'s `pInitialData` hands the ICD is fully
// attacker-controlled input (see "Pipeline Cache" in
// feme/docs/FeMeVulkanDesign.md), so fuzzing its parser is a V4 requirement,
// matching how every other externally-defined binary format FeMe consumes
// (SPIR-V, DXIL, DXBC) already is.
//
//===----------------------------------------------------------------------===//

#include "PipelineCache.h"

#include "llvm/ADT/ArrayRef.h"

using namespace feme::vulkan;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  // A fixed, arbitrary UUID/vendor/device triple: the parser's job is to
  // reject anything not matching it, so which values are used does not
  // change what is being fuzzed (the bounds-checked parsing itself), only
  // which inputs happen to pass the early header/UUID checks and reach the
  // rest of the parser -- both matter, so the corpus should include blobs
  // this fuzzer itself produced via `serializePipelineCacheBlob` with this
  // exact UUID/vendor/device to exercise the code past that point.
  uint8_t UUID[VK_UUID_SIZE] = {0};
  auto Parsed = parsePipelineCacheBlob(llvm::ArrayRef(Data, Size), UUID,
                                       /*VendorID=*/0x10000,
                                       /*DeviceID=*/1);
  (void)Parsed;
  return 0;
}
