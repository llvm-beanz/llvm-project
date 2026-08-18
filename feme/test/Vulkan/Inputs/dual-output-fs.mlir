// A fragment stage writing solid red to SV_Target0 (location 0) and solid
// green to SV_Target1 (location 1), for the multiple-render-target
// differential scenario in feme/test/Vulkan/graphics-lavapipe-diff.test
// (see "V6: Graphics queue and basic rendering" in
// feme/docs/FeMeVulkanDesign.md).
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @color1 {location = 1 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c0 = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %c1 = spirv.Constant dense<[0.0, 1.0, 0.0, 1.0]> : vector<4xf32>
    %p0 = spirv.mlir.addressof @color0 : !spirv.ptr<vector<4xf32>, Output>
    %p1 = spirv.mlir.addressof @color1 : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p0, %c0 : vector<4xf32>
    spirv.Store "Output" %p1, %c1 : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color0, @color1
  spirv.ExecutionMode @main "OriginUpperLeft"
}
