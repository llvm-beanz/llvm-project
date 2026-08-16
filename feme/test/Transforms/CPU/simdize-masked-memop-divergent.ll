; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A `feme.cpu.masked.load`/`.store` pair at a divergent (per-lane) address
; widens to `llvm.masked.gather`/`.scatter` over a `<W x ptr>` address
; vector, ANDing the call's own governing mask (the diamond arm's mask) into
; the wave's entry mask -- the same pattern `feme.cpu.resource.*` widening
; already uses (see "masked feme.cpu.resource.* call" and the "Mask
; representation between phases" table's masked-load/store rows in "Phase 4:
; Widening" / "Phase 3: Linearization and Predication" in
; feme/docs/FeMeCPUDesign.md).

; CHECK-LABEL: define void @main(
; CHECK: t:
; CHECK: %masked.mask = and <4 x i1> %wave_entry_mask, %live.t.wide
; CHECK: call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr>{{.*}}, <4 x i1> %masked.mask, <4 x i32> zeroinitializer)
; CHECK: %masked.mask{{[0-9]*}} = and <4 x i1> %wave_entry_mask, %sideeffect.t.wide
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> %{{.*}}, <4 x ptr>{{.*}}, <4 x i1> %masked.mask{{[0-9]*}})
define void @main(ptr %p) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %off = zext i32 %tid to i64
  %addr = getelementptr i32, ptr %p, i64 %off
  %loaded = load i32, ptr %addr
  %added = add i32 %loaded, 1
  store i32 %added, ptr %addr
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
