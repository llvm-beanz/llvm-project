//===- Options.td - Options for the `feme` frontend -----------*- tblgen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Option definitions for feme's command line tool (see the "Command Line
// Tool(s)" section of feme/docs/Design.md), consumed by llvm::opt's
// TableGen backend (-gen-opt-parser-defs) to produce an `OptTable`. This is
// what lets `feme` itself, and any embedding driver, parse arguments without
// depending on `llvm::cl::opt` global state (see the "Core Architectural
// Principle: No Global State" section of the design doc).
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def help : Flag<["-", "--"], "help">,
    HelpText<"Display available options">;
def h : Flag<["-"], "h">, Alias<help>,
    HelpText<"Alias for --help">;

def version : Flag<["-", "--"], "version">,
    HelpText<"Display the version of feme">;

def target_EQ : Joined<["--"], "target=">,
    HelpText<"Output format/target to translate to "
             "(e.g. dxil, spirv, or a target triple)">,
    MetaVarName<"<format>">;

def o : JoinedOrSeparate<["-"], "o">,
    HelpText<"Write output to <file>">,
    MetaVarName<"<file>">;
