; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L15: a masked *store* of a vector-typed value through a
; groupshared address (e.g. `SharedMat[ThreadID.y] = In[ThreadID.y]`,
; reduced from a real `WaveOps/GroupSharedMatrixRowComponentDataRace.test`
; failure), the write-side sibling of
; simdize-groupshared-masked-vector-row-load.ll's own read-side fix, could
; not use `widenMaskedStore`'s existing generic vector-typed branch (see
; simdize-masked-memop-vector-divergent.ll): that branch extracts each
; lane's own scalar pointer out of the widened `<W x ptr>` address with an
; `extractelement`, a leaf `feme::cpu::rewriteGroupSharedGlobals`'s own
; validation does not recognize (only a `load`/`store`/`atomicrmw` or a
; gather/scatter call's own pointer operand are supported groupshared leaf
; users) -- and, since a groupshared address is retargeted to a real
; per-wave flat buffer rather than an ordinary heap address, safely cannot
; recognize, so this previously hit `rewriteGroupSharedGlobals`'s own
; "feeds a nested getelementptr or another unsupported user" diagnostic.
; `widenMaskedStore` now has its own groupshared-specific vector branch
; (gated on the address's own address space, `isGroupSharedPointerType`),
; decomposing into `N` per-component `llvm.masked.scatter`s off the same
; `<W x ptr>` row address instead, mirroring `widenMaskedLoad`'s own
; vector case exactly -- the second-level-getelementptr-feeding-a-masked-
; scatter shape `rewriteGroupSharedGlobals` already generically supports
; (no change needed there).

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: t:
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %ptr.wide{{[0-9]*}} = getelementptr inbounds [4 x <4 x float>], ptr %shared.flat, i32 0, <4 x i32> %tid{{[0-9]*}}
; CHECK: %masked.mask = and <4 x i1> %wave_sideeffect_mask, %sideeffect.t.wide
; CHECK-NEXT: %{{.*}}.elt0.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 0
; CHECK-NEXT: call void @llvm.masked.scatter.v4f32.v4p0(<4 x float> splat (float 1.000000e+00), <4 x ptr> align 16 %{{.*}}.elt0.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
; CHECK-NEXT: %{{.*}}.elt1.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 1
; CHECK-NEXT: call void @llvm.masked.scatter.v4f32.v4p0(<4 x float> splat (float 2.000000e+00), <4 x ptr> align 4 %{{.*}}.elt1.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
; CHECK-NEXT: %{{.*}}.elt2.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 2
; CHECK-NEXT: call void @llvm.masked.scatter.v4f32.v4p0(<4 x float> splat (float 3.000000e+00), <4 x ptr> align 8 %{{.*}}.elt2.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
; CHECK-NEXT: %{{.*}}.elt3.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 3
; CHECK-NEXT: call void @llvm.masked.scatter.v4f32.v4p0(<4 x float> splat (float 4.000000e+00), <4 x ptr> align 4 %{{.*}}.elt3.ptr{{[0-9]*}}, <4 x i1> %masked.mask)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %ptr = getelementptr inbounds [4 x <4 x float>], ptr addrspace(3) @shared, i32 0, i32 %tid
  store <4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, ptr addrspace(3) %ptr
  br label %end
f:
  br label %end
end:
  ret void
}
@shared = internal addrspace(3) global [4 x <4 x float>] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
