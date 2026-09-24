// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GL.FrexpStruct`/`spirv.GL.ModfStruct`/`spirv.GL.Modf`/
// `spirv.GL.Frexp` (GLSL.std.450's `FrexpStruct`/`ModfStruct`/`Modf`/`Frexp`,
// roadmap L120/L184) convert
// through this pass's own pipeline: an entry-point-less function is enough
// here, since these ops' own conversion patterns
// (`mlir::populateSPIRVToLLVMConversionPatterns`) don't depend on any of the
// entry-point/resource maps this pass otherwise threads through.
// `CompositeExtract`ing a single member (as CTS's own `frexp_st`/`modf_st`
// operations, and this dialect's deserializer, do) is included to mirror the
// shape those tests actually exercise for the struct-returning variants.

// CHECK-LABEL: llvm.func @frexp_st
// CHECK: %[[STRUCT:.*]] = llvm.intr.frexp(%arg0) : (f32) -> !llvm.struct<packed (f32, i32)>
// CHECK: %[[SIGNIFICAND:.*]] = llvm.extractvalue %[[STRUCT]][0] : !llvm.struct<packed (f32, i32)>
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
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<packed (f32, f32)>
// CHECK: %[[S0:.*]] = llvm.insertvalue %[[FRAC]], %[[POISON]][0] : !llvm.struct<packed (f32, f32)>
// CHECK: %[[S1:.*]] = llvm.insertvalue %[[INT]], %[[S0]][1] : !llvm.struct<packed (f32, f32)>
// CHECK: %[[RESULT:.*]] = llvm.extractvalue %[[S1]][1] : !llvm.struct<packed (f32, f32)>
// CHECK: llvm.return %[[RESULT]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @modf_st(%arg0 : f32) -> f32 "None" {
    %0 = spirv.GL.ModfStruct %arg0 : f32 -> !spirv.struct<(f32, f32)>
    %1 = spirv.CompositeExtract %0[1 : i32] : !spirv.struct<(f32, f32)>
    spirv.ReturnValue %1 : f32
  }
}

// -----

// `Modf` (unlike `ModfStruct`) writes its integer part through a genuine
// pointer operand rather than packing both parts into a struct result, so
// it converts the same way except the integer part is stored through the
// (already-converted, plain LLVM) pointer instead of being inserted into a
// struct.

// CHECK-LABEL: llvm.func @modf
// CHECK: %[[PTR:.*]] = llvm.alloca {{.*}} x f32
// CHECK: %[[INT:.*]] = llvm.intr.trunc(%arg0) : (f32) -> f32
// CHECK: %[[FRAC:.*]] = llvm.fsub %arg0, %[[INT]] : f32
// CHECK: llvm.store %[[INT]], %[[PTR]]
// CHECK: llvm.return %[[FRAC]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @modf(%arg0 : f32) -> f32 "None" {
    %ptr = spirv.Variable : !spirv.ptr<f32, Function>
    %0 = spirv.GL.Modf %arg0, %ptr : f32, !spirv.ptr<f32, Function> -> f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// `Frexp` (unlike `FrexpStruct`) writes its exponent through a genuine
// pointer operand rather than packing both parts into a struct result, so
// it converts the same way except the exponent is extracted out of the
// intrinsic's own tight struct result and stored through the
// (already-converted, plain LLVM) pointer instead of being returned as
// part of a struct.

// CHECK-LABEL: llvm.func @frexp
// CHECK: %[[PTR:.*]] = llvm.alloca {{.*}} x i32
// CHECK: %[[STRUCT:.*]] = llvm.intr.frexp(%arg0) : (f32) -> !llvm.struct<(f32, i32)>
// CHECK: %[[SIGNIFICAND:.*]] = llvm.extractvalue %[[STRUCT]][0] : !llvm.struct<(f32, i32)>
// CHECK: %[[EXPONENT:.*]] = llvm.extractvalue %[[STRUCT]][1] : !llvm.struct<(f32, i32)>
// CHECK: llvm.store %[[EXPONENT]], %[[PTR]]
// CHECK: llvm.return %[[SIGNIFICAND]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @frexp(%arg0 : f32) -> f32 "None" {
    %ptr = spirv.Variable : !spirv.ptr<i32, Function>
    %0 = spirv.GL.Frexp %arg0, %ptr : f32, !spirv.ptr<i32, Function> -> f32
    spirv.ReturnValue %0 : f32
  }
}

