; RUN: feme-opt --llvm -passes=feme-cpu-lower-root-constants -S %s | FileCheck %s

; Roadmap R25: two distinct `dx.CBuffer` bindings in the same function
; remain ambiguous -- there is still only one root-constant block -- and
; are left entirely alone, for `feme::cpu::checkSupportedRaisedOps` to
; reject as ordinary register-bound handles (see the header comment).
; Declared first: a lowered function is rebuilt (its signature grows) and
; the replacement is appended at the module's end, so `@main` below prints
; after every function declared ahead of it.
; CHECK-LABEL: define void @two_bindings(
; CHECK-NOT: root_constants
; CHECK: call target("dx.CBuffer", {{.*}}) @llvm.dx.resource.handlefrombinding
; CHECK: call target("dx.CBuffer", {{.*}}) @llvm.dx.resource.handlefrombinding
define void @two_bindings() {
  %h0 = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a16i8t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %h1 = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a16i8t(i32 0, i32 1, i32 1, i32 0, ptr null)
  %v0 = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a16i8t(
      target("dx.CBuffer", [16 x i8]) %h0, i32 0)
  %v1 = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a16i8t(
      target("dx.CBuffer", [16 x i8]) %h1, i32 0)
  ret void
}
declare target("dx.CBuffer", [16 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a16i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_a16i8t(
    target("dx.CBuffer", [16 x i8]), i32)

; Roadmap R25: any single binding is recognized now, not just the default
; `(b0, space0)` -- here `(b1, space0)`. The metadata this function's
; access is recorded under (see the end of this file) reports which one.
; CHECK-LABEL: define i32 @other_binding(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK: %root_const.inbounds = icmp uge i32 %root_constant_size, 16
define i32 @other_binding() {
  %h = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_b1_a16i8t(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_b1_a16i8t(
      target("dx.CBuffer", [16 x i8]) %h, i32 0)
  %v0 = extractvalue {i32, i32, i32, i32} %r, 0
  ret i32 %v0
}
declare target("dx.CBuffer", [16 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_b1_a16i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_b1_a16i8t(
    target("dx.CBuffer", [16 x i8]), i32)

; Roadmap R25: an array binding (`RangeSize == 4` here) with a dynamic
; array index is accepted; the required root-constant span is the full
; advertised size (`ElementSize * RangeSize`, 16 * 4 == 64), not merely
; the one row this function's own load happens to touch.
; CHECK-LABEL: define i32 @array_binding(
; CHECK-SAME: i32 %idx, ptr %root_constants, i32 %root_constant_size)
; CHECK: %root_const.elem_off = mul i32 %idx, 16
; CHECK: %root_const.base_off = add i32 %root_const.elem_off, 0
; CHECK: [[END:%.*]] = add i32 %root_const.base_off, 16
; CHECK: %root_const.inbounds = icmp uge i32 %root_constant_size, [[END]]
define i32 @array_binding(i32 %idx) {
  %h = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_arr_a16i8t(i32 0, i32 2, i32 4, i32 %idx, ptr null)
  %r = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_arr_a16i8t(
      target("dx.CBuffer", [16 x i8]) %h, i32 0)
  %v0 = extractvalue {i32, i32, i32, i32} %r, 0
  ret i32 %v0
}
declare target("dx.CBuffer", [16 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_arr_a16i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_arr_a16i8t(
    target("dx.CBuffer", [16 x i8]), i32)

; Roadmap R25: a dynamic (non-constant) row index is accepted, computing
; the byte offset at runtime instead of at compile time.
; CHECK-LABEL: define i32 @dynamic_row(
; CHECK-SAME: i32 %row, ptr %root_constants, i32 %root_constant_size)
; CHECK: %root_const.row_off = mul i32 %row, 16
; CHECK: %root_const.base_off = add i32 0, %root_const.row_off
define i32 @dynamic_row(i32 %row) {
  %h = call target("dx.CBuffer", [32 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_dyn_a32i8t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_dyn_a32i8t(
      target("dx.CBuffer", [32 x i8]) %h, i32 %row)
  %v0 = extractvalue {i32, i32, i32, i32} %r, 0
  ret i32 %v0
}
declare target("dx.CBuffer", [32 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_dyn_a32i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_dyn_a32i8t(
    target("dx.CBuffer", [32 x i8]), i32)

; The default `(b0, space0)` binding with a constant row index (roadmap
; R12's original shape) lowers a constant-row `cbufferrow.4` load into a
; bounds-checked load from the appended `root_constants` parameter -- zero
; for any row wholly or partly outside `root_constant_size`'s span,
; matching "Root constants" in feme/docs/FeMeCPUDesign.md. Row 1 reads
; bytes [16, 32), so `root_constant_size` must be at least 32 for the real
; load to run.

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

; Roadmap H7o: the metadata attached to the original function (here a
; stand-in for `!feme.signature`, which a later pass like
; `FragmentWrapperPass` requires to resolve stage-IO element IDs) must
; survive the `Function::Create` replacement this pass performs to append
; the trailing `root_constants`/`root_constant_size` parameters --
; `addRootConstantParams` previously dropped every function-attached
; metadata node entirely, since `GlobalObject::copyAttributesFrom()` does
; not copy it.
; CHECK-LABEL: define i32 @keeps_metadata(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK-SAME: !feme.fake_signature ![[FAKE_MD:[0-9]+]]
define i32 @keeps_metadata() !feme.fake_signature !10 {
  %h = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_md_a16i8t(i32 0, i32 3, i32 1, i32 0, ptr null)
  %r = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_md_a16i8t(
      target("dx.CBuffer", [16 x i8]) %h, i32 0)
  %v0 = extractvalue {i32, i32, i32, i32} %r, 0
  ret i32 %v0
}
declare target("dx.CBuffer", [16 x i8])
    @llvm.dx.resource.handlefrombinding.tdx.CBuffer_md_a16i8t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_md_a16i8t(
    target("dx.CBuffer", [16 x i8]), i32)
!10 = !{!"keeps_metadata_marker"}

; Every lowered function's binding is recorded in the `!feme.cpu.resources`
; metadata "Resource usage discovery" describes: `other_binding` at
; `(space0, b1)`, `array_binding` at `(space0, b2)` with its full
; advertised 64-byte span, `dynamic_row` at `(space0, b0)` with its full
; 32-byte span, and `main` at the default `(space0, b0)`.
; CHECK: !0 = !{!"other_binding", i32 16, i1 false, i32 0, i32 1, i32 0}
; CHECK: !1 = !{!"array_binding", i32 64, i1 false, i32 0, i32 2, i32 0}
; CHECK: !2 = !{!"dynamic_row", i32 32, i1 false, i32 0, i32 0, i32 0}
; CHECK: !3 = !{!"main", i32 32, i1 false, i32 0, i32 0, i32 0}
; CHECK: !4 = !{!"keeps_metadata", i32 16, i1 false, i32 0, i32 3, i32 0}
; CHECK: ![[FAKE_MD]] = !{!"keeps_metadata_marker"}
