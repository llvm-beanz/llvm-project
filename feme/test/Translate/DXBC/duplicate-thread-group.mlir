// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// RUN: not feme-translate --dxsa-to-llvmir %s 2>&1 | FileCheck %s

dxsa.module compute_shader 5 0 {
  dxsa.dcl_thread_group <x = 8, y = 1, z = 1>
  dxsa.dcl_thread_group <x = 16, y = 1, z = 1>
  dxsa.ret
}

// CHECK: error: duplicate thread-group declaration
