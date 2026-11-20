// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// RUN: feme-translate --dxsa-to-llvmir %s | opt -S -passes=verify | FileCheck %s

dxsa.module compute_shader 5 0 {
  dxsa.dcl_thread_group <x = 1, y = 1, z = 1>
  dxsa.dcl_tgsm_raw g<0>, 16384
  dxsa.dcl_tgsm_structured g<1>, 16, 1024
  dxsa.ret
}

// CHECK: @g0 = internal addrspace(3) global [4096 x i32] undef, align 4
// CHECK: @g1 = internal addrspace(3) global [4096 x i32] undef, align 4
