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

groupshared uint Scratch[64];

[numthreads(8, 4, 2)] void main(uint3 DispatchID : SV_DispatchThreadID,
                                uint3 GroupID : SV_GroupID,
                                uint GroupIndex : SV_GroupIndex) {
  uint Group = GroupID.x + 2 * (GroupID.y + 2 * GroupID.z);
  uint Index = Group * 64 + GroupIndex;
  Scratch[GroupIndex] = (Index * 17u + 23u) ^ (Index >> 3);
  GroupMemoryBarrierWithGroupSync();
  uint Neighbor = Scratch[GroupIndex ^ 32];
  // Finish every read before reusing the same shared storage.
  GroupMemoryBarrierWithGroupSync();
  Scratch[GroupIndex] = Neighbor + Group * 101;
  GroupMemoryBarrierWithGroupSync();
  uint OutputIndex = DispatchID.x + 16 * (DispatchID.y + 8 * DispatchID.z);
  Output.Store(4 * OutputIndex, Scratch[GroupIndex ^ 1]);
}

// IR: addrspace(3) global [64 x i32]
// IR: define void @main()
// IR-DAG: call i32 @dx.op.groupId.i32(i32 94, i32 0)
// IR-DAG: call i32 @dx.op.groupId.i32(i32 94, i32 1)
// IR-DAG: call i32 @dx.op.groupId.i32(i32 94, i32 2)
// IR-DAG: call i32 @dx.op.flattenedThreadIdInGroup.i32(i32 96)
// IR: store i32 {{.*}}, ptr addrspace(3)
// IR: call void @dx.op.barrier(i32 80, i32 9)
// IR: load i32, ptr addrspace(3)
// IR: call void @dx.op.barrier(i32 80, i32 9)
// IR: store i32 {{.*}}, ptr addrspace(3)
// IR: call void @dx.op.barrier(i32 80, i32 9)
// IR: load i32, ptr addrspace(3)
// SPACE0: !{{[0-9]+}} = !{i32 0, {{.*}}, !"{{.*}}", i32 0, i32 3, i32 1, i32 11,
// SPACE7: !{{[0-9]+}} = !{i32 0, {{.*}}, !"{{.*}}", i32 7, i32 3, i32 1, i32 11,
// IR: !{{[0-9]+}} = !{i32 8, i32 4, i32 2}
