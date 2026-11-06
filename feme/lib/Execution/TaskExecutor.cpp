//===- TaskExecutor.cpp - API-neutral single-worker task queue ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `TaskExecutor` (WARP roadmap WS2(a)) is fully defined inline in its own
// header (`feme/include/feme/Execution/TaskExecutor.h`) -- this
// translation unit exists only so `FeMeExecution` is a real library
// target (consistent with every other `feme/lib/*` library) rather than
// a header-only one.
//
//===----------------------------------------------------------------------===//

#include "feme/Execution/TaskExecutor.h"
