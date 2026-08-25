// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GL.FrexpStruct`/`spirv.GL.ModfStruct` (GLSL.std.450's
// `FrexpStruct`/`ModfStruct`, roadmap F16) convert through this pass's own
// pipeline: an entry-point-less function is enough here, since these two
// ops' own conversion patterns (`mlir::populateSPIRVToLLVMConversionPatterns`)
// don't depend on any of the entry-point/resource maps this pass otherwise
// threads through. `CompositeExtract`ing a single member (as CTS's own
// `frexp_st`/`modf_st` operations, and this dialect's deserializer, do) is
// included to mirror the shape those tests actually exercise.

// CHECK-LABEL: llvm.func @frexp_st
// CHECK: %[[STRUCT:.*]] = llvm.intr.frexp(%arg0) : (f32) -> !llvm.struct<(f32, i32)>
// CHECK: %[[SIGNIFICAND:.*]] = llvm.extractvalue %[[STRUCT]][0] : !llvm.struct<(f32, i32)>
// CHECK: llvm.return %[[SIGNIFICAND]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @frexp_st(%arg0 : f32) -> f32 "None" {
    %0 = spirv.GL.FrexpStruct %arg0 : f32 -> !spirv.struct<(f32, i32)>
    %1 = spirv.CompositeExtract %0[0 : i32] : !spirv.struct<(f32, i32)>
    spirv.ReturnValue %1 : f32
  }
}

// -----

// ModfStruct has no matching LLVM intrinsic (unlike FrexpStruct, which maps
// directly onto `llvm.intr.frexp`), so it decomposes into a truncation (the
// integer part) and a subtraction (the fractional part), matching GLSL.std.450's
// own "x = i + fraction" definition.

// CHECK-LABEL: llvm.func @modf_st
// CHECK: %[[INT:.*]] = llvm.intr.trunc(%arg0) : (f32) -> f32
// CHECK: %[[FRAC:.*]] = llvm.fsub %arg0, %[[INT]] : f32
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<(f32, f32)>
// CHECK: %[[S0:.*]] = llvm.insertvalue %[[FRAC]], %[[POISON]][0] : !llvm.struct<(f32, f32)>
// CHECK: %[[S1:.*]] = llvm.insertvalue %[[INT]], %[[S0]][1] : !llvm.struct<(f32, f32)>
// CHECK: %[[RESULT:.*]] = llvm.extractvalue %[[S1]][1] : !llvm.struct<(f32, f32)>
// CHECK: llvm.return %[[RESULT]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @modf_st(%arg0 : f32) -> f32 "None" {
    %0 = spirv.GL.ModfStruct %arg0 : f32 -> !spirv.struct<(f32, f32)>
    %1 = spirv.CompositeExtract %0[1 : i32] : !spirv.struct<(f32, f32)>
    spirv.ReturnValue %1 : f32
  }
}
