; RUN: feme-opt --llvm -passes=feme-cpu-lower-root-constants -S %s | FileCheck %s

; A `dx.CBuffer` bound anywhere other than `(b0, space0)` is left entirely
; alone, for `feme::cpu::checkSupportedRaisedOps` to reject as an ordinary
; register-bound handle (see the header comment). Declared first: a
; lowered function is rebuilt (its signature grows) and the replacement is
; appended at the module's end, so `@main` below prints after this one.
; CHECK-LABEL: define void @other_binding(
; CHECK-NOT: root_constants
; CHECK: call target("dx.CBuffer", {{.*}}) @llvm.dx.resource.handlefrombinding
define void @other_binding() {
  %h = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a16i8t(i32 0, i32 1, i32 1, i32 0, ptr null)
  %row = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a16i8t(
      target("dx.CBuffer", [16 x i8]) %h, i32 0)
  ret void
}
declare target("dx.CBuffer", [16 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a16i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a16i8t(
    target("dx.CBuffer", [16 x i8]), i32)

; The one recognized root-constant binding (`(b0, space0)`, see
; feme/include/feme/Transforms/CPU/RootConstantLowering.h) lowers a
; constant-row `cbufferrow.4` load into a bounds-checked load from the
; appended `root_constants` parameter -- zero for any row wholly or partly
; outside `root_constant_size`'s span, matching "Root constants" in
; feme/docs/FeMeCPUDesign.md. Row 1 reads bytes [16, 32), so
; `root_constant_size` must be at least 32 for the real load to run.

; CHECK-LABEL: define i32 @main(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK: %root_const.inbounds = icmp uge i32 %root_constant_size, 32
; CHECK: br i1 %root_const.inbounds
; CHECK: getelementptr inbounds i8, ptr %root_constants, i64 16
; CHECK: load i32, ptr {{.*}}, align 4
; CHECK: getelementptr inbounds i8, ptr %root_constants, i64 20
; CHECK: getelementptr inbounds i8, ptr %root_constants, i64 24
; CHECK: getelementptr inbounds i8, ptr %root_constants, i64 28
; CHECK: phi i32 [ {{.*}}, %{{.*}} ], [ 0, %{{.*}} ]
define i32 @main() #0 {
  %h = call target("dx.CBuffer", [32 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a32i8t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %row = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a32i8t(
      target("dx.CBuffer", [32 x i8]) %h, i32 1)
  %v0 = extractvalue {i32, i32, i32, i32} %row, 0
  ret i32 %v0
}
declare target("dx.CBuffer", [32 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a32i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a32i8t(
    target("dx.CBuffer", [32 x i8]), i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
