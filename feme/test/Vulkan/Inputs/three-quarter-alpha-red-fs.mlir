// A fragment stage writing three-quarter-alpha red to SV_Target0
// (location 0), for the blending differential scenario in
// feme/test/Vulkan/graphics-lavapipe-diff.test. Deliberately not 0.5: an
// exact half-alpha blend against a half-covered background lands each
// channel exactly on an 8-bit unorm quantization tie (127.5), where two
// independent renderers may round either way without either being wrong --
// not a useful thing to diff (see "V6: Graphics queue and basic rendering"
// in feme/docs/FeMeVulkanDesign.md).
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[1.0, 0.0, 0.0, 0.75]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
