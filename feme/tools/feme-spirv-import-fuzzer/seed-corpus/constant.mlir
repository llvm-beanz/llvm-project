// Source text for seed-corpus/constant.spv (see ../seed-corpus in
// feme-spirv-import-fuzzer.md for regeneration instructions). Adds a global
// variable and a constant store, giving the fuzzer a second, structurally
// distinct seed to mutate from.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gv : !spirv.ptr<f32, Private>
  spirv.func @bar() -> () "None" {
    %0 = spirv.Constant 1.0 : f32
    %1 = spirv.mlir.addressof @gv : !spirv.ptr<f32, Private>
    spirv.Store "Private" %1, %0 : f32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @bar
  spirv.ExecutionMode @bar "LocalSize", 1, 1, 1
}
