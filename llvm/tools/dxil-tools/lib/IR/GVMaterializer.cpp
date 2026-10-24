//===-- GVMaterializer.cpp - Base implementation for GV materializers -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Minimal implementation of the abstract interface for materializing
// GlobalValues.
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/GVMaterializer.h"

dxil::GVMaterializer::~GVMaterializer() = default;
