//===- FrontendOptions.cpp - feme driver options --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Frontend/FrontendOptions.h"

#include "feme/Frontend/Options.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"

using namespace llvm::opt;

namespace feme::frontend {

std::optional<DriverOptions> parseArgs(llvm::ArrayRef<const char *> Args,
                                       llvm::raw_ostream &Diags) {
  const OptTable &Opts = getOptTable();

  unsigned MissingArgIndex, MissingArgCount;
  InputArgList ParsedArgs =
      Opts.ParseArgs(Args, MissingArgIndex, MissingArgCount);

  if (MissingArgCount) {
    Diags << "error: argument to '" << ParsedArgs.getArgString(MissingArgIndex)
          << "' is missing " << "(expected " << MissingArgCount
          << " value(s))\n";
    return std::nullopt;
  }

  for (const Arg *A : ParsedArgs.filtered(OPT_UNKNOWN)) {
    Diags << "error: unknown argument '" << A->getAsString(ParsedArgs) << "'\n";
    return std::nullopt;
  }

  DriverOptions Opts_;
  Opts_.ShowHelp = ParsedArgs.hasArg(OPT_help);
  Opts_.ShowVersion = ParsedArgs.hasArg(OPT_version);

  if (const Arg *A = ParsedArgs.getLastArg(OPT_from_EQ))
    Opts_.From = A->getValue();
  if (const Arg *A = ParsedArgs.getLastArg(OPT_to_EQ))
    Opts_.To = A->getValue();
  if (const Arg *A = ParsedArgs.getLastArg(OPT_target_EQ))
    Opts_.Target = A->getValue();
  if (const Arg *A = ParsedArgs.getLastArg(OPT_o))
    Opts_.OutputFilename = A->getValue();

  llvm::SmallVector<std::string> Inputs;
  for (const Arg *A : ParsedArgs.filtered(OPT_INPUT))
    Inputs.push_back(A->getValue());

  // `--help`/`--version` are handled by the caller without requiring an
  // input file; anything else needs exactly one.
  if (!Opts_.ShowHelp && !Opts_.ShowVersion) {
    if (Inputs.empty()) {
      Diags << "error: no input file specified\n";
      return std::nullopt;
    }
    if (Inputs.size() > 1) {
      Diags << "error: feme only supports a single input file, got "
            << Inputs.size() << "\n";
      return std::nullopt;
    }
    Opts_.InputFilename = Inputs.front();
  }

  return Opts_;
}

} // namespace feme::frontend
