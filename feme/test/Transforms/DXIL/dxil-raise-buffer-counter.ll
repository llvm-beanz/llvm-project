; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s --check-prefix=RAISE
; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,feme-cpu-normalize-bound-resources,feme-cpu-lower-resources -S %s | FileCheck %s --check-prefix=LOWER

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { ptr }

; RAISE-LABEL: define i32 @counter(
; RAISE: call i32 @llvm.dx.resource.updatecounter{{.*}}(target("dx.RawBuffer", {{.*}}) %{{.*}}, i8 1)
; RAISE: call i32 @llvm.dx.resource.updatecounter{{.*}}(target("dx.RawBuffer", {{.*}}) %{{.*}}, i8 -1)
; LOWER-LABEL: define i32 @counter(
; LOWER: call i32 @feme.cpu.resource.atomic.updatecounter.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i32 1, i1 true)
; LOWER: call i32 @feme.cpu.resource.atomic.updatecounter.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i32 -1, i1 true)
define i32 @counter() {
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  %inc = call i32 @dx.op.bufferUpdateCounter(i32 70, %dx.types.Handle %h, i8 1)
  %dec = call i32 @dx.op.bufferUpdateCounter(i32 70, %dx.types.Handle %h, i8 -1)
  %sum = add i32 %inc, %dec
  ret i32 %sum
}

; RAISE-LABEL: define i32 @invalid_delta(
; RAISE: call i32 @dx.op.bufferUpdateCounter(i32 70, {{.*}}, i8 0)
define i32 @invalid_delta() {
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  %result = call i32 @dx.op.bufferUpdateCounter(i32 70, %dx.types.Handle %h, i8 0)
  ret i32 %result
}

declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
declare i32 @dx.op.bufferUpdateCounter(i32, %dx.types.Handle, i8)

!dx.resources = !{!0}
!0 = !{null, !1, null, null}
!1 = !{!2}
!2 = !{i32 0, ptr undef, !"counter", i32 0, i32 0, i32 1, i32 12, i1 false, i1 true, i1 false, !3}
!3 = !{i32 1, i32 4}
