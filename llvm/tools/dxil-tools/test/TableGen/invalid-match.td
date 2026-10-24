// RUN: not dxil-tblgen -gen-intrinsic -I %dxil_include %s 2>&1 | FileCheck %s

include "dxil/IR/Intrinsics.td"

def int_test_invalid : Intrinsic<[LLVMMatchType<0>]>;

// CHECK: error: Invalid matching number!
