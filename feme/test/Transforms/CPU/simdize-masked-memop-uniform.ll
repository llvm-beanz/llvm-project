; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A `feme.cpu.masked.load`/`.store` pair at a uniform (same-every-lane)
; address still widens to `llvm.masked.gather`/`.scatter`, broadcasting the
; single pointer into a `<W x ptr>` vector of identical addresses: correct
; regardless of the address's own uniformity (see the deviation note this
; milestone adds to feme/docs/FeMeCPUDesign.md -- the design's finer
; per-case lowering, e.g. one guarded scalar load broadcast for a
; wave-invariant uniform address, is deferred performance work).

; CHECK-LABEL: define void @main(
; CHECK: t:
; CHECK: %masked.mask = and <4 x i1> %wave_entry_mask, %live.t.wide
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> align 4 %p.splat.splat, <4 x i1> %masked.mask, <4 x i32> zeroinitializer)
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> %{{.*}}, <4 x ptr> align 4 %p.splat.splat, <4 x i1> %{{.*}})
define void @main(ptr %p) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %loaded = load i32, ptr %p
  %added = add i32 %loaded, 1
  store i32 %added, ptr %p
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
