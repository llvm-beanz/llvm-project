//===- Verifier.h - LLVM IR Verifier ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the function verifier interface, that can be used for some
// sanity checking of input to the system, and for checking that transformations
// haven't done something bad.
//
// Note that this does not provide full 'java style' security and verifications,
// instead it just tries to ensure that code is well formed.
//
// To see what specifically is checked, look at the top of Verifier.cpp
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_VERIFIER_H
#define DXIL_IR_VERIFIER_H

namespace llvm {
class raw_ostream;
}

namespace dxil {

class Function;
class Module;

/// \brief Check a function for errors, useful for use when debugging a
/// pass.
///
/// If there are no errors, the function returns false. If an error is found,
/// a message describing the error is written to OS (if non-null) and true is
/// returned.
bool verifyFunction(const Function &F, llvm::raw_ostream *OS = nullptr);

/// \brief Check a module for errors.
///
/// If there are no errors, the function returns false. If an error is found,
/// a message describing the error is written to OS (if non-null) and true is
/// returned.
bool verifyModule(const Module &M, llvm::raw_ostream *OS = nullptr);

} // namespace dxil

#endif
