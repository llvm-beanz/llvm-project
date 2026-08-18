; RUN: feme-opt --llvm -passes=feme-cpu-normalize-bound-resources -S %s | FileCheck %s

; Covers feme::cpu::BoundResourceNormalizationPass's basic case (see
; "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md): a
; register-bound typed-buffer handle (`register(u0, space0)`, a 4-element
; array) is rewritten into a range-checked `handlefromheap` access into the
; reserved heap prefix, keeping the same load/store accesses through it
; unmodified -- `feme::cpu::ResourceLoweringPass` (which runs next in the
; pipeline) never gains a bound-resource case of its own.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK: [[OOR:%.*]] = icmp uge i32 %idx, 4
; CHECK: [[EXT:%.*]] = zext i32 %idx to i64
; CHECK: [[SUM:%.*]] = add i64 0, [[EXT]]
; CHECK: [[OVF:%.*]] = icmp ugt i64 [[SUM]], 4294967295
; CHECK: [[TRUNC:%.*]] = trunc i64 [[SUM]] to i32
; CHECK: [[CLAMPED:%.*]] = select i1 [[OVF]], i32 -1, i32 [[TRUNC]]
; CHECK: [[IDX:%.*]] = select i1 [[OOR]], i32 -1, i32 [[CLAMPED]]
; CHECK: call target("dx.TypedBuffer", <4 x float>, 1, 0, 0) @llvm.dx.resource.handlefromheap.{{.*}}(i32 [[IDX]], i1 false)
define void @main(i32 %idx) {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32 0, i32 0, i32 4, i32 %idx, ptr null)
  %loaded = call {<4 x float>, i1}
      @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(
          target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx)
  %val = extractvalue {<4 x float>, i1} %loaded, 0
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx, <4 x float> %val)
  ret void
}

; CHECK-NOT: @llvm.dx.resource.handlefrombinding

; The reserved-prefix size (4, the range's own size) and the one accepted
; range (space 0, register 0, range size 4, heap base 0) are published for
; `feme::cpu::ResourceInfo` to read (see "Bound-resource normalization").
; CHECK: !feme.cpu.bound_resources = !{![[MD:[0-9]+]]}
; CHECK: ![[MD]] = !{!"main", i32 4, i32 0, i32 0, i32 0, i32 0, i32 4, i32 0, i32 0}

declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
    @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32, i32, i32, i32, ptr)
declare {<4 x float>, i1}
    @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32)
declare void
    @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32, <4 x float>)
