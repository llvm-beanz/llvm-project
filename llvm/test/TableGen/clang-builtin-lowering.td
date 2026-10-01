// RUN: llvm-tblgen -gen-intrinsic-impl -I %p/../../include %s | FileCheck %s

include "llvm/IR/Intrinsics.td"

// CHECK-LABEL: #ifdef GET_LLVM_CLANG_BUILTIN_LOWERING

// CHECK: static constexpr Intrinsic::ClangBuiltinOperand [[CAST_ARGS:ClangBuiltinOperands[0-9_]+]][] = {
// CHECK-NEXT: {0, 0, false},
// CHECK-NEXT: {1, 8, true},
// CHECK-NEXT: };
// CHECK: {Intrinsic::dx_resource_updatecounter, [[CAST_ARGS]], Intrinsic::ClangBuiltinPredicateKind::Any, 0, ""},

// CHECK: static constexpr Intrinsic::ClangBuiltinOperand [[PERMUTED_ARGS:ClangBuiltinOperands[0-9_]+]][] = {
// CHECK-NEXT: {2, 0, false},
// CHECK-NEXT: {0, 0, false},
// CHECK-NEXT: {1, 0, false},
// CHECK-NEXT: };
// CHECK: {Intrinsic::dx_dot4add_i8packed, [[PERMUTED_ARGS]], Intrinsic::ClangBuiltinPredicateKind::Any, 0, "hlsl.dot4add.i8packed"},

// CHECK: {Intrinsic::dx_nclamp, {{.*}}, Intrinsic::ClangBuiltinPredicateKind::Floating, 0, "hlsl.clamp"},
// CHECK: {Intrinsic::dx_sclamp, {{.*}}, Intrinsic::ClangBuiltinPredicateKind::SignedInteger, 0, "hlsl.clamp"},
// CHECK: {Intrinsic::dx_uclamp, {{.*}}, Intrinsic::ClangBuiltinPredicateKind::UnsignedInteger, 0, "hlsl.clamp"},

// CHECK: {Intrinsic::not_intrinsic, {{.*}}, Intrinsic::ClangBuiltinPredicateKind::Any, 0, ""},

// CHECK: {"dx", "__builtin_hlsl_elementwise_clamp", {{.*}}},
// CHECK: {"spv", "__builtin_hlsl_elementwise_clamp", {{.*}}},
// CHECK: {"__builtin_hlsl_resource_counterhandlefromheap", {{.*}}},

// CHECK-LABEL: Intrinsic::getClangBuiltinLowerings(StringRef TargetPrefix,
// CHECK: llvm::lower_bound(ClangBuiltinLoweringTable, Key)
// CHECK: llvm::lower_bound(ClangBuiltinFallbackTable, BuiltinName)