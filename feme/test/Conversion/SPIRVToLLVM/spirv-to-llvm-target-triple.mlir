// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that feme::spirv::createConvertSPIRVToLLVMPass records the target
// triple (and the data layout that triple implies) each `spirv.module` was
// compiled for as `llvm` dialect module attributes, which
// `mlir::translateModuleToLLVMIR` then forwards onto the `llvm::Module` -- the
// prerequisite for naming target intrinsics, see the "SPIR-V -> MLIR `llvm`
// dialect -> LLVM IR" section of feme/docs/Design.md.

// CHECK-LABEL: module attributes
// CHECK-SAME: llvm.data_layout = "e-ve-i64:64-n8:16:32:64-G10"
// CHECK-SAME: llvm.target_triple = "spirv-unknown-vulkan-compute"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @compute_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @compute_entry
}

// -----

// CHECK: llvm.target_triple = "spirv-unknown-vulkan-pixel"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @pixel_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @pixel_entry
}

// -----

// CHECK: llvm.target_triple = "spirv-unknown-vulkan-vertex"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @vertex_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @vertex_entry
}

// -----

// A module with no entry point at all names no pipeline stage.

// CHECK: llvm.target_triple = "spirv-unknown-vulkan"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @not_an_entry_point() -> () "None" {
    spirv.Return
  }
}

// -----

// An OpenCL kernel is not a graphics pipeline stage: it gets the physical,
// bitness-carrying triple LLVM's SPIRV backend keys its `Kernel` environment
// off instead.

// CHECK: llvm.data_layout = "e-p:32:32-i64:64-
// CHECK-SAME: llvm.target_triple = "spirv32-unknown-unknown"
spirv.module Physical32 OpenCL requires #spirv.vce<v1.0, [Kernel, Addresses], []> {
  spirv.func @kernel_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Kernel" @kernel_entry
}
