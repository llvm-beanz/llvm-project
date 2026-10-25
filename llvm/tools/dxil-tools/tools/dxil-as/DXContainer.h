//===- DXContainer.h - Legacy DXIL container assembly ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_TOOLS_DXIL_AS_DXCONTAINER_H
#define DXIL_TOOLS_DXIL_AS_DXCONTAINER_H

#include "llvm/Support/Error.h"
#include <string>

namespace dxil {
class Module;

llvm::Expected<std::string> assembleDXContainer(const Module &M,
                                                bool PreserveUseListOrder);
} // namespace dxil

#endif
