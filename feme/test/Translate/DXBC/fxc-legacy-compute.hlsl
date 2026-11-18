// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: system-fxc
// RUN: %fxc /nologo /WX /T cs_5_0 /E main /D USE_REGISTER_SPACE=0 \
// RUN:   /Fo %t.sm50.dxbc %s
// RUN: %fxc /nologo /WX /T cs_5_1 /E main /D USE_REGISTER_SPACE=1 \
// RUN:   /Fo %t.sm51.dxbc %s
// RUN: feme-translate --import-dxbc %t.sm50.dxbc \
// RUN:   | FileCheck %s --check-prefixes=SM50,DXSA
// RUN: feme-translate --import-dxbc %t.sm51.dxbc \
// RUN:   | FileCheck %s --check-prefixes=SM51,DXSA
// RUN: feme-translate --import-dxbc %t.sm50.dxbc \
// RUN:   | feme-translate --dxsa-to-llvmir \
// RUN:   | FileCheck %s --check-prefixes=IR50,IR
// RUN: feme-translate --import-dxbc %t.sm51.dxbc \
// RUN:   | feme-translate --dxsa-to-llvmir \
// RUN:   | FileCheck %s --check-prefixes=IR51,IR

// Exercise the real legacy compiler's containers, declarations and
// instructions, not only dxbc-as output. SM5.1 separates a range ID from its
// register and space; a nonzero register and space catch accidentally treating
// that ID as the binding. This tests compilation/import/translation, not D3D12
// device execution.
#if USE_REGISTER_SPACE
RWByteAddressBuffer Output : register(u3, space7);
#else
RWByteAddressBuffer Output : register(u3);
#endif

[numthreads(8, 1, 1)] void main(uint3 ThreadID : SV_DispatchThreadID) {
  Output.Store(4 * ThreadID.x, (ThreadID.x * 17u + 23u) ^ (ThreadID.x >> 3));
}

// SM50: dxsa.module compute_shader 5 0
// SM51: dxsa.module compute_shader 5 1
// SM50: dxsa.dcl_uav_raw <id = 3>
// SM51: dxsa.dcl_uav_raw <id = 0, lbound = 3, ubound = 3, space = 7>
// DXSA: dxsa.dcl_input
// DXSA: dxsa.dcl_thread_group <x = 8, y = 1, z = 1>
// DXSA: dxsa.store_raw
// DXSA: dxsa.ret

// IR: define void @main()
// IR: call i32 @dx.op.threadId.i32(i32 93, i32 0)
// IR: call i32 @dx.op.tertiary.i32(i32 48, i32 {{%[0-9]+}}, i32 17, i32 23)
// IR: lshr i32 {{%[0-9]+}}, 3
// IR: xor i32
// IR: call void @dx.op.bufferStore.i32(i32 69,
// IR50: !{{[0-9]+}} = !{i32 0, ptr addrspace(1) undef, !"U0", i32 0, i32 3,
// IR50-SAME: i32 1,
// IR51: !{{[0-9]+}} = !{i32 0, ptr addrspace(1) undef, !"U0", i32 7, i32 3,
// IR51-SAME: i32 1,
// IR: ![[THREADS:[0-9]+]] = !{i32 8, i32 1, i32 1}
// IR: !{{[0-9]+}} = !{i32 0, i64 256, i32 4, ![[THREADS]]}
