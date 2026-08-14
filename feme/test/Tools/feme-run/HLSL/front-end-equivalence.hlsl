// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --groups=1,1,1 --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s
// RUN: feme-translate --no-implicit-module --serialize-spirv %t/shader.mlir -o %t/shader.spv
// RUN: feme-run --groups=1,1,1 --heap=%t/heap.yaml %t/shader.spv | FileCheck %s

// Front-end equivalence (roadmap step R10, closing §1.2's "SPIR-V shaders
// cannot execute" P0 and §2.2's item 3 in feme/docs/Roadmap.md): the same
// compute shader -- read `SV_DispatchThreadID`/`GlobalInvocationId`, convert
// to `float`, write to a bound `RWStructuredBuffer<float>` -- run through
// both front ends and both compared against the *same* `CHECK` lines, the
// test DXIL's own execution coverage has had since milestone 4 but SPIR-V's
// never has until `feme-run` could accept SPIR-V input at all (see
// `feme-run`'s own file comment: `feme::SPIRVImporter` +
// `feme::SPIRVToLLVMTranslator`, followed by
// `feme::cpu::SPIRVResourceLoweringPass`/`feme::cpu::SPIRVBuiltinFoldingPass`
// in the CPU pipeline itself).
//
// The DXIL half is real HLSL compiled by Clang's own HLSL front end and
// DirectX backend, exactly like every other test under this directory. A
// real SPIR-V counterpart compiled from the *same* `.hlsl` file is not yet
// possible: Clang's HLSL front end only targets SPIR-V through LLVM's
// in-tree SPIRV backend, which this build does not configure (see
// `directx-registered-target`'s absence of a `spirv-registered-target`
// counterpart above -- neither this test nor any existing SPIR-V one in
// this tree requires it, since none of them compile SPIR-V through that
// backend either). The SPIR-V half below is instead the same shader
// hand-written directly in the `spirv` dialect -- exactly how every other
// SPIR-V input in this tree is authored (see e.g.
// feme/test/Tools/feme/feme-spirv-compute-shader.mlir), assembled at test
// time with `feme-translate --serialize-spirv` rather than checked in as a
// binary fixture (see "Avoiding binary test fixtures" in
// feme/docs/Design.md) -- asserting the two independently-authored front
// ends agree on the answer, which is the property this test exists to
// cover.

// CHECK: binding[0:0][0]: 0 1065353216 1073741824 1077936128

//--- shader.hlsl
RWStructuredBuffer<float> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  Out[tid.x] = (float)tid.x;
}

//--- shader.mlir
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @tid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @Out bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %tid_addr = spirv.mlir.addressof @tid : !spirv.ptr<vector<3xi32>, Input>
    %tid_v = spirv.Load "Input" %tid_addr : vector<3xi32>
    %tid_x = spirv.CompositeExtract %tid_v[0 : i32] : vector<3xi32>
    %out_addr = spirv.mlir.addressof @Out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %elem = spirv.AccessChain %out_addr[%c0, %tid_x]
        : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    %tid_x_f = spirv.ConvertUToF %tid_x : i32 to f32
    spirv.Store "StorageBuffer" %elem, %tid_x_f : f32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @tid, @Out
  spirv.ExecutionMode @main "LocalSize", 4, 1, 1
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
