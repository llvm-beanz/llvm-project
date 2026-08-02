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

#define OPTTABLE_STR_TABLE_CODE
#include "feme/Frontend/Options.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "feme/Frontend/Options.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "feme/Frontend/Options.inc"
#undef OPTION
};

/// The `OptTable` for feme's command line options. Building the prefix
/// table at construction time (rather than TableGen-precomputing it, as
/// `PrecomputedOptTable` does) keeps Options.td simple; feme's option count
/// is small enough that this is not a meaningful cost.
class FeMeOptTable : public GenericOptTable {
public:
  FeMeOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};

} // namespace

const OptTable &getOptTable() {
  static const FeMeOptTable Table;
  return Table;
}

} // namespace feme::frontend
