// A fragment stage writing opaque solid red to SV_Target0 (location 0).
// Used by the depth-test, stencil-test, and multisample-resolve
// differential scenarios in feme/test/Vulkan/graphics-lavapipe-diff.test
// (see "V6: Graphics queue and basic rendering" in
// feme/docs/FeMeVulkanDesign.md).
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
