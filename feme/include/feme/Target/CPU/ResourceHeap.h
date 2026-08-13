//===- ResourceHeap.h - CPU target physical heap materialization -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::materializeResourceHeap, which builds the
// physical resource heap a compiled shader expects from a host's
// traditionally-bound resource descriptors plus its logical dynamic heap --
// see "Descriptor heaps" in feme/docs/FeMeCPUDesign.md:
//
//   [descriptors for reserved bound ranges][caller's logical dynamic heap]
//
// Both feme::cpu::JITEngine::dispatch and feme-run call this to materialize
// the heap they pass to a dispatch, rather than duplicating the logic (see
// roadmap milestone 11's "teach JITEngine/.../feme-run to materialize
// physical heaps from bound ranges plus logical dynamic heaps"). It cannot
// live in libFeMeRuntimeCPU's own FeMeRuntimeCPU.c: that file is plain
// freestanding C compiled for the *shader's* own IR (see its file comment),
// with no dynamic allocation and no dependency on FeMe's own C++ code, so it
// has no way to host this host-side, `std::vector`-returning helper -- see
// the Status section's Deviation note in feme/docs/FeMeCPUDesign.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_RESOURCEHEAP_H
#define FEME_TARGET_CPU_RESOURCEHEAP_H

#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::cpu {

/// One traditionally-bound resource's host-supplied descriptors for a
/// dispatch, matched to one of \p Info's `BoundRanges` entries by (Space,
/// BaseRegister) -- see `feme::cpu::BoundResourceRange`. `Descriptors[j]` is
/// the descriptor for the range's array element `j`; a range longer than
/// `Descriptors` (or with no matching `BoundResourceBinding` at all) has its
/// remaining/every slot left as the zero descriptor (see "Descriptor
/// heaps": "A descriptor the host has not written is zero-filled").
struct BoundResourceBinding {
  uint32_t Space = 0;
  uint32_t BaseRegister = 0;
  llvm::ArrayRef<FemeDescriptor> Descriptors;
};

/// Materializes the physical resource heap a shader compiled with \p Info's
/// bound-resource layout expects: \p Info.ReservedResourceHeapSize
/// descriptors reserved for its traditionally-bound resources (filled from
/// \p Bindings, by matching each of \p Info.BoundRanges), followed by
/// \p DynamicHeap unchanged -- see the file comment above. For a shader
/// using no traditional binding (`Info.ReservedResourceHeapSize == 0`), the
/// result is exactly \p DynamicHeap's contents, still returned as an owned
/// copy so every caller shares one return convention.
std::vector<FemeDescriptor>
materializeResourceHeap(const ResourceInfo &Info,
                        llvm::ArrayRef<BoundResourceBinding> Bindings,
                        llvm::ArrayRef<FemeDescriptor> DynamicHeap);

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_RESOURCEHEAP_H
