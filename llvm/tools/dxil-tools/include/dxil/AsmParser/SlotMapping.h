//===-- SlotMapping.h - Slot number mapping for unnamed values --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the SlotMapping struct.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_ASMPARSER_SLOTMAPPING_H
#define DXIL_ASMPARSER_SLOTMAPPING_H

#include "dxil/IR/TrackingMDRef.h"
#include <map>
#include <vector>

namespace dxil {

class GlobalValue;

/// This struct contains the mapping from the slot numbers to unnamed metadata
/// nodes and global values.
struct SlotMapping {
  std::vector<GlobalValue *> GlobalValues;
  std::map<unsigned, TrackingMDNodeRef> MetadataNodes;
};

} // end namespace llvm

#endif
