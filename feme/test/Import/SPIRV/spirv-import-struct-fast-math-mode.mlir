// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | FileCheck %s

// Regression test for roadmap F16: a struct type (here, the result of
// `spirv.GL.FrexpStruct`, matching CTS's own `frexp_st` operation and its
// `dEQP-VK.spirv_assembly.instruction.compute.float_controls2.fp32.
// input_args.frexp_st_*` shaders) decorated directly with `FPFastMathMode`
// (rather than one of its members, or a value an op produces) used to
// crash `mlir::spirv::Deserializer::processStructType` on
// `assert(decoration.has_value())`: guessing the decoration back from its
// mangled attribute name conflated "FPFastMathMode" with the nonexistent
// "FpFastMathMode". feme::SPIRVImporter shares this deserializer with
// upstream MLIR (`mlir::spirv::deserialize`), so the crash was reachable
// from feme's own import path, not just `mlir-translate`.

spirv.module Logical OpenCL requires #spirv.vce<v1.0, [Kernel, Linkage], []> {
  spirv.func @frexp_st(%arg0 : f32) -> f32 "None" {
    %0 = spirv.GL.FrexpStruct %arg0 : f32 -> !spirv.struct<(f32, i32), FPFastMathMode=#spirv.fastmath_mode<NotNaN>>
    %1 = spirv.CompositeExtract %0[0 : i32] : !spirv.struct<(f32, i32), FPFastMathMode=#spirv.fastmath_mode<NotNaN>>
    spirv.ReturnValue %1 : f32
  }
}

// CHECK: spirv.GL.FrexpStruct {{%.*}} : f32 -> !spirv.struct<(f32, i32), FPFastMathMode=#spirv.fastmath_mode<NotNaN>>
