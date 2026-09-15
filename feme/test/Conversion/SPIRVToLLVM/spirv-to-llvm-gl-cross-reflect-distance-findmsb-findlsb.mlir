// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H124j: `spirv.GL.Cross`/`spirv.GL.Reflect`/`spirv.GL.Distance`/
// `spirv.GL.FindUMsb`/`spirv.GL.FindSMsb`/`spirv.GL.FindILsb` had no
// conversion pattern at all before this fix -- the same "no pattern at
// all" gap H124f fixed for `spirv.GL.Normalize`/`spirv.GL.Length`/
// `spirv.IsNan`/`spirv.IsInf` (see
// spirv-to-llvm-gl-length-normalize-isnan-isinf.mlir), for a different
// GLSL.std.450 op subset that fix did not cover. Every HLSL-derived shape
// reaching one of these six ops (e.g. `cross`/`reflect`/`distance`/
// `firstbithigh`/`firstbitlow` HLSL intrinsics) failed pipeline creation
// with "failed to legalize operation ... that was explicitly marked
// illegal".

// `Cross(x, y)` builds a fixed 3-component result one lane at a time via
// extractelement/insertelement, per the GLSL.std.450 spec's own
// component-wise definition.
// CHECK-LABEL: llvm.func @cross_vector
// CHECK: %[[X1:.*]] = llvm.extractelement %arg0{{.*}} : vector<3xf32>
// CHECK: %[[Y2:.*]] = llvm.extractelement %arg1{{.*}} : vector<3xf32>
// CHECK: %[[X1Y2:.*]] = llvm.fmul %[[X1]], %[[Y2]] : f32
// CHECK: %[[Y1:.*]] = llvm.extractelement %arg1{{.*}} : vector<3xf32>
// CHECK: %[[X2:.*]] = llvm.extractelement %arg0{{.*}} : vector<3xf32>
// CHECK: %[[Y1X2:.*]] = llvm.fmul %[[Y1]], %[[X2]] : f32
// CHECK: %[[LANE0:.*]] = llvm.fsub %[[X1Y2]], %[[Y1X2]] : f32
// CHECK: llvm.insertelement %[[LANE0]]
// CHECK: llvm.insertelement
// CHECK: %[[RES:.*]] = llvm.insertelement
// CHECK: llvm.return %[[RES]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @cross_vector(%x: vector<3xf32>, %y: vector<3xf32>) -> (vector<3xf32>) "None" {
    %0 = spirv.GL.Cross %x, %y : vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.EntryPoint "GLCompute" @cross_vector
  spirv.ExecutionMode @cross_vector "LocalSize", 1, 1, 1
}

// -----

// `Reflect(i, n)` lowers to `i - 2 * dot(n, i) * n`.
// CHECK-LABEL: llvm.func @reflect_vector
// CHECK: llvm.intr.fmuladd
// CHECK: %[[DOT:.*]] = llvm.intr.fmuladd
// CHECK: %[[TWO:.*]] = llvm.mlir.constant(2.000000e+00 : f32) : f32
// CHECK: %[[TWODOT:.*]] = llvm.fmul %[[TWO]], %[[DOT]] : f32
// CHECK: %[[TWODOTLIKE:.*]] = llvm.shufflevector {{.*}} : vector<3xf32>
// CHECK: %[[TWODOTN:.*]] = llvm.fmul %[[TWODOTLIKE]], %arg1 : vector<3xf32>
// CHECK: %[[RES:.*]] = llvm.fsub %arg0, %[[TWODOTN]] : vector<3xf32>
// CHECK: llvm.return %[[RES]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @reflect_vector(%i: vector<3xf32>, %n: vector<3xf32>) -> (vector<3xf32>) "None" {
    %0 = spirv.GL.Reflect %i, %n : vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.EntryPoint "GLCompute" @reflect_vector
  spirv.ExecutionMode @reflect_vector "LocalSize", 1, 1, 1
}

// -----

// `Distance(p0, p1)` lowers to `Length(p0 - p1)`, i.e. `sqrt(dot(d, d))`
// where `d = p0 - p1`.
// CHECK-LABEL: llvm.func @distance_vector
// CHECK: %[[DIFF:.*]] = llvm.fsub %arg0, %arg1 : vector<3xf32>
// CHECK: llvm.intr.fmuladd
// CHECK: %[[DOT:.*]] = llvm.intr.fmuladd
// CHECK: %[[RES:.*]] = llvm.intr.sqrt(%[[DOT]]) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @distance_vector(%p0: vector<3xf32>, %p1: vector<3xf32>) -> (f32) "None" {
    %0 = spirv.GL.Distance %p0, %p1 : vector<3xf32>, vector<3xf32> -> f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @distance_vector
  spirv.ExecutionMode @distance_vector "LocalSize", 1, 1, 1
}

// -----

// `FindUMsb(x)` lowers to `31 - llvm.ctlz(x, is_zero_poison=false)`: for a
// zero operand, `is_zero_poison=false` makes `ctlz` return 32 (the full
// width) rather than being undefined, so `31 - 32 = -1` already matches
// the spec's own "if Value is 0, the result is -1" with no separate
// select.
// CHECK-LABEL: llvm.func @find_umsb_scalar
// CHECK: %[[CLZ:.*]] = "llvm.intr.ctlz"(%arg0) <{is_zero_poison = false}> : (i32) -> i32
// CHECK: %[[THIRTYONE:.*]] = llvm.mlir.constant(31 : i32) : i32
// CHECK: %[[RES:.*]] = llvm.sub %[[THIRTYONE]], %[[CLZ]] : i32
// CHECK: llvm.return %[[RES]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @find_umsb_scalar(%x: i32) -> (i32) "None" {
    %0 = spirv.GL.FindUMsb %x : i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @find_umsb_scalar
  spirv.ExecutionMode @find_umsb_scalar "LocalSize", 1, 1, 1
}

// -----

// `FindSMsb(x)` (HLSL `firstbithigh` on a *signed* operand) lowers to the
// same `31 - ctlz` computation `FindUMsb` above uses, but applied to a
// sign-normalized `y = x XOR AShr(x, 31)` first: `y` equals `x` unchanged
// for a non-negative operand, or `~x` for a negative one, which turns
// "first 0-bit from the top of a negative number" into "first 1-bit from
// the top of its complement" -- reusing this same identity also
// reproduces the spec's two zero-result cases (`x == 0` and `x == -1`)
// for free, both mapping to `y == 0` and so `31 - ctlz(0) = -1`, with no
// separate select.
// CHECK-LABEL: llvm.func @find_smsb_scalar
// CHECK: %[[THIRTYONE:.*]] = llvm.mlir.constant(31 : i32) : i32
// CHECK: %[[SIGNMASK:.*]] = llvm.ashr %arg0, %[[THIRTYONE]] : i32
// CHECK: %[[Y:.*]] = llvm.xor %arg0, %[[SIGNMASK]] : i32
// CHECK: %[[CLZ:.*]] = "llvm.intr.ctlz"(%[[Y]]) <{is_zero_poison = false}> : (i32) -> i32
// CHECK: %[[RES:.*]] = llvm.sub %[[THIRTYONE]], %[[CLZ]] : i32
// CHECK: llvm.return %[[RES]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @find_smsb_scalar(%x: i32) -> (i32) "None" {
    %0 = spirv.GL.FindSMsb %x : i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @find_smsb_scalar
  spirv.ExecutionMode @find_smsb_scalar "LocalSize", 1, 1, 1
}

// -----

// `FindILsb(x)` lowers to `llvm.cttz(x, is_zero_poison=false)`, with an
// explicit `x == 0` select to `-1` -- unlike `FindUMsb` above, `cttz`'s
// own zero-input width result (32) has no equivalent one-subtraction
// identity landing on -1, so this needs the explicit compare-and-select.
// CHECK-LABEL: llvm.func @find_ilsb_scalar
// CHECK: %[[CTZ:.*]] = "llvm.intr.cttz"(%arg0) <{is_zero_poison = false}> : (i32) -> i32
// CHECK-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK-DAG: %[[NEGONE:.*]] = llvm.mlir.constant(-1 : i32) : i32
// CHECK: %[[ISZERO:.*]] = llvm.icmp "eq" %arg0, %[[ZERO]] : i32
// CHECK: %[[RES:.*]] = llvm.select %[[ISZERO]], %[[NEGONE]], %[[CTZ]] : i1, i32
// CHECK: llvm.return %[[RES]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @find_ilsb_scalar(%x: i32) -> (i32) "None" {
    %0 = spirv.GL.FindILsb %x : i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @find_ilsb_scalar
  spirv.ExecutionMode @find_ilsb_scalar "LocalSize", 1, 1, 1
}
