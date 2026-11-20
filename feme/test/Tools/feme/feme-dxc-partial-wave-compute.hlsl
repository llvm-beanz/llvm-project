// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: system-dxc-dxil, directx-registered-target
// RUN: %dxil_dxc /nologo /WX /T cs_6_0 /E main /D USE_REGISTER_SPACE=0 /Fo %t.space0.dxil %s
// RUN: %dxil_dxc /nologo /WX /T cs_6_0 /E main /D USE_REGISTER_SPACE=1 /Fo %t.space7.dxil %s
// RUN: feme --target=dxil %t.space0.dxil -o %t.out0.dxil
// RUN: feme --target=dxil %t.space7.dxil -o %t.out7.dxil
// RUN: feme-translate --import-dxil %t.out0.dxil | opt -S -passes=verify \
// RUN:   | FileCheck %s --check-prefixes=IR,SPACE0 --implicit-check-not=feme.signature
// RUN: feme-translate --import-dxil %t.out7.dxil | opt -S -passes=verify \
// RUN:   | FileCheck %s --check-prefixes=IR,SPACE7 --implicit-check-not=feme.signature

#if USE_REGISTER_SPACE
RWByteAddressBuffer Output : register(u3, space7);
#else
RWByteAddressBuffer Output : register(u3);
#endif

groupshared uint Scratch[30];

[numthreads(5, 3, 2)] void main(uint3 DispatchID : SV_DispatchThreadID,
                               uint3 GroupID : SV_GroupID,
                               uint GroupIndex : SV_GroupIndex) {
  uint Group = GroupID.x + 3 * GroupID.y;
  uint Index = Group * 30 + GroupIndex;
  Scratch[GroupIndex] = (Index * 17u + 23u) ^ (Index >> 3);
  GroupMemoryBarrierWithGroupSync();
  uint Value = Scratch[(GroupIndex + 11) % 30] + Group * 101;
  uint DispatchIndex = DispatchID.x + 15 * (DispatchID.y + 6 * DispatchID.z);
  Output.Store(32 * Index, Value);
  Output.Store(32 * Index + 4, DispatchIndex);
  Output.Store(32 * Index + 8, WaveGetLaneCount());
  Output.Store(32 * Index + 12, WaveGetLaneIndex());
  Output.Store(32 * Index + 16, WaveReadLaneFirst(GroupIndex));
  Output.Store(32 * Index + 20, WaveActiveSum(1u));
  Output.Store(32 * Index + 24, WaveActiveSum(GroupIndex + 1));
  Output.Store(32 * Index + 28, WaveReadLaneFirst(Group));
}

// IR: addrspace(3) global [30 x i32]
// IR: define void @main()
// IR-DAG: call i32 @dx.op.waveGetLaneCount(i32 112)
// IR-DAG: call i32 @dx.op.waveGetLaneIndex(i32 111)
// IR-DAG: call i32 @dx.op.waveReadLaneFirst.i32(i32 118,
// IR-DAG: call i32 @dx.op.waveActiveOp.i32(i32 119,
// IR-DAG: call void @dx.op.barrier(i32 80, i32 9)
// SPACE0: !{{[0-9]+}} = !{i32 0, {{.*}}, !"{{.*}}", i32 0, i32 3, i32 1, i32 11,
// SPACE7: !{{[0-9]+}} = !{i32 0, {{.*}}, !"{{.*}}", i32 7, i32 3, i32 1, i32 11,
// IR: !{{[0-9]+}} = !{i32 5, i32 3, i32 2}
