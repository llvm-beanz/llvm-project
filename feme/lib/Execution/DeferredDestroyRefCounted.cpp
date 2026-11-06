//===- DeferredDestroyRefCounted.cpp - Deferred-destroy keep-alive mixin ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `DeferredDestroyRefCounted` (WARP roadmap WS2(b)) is a template fully
// defined inline in its own header (`feme/include/feme/Execution/
// DeferredDestroyRefCounted.h`) -- this translation unit exists only so
// `FeMeExecution` keeps every one of its source files paired with a
// matching header, matching this codebase's usual one-concept-per-file
// convention (see `TaskExecutor.cpp`'s identical rationale).
//
//===----------------------------------------------------------------------===//

#include "feme/Execution/DeferredDestroyRefCounted.h"
