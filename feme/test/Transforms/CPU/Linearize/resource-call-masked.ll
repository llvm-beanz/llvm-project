; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A canonical `feme.cpu.resource.*` call under a divergent branch's arm gets
; its mask operand (always `true` as `feme::cpu::ResourceLoweringPass` left
; it) rewritten to the block's actual mask, so it never touches memory on
; behalf of an invocation that did not take this path. See "Canonical
; resource calls are similarly rewritten to masked forms" in "Phase 3:
; Linearization and Predication" in feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: %mask.t = and i1 true, %c
; CHECK: t:
; CHECK: call float @feme.cpu.resource.load.raw.f32(ptr %heap, i32 %heap_count, i32 %desc, i64 %off, i1 %mask.t)
define void @main(ptr %heap, i32 %heap_count, i32 %desc) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %off = sext i32 %tid to i64
  %loaded = call float @feme.cpu.resource.load.raw.f32(ptr %heap, i32 %heap_count, i32 %desc, i64 %off, i1 true)
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
