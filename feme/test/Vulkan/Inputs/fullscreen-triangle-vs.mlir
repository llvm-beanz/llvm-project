// A vertex stage selecting one of three hard-coded oversized-triangle
// corners -- (-1,-1), (3,-1), (-1,3) -- from `gl_VertexIndex`, which after
// clipping exactly covers the [-1, 1] unit square. Written directly in the
// SPIR-V dialect (no GLSL/HLSL compiler is assumed available) and
// serialized ahead of time with `feme-translate --serialize-spirv`; used by
// feme/test/Vulkan/graphics-loader-smoke.test.
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
