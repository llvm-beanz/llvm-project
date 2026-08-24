// RUN: not feme-opt --feme-convert-spirv-to-llvm %s 2>&1 | FileCheck %s

// Roadmap E29: a `bool` (`i1`, SPIR-V's own `OpTypeBool` representation --
// see `WorkgroupGlobalVariablePattern`'s `containsAddressableBool` helper in
// SPIRVToLLVMPatterns.cpp) member of a `Workgroup`-storage struct is not yet
// byte-addressable, so this fails to legalize cleanly (a diagnosed error,
// not a crash) instead of reaching `llvm.getelementptr`/LLVM IR translation
// at all -- previously an `llvm::GetElementPtrTypeIterator.h` assertion
// ("Not byte-addressable"), reproduced on
// dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.composites.*
// (whose per-case struct mixes a `bool`/`bvec2`/`bvec3`/`bvec4` field in
// with real scalars).

// CHECK: error: failed to legalize operation 'spirv.GlobalVariable'
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader], []> {
  spirv.GlobalVariable @wg : !spirv.ptr<!spirv.struct<(i32, i1)>, Workgroup>
  spirv.func @unused() "None" {
    spirv.Return
  }
}
