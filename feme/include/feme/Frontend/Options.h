//===- Options.h - Option info & table for feme --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the `llvm::opt::OptTable` used to parse feme's command line
// options (see feme/include/feme/Frontend/Options.td and the "Command Line
// Tool(s)" section of feme/docs/Design.md). Both the `feme` CLI tool and any
// embedding driver link against this to get identical, `cl::opt`-free option
// parsing (see the "Core Architectural Principle: No Global State" section
// of the design doc).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_FRONTEND_OPTIONS_H
#define FEME_FRONTEND_OPTIONS_H

#include "llvm/Option/OptTable.h"

namespace feme::frontend {

/// IDs for each option declared in Options.td, usable to query a parsed
/// `llvm::opt::ArgList` (e.g. `Args.hasArg(OPT_help)`).
enum ID {
  OPT_INVALID = 0, // This is not a valid option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "feme/Frontend/Options.inc"
#undef OPTION
  LastOption
};

/// Returns the `OptTable` describing feme's command line options.
const llvm::opt::OptTable &getOptTable();

} // namespace feme::frontend

#endif // FEME_FRONTEND_OPTIONS_H
