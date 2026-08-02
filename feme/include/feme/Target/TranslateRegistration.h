//===- TranslateRegistration.h - feme-translate hooks ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the registration hook exposing
// feme::TargetMachineBackend to feme-translate as the `--llvm-backend` flag
// (see the "Testing Tools" / `feme-translate` section of
// feme/docs/Design.md).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_TRANSLATEREGISTRATION_H
#define FEME_TARGET_TRANSLATEREGISTRATION_H

namespace feme {

/// Registers feme::TargetMachineBackend with MLIR's translation registry
/// under the `llvm-backend` name, for use by feme-translate and any other
/// tool linking MLIRTranslateLib. Also initializes every LLVM target
/// configured into this build (as tools like llc do), so `--llvm-backend`'s
/// `--target-triple` option can name any of them.
void registerTargetMachineBackendTranslation();

} // namespace feme

#endif // FEME_TARGET_TRANSLATEREGISTRATION_H
