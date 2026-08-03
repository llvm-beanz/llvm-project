// RUN: feme-opt %s --mlir-print-op-generic --verify-roundtrip | FileCheck %s

// CHECK:      "dxsa.module"() <{major_version = 5 : i32, minor_version = 0 : i32, program_type = #dxsa<program_type pixel_shader>}> ({
// CHECK-NEXT:   "dxsa.dcl_global_flags"() <{flags = #dxsa.global_flags<refactoringAllowed>}> : () -> ()
// CHECK-NEXT:   "dxsa.add"() <{dst = #dxsa.dst_operand<r<0>>, lhs = #dxsa.src_operand<r<1>>, rhs = #dxsa.src_operand<r<2>>}> : () -> ()
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_global_flags <refactoringAllowed>
  dxsa.add r<0>, r<1>, r<2>
}
