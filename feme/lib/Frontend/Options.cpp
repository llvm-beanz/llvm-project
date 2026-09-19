//===- Options.cpp - Option info & table for feme ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Frontend/Options.h"

#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"

using namespace llvm::opt;

namespace feme::frontend {

namespace {

#define OPTTABLE_CODE
#include "feme/Frontend/Options.inc"
#undef OPTTABLE_CODE

/// The `OptTable` for feme's command line options.
class FeMeOptTable : public OptTable {
public:
  FeMeOptTable() : OptTable(optionTables()) {}
};

} // namespace

const OptTable &getOptTable() {
  static const FeMeOptTable Table;
  return Table;
}

} // namespace feme::frontend
