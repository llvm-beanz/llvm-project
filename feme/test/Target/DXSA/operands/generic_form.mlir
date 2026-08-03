// RUN: feme-opt %s -split-input-file --mlir-print-op-generic --verify-roundtrip | FileCheck %s

// CHECK: "dxsa.add"() <{dst = #dxsa.dst_operand<r<0, <x, y>>>, lhs = #dxsa.src_operand<-|r<1, min16f, nonuniform>|>, rhs = #dxsa.src_operand<d(0x3FF0000000000000, 0x4000000000000000)>}>
dxsa.add r<0, <x, y>>, -|r<1, min16f, nonuniform>|, d(0x3FF0000000000000, 0x4000000000000000)

// -----

// CHECK: "dxsa.add"() <{dst = #dxsa.dst_operand<null>, lhs = #dxsa.src_operand<r<1>>, rhs = #dxsa.src_operand<r<2>>}>
dxsa.add null, r<1>, r<2>

// -----

// CHECK: "dxsa.add"() <{dst = #dxsa.dst_operand<null<vector>>, lhs = #dxsa.src_operand<l(0x2A)>, rhs = #dxsa.src_operand<v<42 : i64>>}>
dxsa.add null<vector>, l(0x2A), v<42 : i64>
