//===-- llvm/IR/Mangler.h - Self-contained name mangler ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Unified name mangler for various backends.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_MANGLER_H
#define DXIL_IR_MANGLER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

namespace dxil {

class DataLayout;
class GlobalValue;

class Mangler {
  /// We need to give global values the same name every time they are mangled.
  /// This keeps track of the number we give to anonymous ones.
  mutable llvm::DenseMap<const GlobalValue *, unsigned> AnonGlobalIDs;

  /// This simple counter is used to unique value names.
  mutable unsigned NextAnonGlobalID;

public:
  Mangler() : NextAnonGlobalID(1) {}

  /// Print the appropriate prefix and the specified global variable's name.
  /// If the global variable doesn't have a name, this fills in a unique name
  /// for the global.
  void getNameWithPrefix(llvm::raw_ostream &OS, const GlobalValue *GV,
                         bool CannotUsePrivateLabel) const;
  void getNameWithPrefix(llvm::SmallVectorImpl<char> &OutName,
                         const GlobalValue *GV,
                         bool CannotUsePrivateLabel) const;

  /// Print the appropriate prefix and the specified name as the global variable
  /// name. GVName must not be empty.
  static void getNameWithPrefix(llvm::raw_ostream &OS,
                                const llvm::Twine &GVName,
                                const DataLayout &DL);
  static void getNameWithPrefix(llvm::SmallVectorImpl<char> &OutName,
                                const llvm::Twine &GVName,
                                const DataLayout &DL);
};

} // End llvm namespace

#endif
