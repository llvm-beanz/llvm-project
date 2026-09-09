; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L7o: a masked *store* of a vector-typed value through a
; groupshared address whose own array index is uniform (the same on every
; lane) but not a compile-time constant, reduced from a real
; `dEQP-VK.subgroups.basic.compute.subgroupelect` failure --
; `subgroupElect()`-gated code has exactly one invocation per subgroup
; write a whole vector-typed row (e.g. `tempBuffer[gl_SubgroupID] = ...`)
; through an address every lane still computes identically, just gated
; per-lane by the `subgroupElect()` mask itself. Unlike
; simdize-groupshared-masked-vector-row-store.ll's own divergent-index
; case (whose `getelementptr` widens directly into a real
; `<W x ptr>` vector-of-pointers, since its own index genuinely varies per
; lane), a *uniform* index keeps `widenGroupSharedGEP`'s scalar
; `getelementptr` as-is and broadcasts it into a `<W x ptr>` via the usual
; `insertelement`/`shufflevector` splat idiom instead
; (`FunctionWidener::widenMaskedStore`'s vector case then indexes that
; broadcast, one `getelementptr` per row component, exactly as it would a
; genuinely divergent row address). `feme::cpu::rewriteGroupSharedGlobals`
; previously only recognized this second-level per-component
; `getelementptr` shape when the address it indexed was itself a real,
; naturally divergent vector-of-pointers (roadmap L11), not a *broadcast*
; of a uniform one -- declining with "feeds a nested getelementptr or
; another unsupported user" -- and its own retargeting logic assumed a
; broadcast always fed a gather/scatter call directly, never a nested
; `getelementptr`. Both `hasOnlySupportedBroadcasts`'s validation (now
; `isSupportedGroupSharedRowUser`) and `retargetGroupSharedProducer`'s own
; retargeting (now delegating to itself recursively for a broadcast's
; final value, exactly as it already did for a first-level `getelementptr`
; off a groupshared global) have been extended to recognize this shape
; too.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: t:
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %ptr{{[0-9]*}} = getelementptr inbounds [1 x <4 x i32>], ptr %shared.flat, i32 0, i32 %widx{{[0-9]*}}
; CHECK-NEXT: %ptr{{[0-9]*}}.splat.splatinsert = insertelement <4 x ptr> poison, ptr %ptr{{[0-9]*}}, i64 0
; CHECK-NEXT: %ptr{{[0-9]*}}.splat.splat = shufflevector <4 x ptr> %ptr{{[0-9]*}}.splat.splatinsert, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: %masked.mask = and <4 x i1> %wave_sideeffect_mask, %sideeffect.t.wide
; CHECK-NEXT: %{{.*}}.elt0.ptr{{[0-9]*}} = getelementptr <4 x i32>, <4 x ptr> %ptr{{[0-9]*}}.splat.splat, i32 0, i32 0
; CHECK-NEXT: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> zeroinitializer, <4 x ptr> align 16 %{{.*}}.elt0.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
; CHECK-NEXT: %{{.*}}.elt1.ptr{{[0-9]*}} = getelementptr <4 x i32>, <4 x ptr> %ptr{{[0-9]*}}.splat.splat, i32 0, i32 1
; CHECK-NEXT: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> zeroinitializer, <4 x ptr> align 4 %{{.*}}.elt1.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
; CHECK-NEXT: %{{.*}}.elt2.ptr{{[0-9]*}} = getelementptr <4 x i32>, <4 x ptr> %ptr{{[0-9]*}}.splat.splat, i32 0, i32 2
; CHECK-NEXT: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> zeroinitializer, <4 x ptr> align 8 %{{.*}}.elt2.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
; CHECK-NEXT: %{{.*}}.elt3.ptr{{[0-9]*}} = getelementptr <4 x i32>, <4 x ptr> %ptr{{[0-9]*}}.splat.splat, i32 0, i32 3
; CHECK-NEXT: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> zeroinitializer, <4 x ptr> align 4 %{{.*}}.elt3.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  ; `dx_wave_get_lane_count` is classified `AlwaysUniform` by
  ; `feme::cpu::computeWaveUniformity` -- a stand-in for the uniform,
  ; not-compile-time-constant `gl_SubgroupID`-derived index the real CTS
  ; shader's own row address uses, without depending on this test needing
  ; a real `gl_SubgroupID` intrinsic of its own.
  %widx = call i32 @llvm.dx.wave.get.lane.count()
  %cond = icmp eq i32 %tid, 0
  br i1 %cond, label %t, label %f
t:
  %ptr = getelementptr inbounds [1 x <4 x i32>], ptr addrspace(3) @shared, i32 0, i32 %widx
  store <4 x i32> zeroinitializer, ptr addrspace(3) %ptr
  br label %end
f:
  br label %end
end:
  ret void
}
@shared = internal addrspace(3) global [1 x <4 x i32>] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
declare i32 @llvm.dx.wave.get.lane.count()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
