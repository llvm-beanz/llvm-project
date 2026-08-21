// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `Workgroup`-storage-class variable -- a GLSL `shared`/HLSL
// `groupshared` variable declared directly in SPIR-V -- becomes an ordinary
// `llvm.mlir.global` in address space 3, the same convention Clang's own
// HLSL `groupshared` codegen uses (`LangAS::hlsl_groupshared`), and that a
// plain `spirv.AccessChain`/`spirv.Load`/`spirv.Store` through it converts
// with MLIR's own generic access-chain/load/store patterns -- neither needs
// a FeMe-specific pattern of its own, since the pointer's address space is
// the only storage-class-specific detail either one depends on (roadmap
// milestone E13).
//
// CHECK: llvm.mlir.global external @wg() {addr_space = 3 : i32} : !llvm.array<4 x i32>
// CHECK-LABEL: llvm.func @write_wg
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @wg : !llvm.ptr<3>
// CHECK: %[[ELEM:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, %{{.*}}]
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : i32, !llvm.ptr<3>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader], []> {
  spirv.GlobalVariable @wg : !spirv.ptr<!spirv.array<4 x i32>, Workgroup>
  spirv.func @write_wg(%idx : i32, %val : i32) "None" {
    %0 = spirv.mlir.addressof @wg : !spirv.ptr<!spirv.array<4 x i32>, Workgroup>
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<4 x i32>, Workgroup>, i32 -> !spirv.ptr<i32, Workgroup>
    spirv.Store "Workgroup" %ac, %val : i32
    spirv.Return
  }
}

// -----

// `zero_initializer` (`VK_KHR_zero_initialize_workgroup_memory`, roadmap
// milestone E13) becomes the LLVM global's own `#llvm.zero` initializer,
// which is what `feme::cpu::GroupSharedLayout::NeedsZeroInit` reads back off
// this global to decide whether to zero this group's groupshared buffer
// before each dispatch (see GroupShared.h/EntryWrapper.cpp).
// CHECK: llvm.mlir.global internal @wg(#llvm.zero) {addr_space = 3 : i32} : !llvm.array<4 x i32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader], []> {
  spirv.GlobalVariable @wg zero_initializer : !spirv.ptr<!spirv.array<4 x i32>, Workgroup>
  spirv.func @unused() "None" {
    spirv.Return
  }
}
