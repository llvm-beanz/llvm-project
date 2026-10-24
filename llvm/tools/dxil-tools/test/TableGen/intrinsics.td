// RUN: dxil-tblgen -gen-intrinsic -I %dxil_include %s | FileCheck %s

include "dxil/IR/Intrinsics.td"

// CHECK:      #ifdef GET_INTRINSIC_ENUM_VALUES
// CHECK:      log, // llvm.log
// CHECK-NEXT: log10, // llvm.log10
// CHECK-NEXT: log2, // llvm.log2
// CHECK:      #ifdef GET_INTRINSIC_NAME_TABLE
// CHECK:      "llvm.log",
// CHECK-NEXT: "llvm.log10",
// CHECK-NEXT: "llvm.log2",
// CHECK:      #ifdef GET_INTRINSIC_OVERLOAD_TABLE
// CHECK:      #ifdef GET_FUNCTION_RECOGNIZER
// CHECK:      #ifdef GET_INTRINSIC_GENERATOR_GLOBAL
// CHECK:      static const unsigned IIT_Table[] = {
// CHECK-NEXT: 0x2E2E, (1U<<31) | 73, 0x10, (1U<<31) | 97, 0x1F1F, 0x2F2F, 0x2F2F,
// CHECK:      static const unsigned char IIT_LongEncodingTable[] = {
// CHECK-NEXT: /* 0 */ 0, 15, 3, 31, 3, 4, 30, 3, 1, 0,
// CHECK:      #ifdef GET_INTRINSIC_ATTRIBUTES
// CHECK:      #ifdef GET_INTRINSIC_MODREF_BEHAVIOR
// CHECK:      #ifdef GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
// CHECK:      #ifdef GET_LLVM_INTRINSIC_FOR_MS_BUILTIN
