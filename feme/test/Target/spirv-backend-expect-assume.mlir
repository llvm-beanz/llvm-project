// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o %t.ll
// RUN: feme-translate --llvm-backend --target-triple=spirv64-unknown-unknown \
// RUN:   -spirv-ext=+SPV_KHR_expect_assume \
// RUN:   %t.ll -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s
//
// REQUIRES: spirv-registered-target

// The same "null pipeline" round-trip as spirv-backend-null-pipeline.mlir,
// but for `spirv.KHR.AssumeTrue`/`spirv.KHR.Expect` (roadmap F4,
// `VK_KHR_shader_expect_assume`/`shaderExpectAssume`): rather than round-
// tripping back to unattributed arithmetic, the `llvm.assume`/`llvm.expect`
// intrinsics feme::spirv::ConvertSPIRVToLLVMPass (SPIRVToLLVMPatterns.cpp's
// AssumeTrueConversionPattern/ExpectConversionPattern) produced for them
// round-trip through LLVM's real, in-tree SPIRV backend back into
// `OpAssumeTrueKHR`/`OpExpectKHR` (see `SPIRVPrepareFunctions.cpp`'s
// `lowerExpectAssume`) -- concrete evidence this pipeline actually
// round-trips through the real backend, not merely that MLIR's own
// conversion pattern fires. Unlike every other capability this backend
// exercises, `SPV_KHR_expect_assume` is gated behind that backend's own
// `-spirv-ext` allow-list (empty by default, per `SPIRVSubtarget::
// initAvailableExtensions`) rather than implied by the module's target
// triple/version alone, so this test passes `-spirv-ext=+SPV_KHR_expect_
// assume` explicitly -- without it, the backend silently drops both
// intrinsics rather than lowering them (`lowerExpectAssume`'s own comment:
// "ignore the intrinsic ... removed later on by LLVM").

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ExpectAssumeKHR], [SPV_KHR_expect_assume]> {
  spirv.func @assume_expect(%cond: i1, %value: i32, %expected: i32) -> (i32) "None" {
    spirv.KHR.AssumeTrue %cond
    %0 = spirv.KHR.Expect %value, %expected : i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @assume_expect
  spirv.ExecutionMode @assume_expect "LocalSize", 1, 1, 1
}

// CHECK: spirv.func @assume_expect
// CHECK: spirv.KHR.AssumeTrue %{{.*}}
// CHECK: spirv.KHR.Expect %{{.*}}, %{{.*}} : i32
