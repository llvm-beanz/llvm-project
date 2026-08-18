// A `void main()` that reads `in[gl_GlobalInvocationID.x]`, adds one, and
// writes the result to `out[gl_GlobalInvocationID.x]` -- two flat
// (non-aggregate) `i32` `StorageBuffer` bindings in one descriptor set,
// matching feme/unittests/Vulkan/CommandBufferTest.cpp's
// StorageBufferDispatchTest. Used by
// feme/test/Vulkan/storage-buffer-lavapipe-diff.test, serialized ahead of
// time with `feme-translate --serialize-spirv` so the same SPIR-V bytes run
// against every ICD under test.
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @in bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %idx = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    %2 = spirv.mlir.addressof @in : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac_in = spirv.AccessChain %2[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac_in : i32
    %c1 = spirv.Constant 1 : i32
    %v2 = spirv.IAdd %v, %c1 : i32
    %3 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %ac_out = spirv.AccessChain %3[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac_out, %v2 : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid, @in, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
