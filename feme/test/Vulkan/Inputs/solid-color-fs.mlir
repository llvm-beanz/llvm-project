// A fragment stage writing an opaque half-green color to SV_Target0
// (location 0). Paired with fullscreen-triangle-vs.mlir by
// feme/test/Vulkan/graphics-loader-smoke.test; the 0.5 component checks
// that the `R8G8B8A8_UNORM` output-merge conversion runs, not just that
// bytes are copied.
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[0.0, 0.5, 0.0, 1.0]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
