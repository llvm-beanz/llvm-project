; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L11: a groupshared address's own *vector-typed* load at a
; genuinely divergent (per-lane-varying) index -- e.g. reading a whole
; `float4` row out of a `groupshared float4x4` at a per-lane row index,
; reduced from a real `WaveOps/GroupSharedMatrixRowComponentDataRace.test`
; failure (`SharedMat[ThreadID.y]`) -- decomposes into one
; `llvm.masked.gather` per row component rather than one illegal
; `<W x <4 x float>>` gather: `FunctionWidener::widenGroupSharedLoad`'s
; vector case builds one `getelementptr` per component off the same
; `<W x ptr>` row-address vector `widenGroupSharedGEP` already built (LLVM
; treats a fixed-vector source element type as an indexable sequential
; type exactly like an array), then gathers each component independently
; -- each with its own, narrower-than-the-row alignment
; (`commonAlignment` of the whole row's own alignment and that
; component's byte offset within it, since only the first component
; shares the row's full alignment) -- recording the result in `WidenedVectorComponents` like every other
; vector-typed producer. (Mirroring simdize-groupshared-divergent-index.ll,
; the loaded row is left unused -- this test is only about the load's own
; widening, not any particular consumer shape, which the other
; `simdize-groupshared-*.ll`/`simdize-vector-*.ll` tests already cover
; individually.)

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %ptr.wide{{[0-9]*}} = getelementptr inbounds [4 x <4 x float>], ptr %shared.flat, i32 0, <4 x i32> %tid{{[0-9]*}}
; CHECK-NEXT: %row.elt0.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 0
; CHECK-NEXT: %row.elt0{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 16 %row.elt0.ptr{{[0-9]*}}, <4 x i1> %wave_entry_mask, <4 x float> zeroinitializer)
; CHECK-NEXT: %row.elt1.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 1
; CHECK-NEXT: %row.elt1{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 4 %row.elt1.ptr{{[0-9]*}}, <4 x i1> %wave_entry_mask, <4 x float> zeroinitializer)
; CHECK-NEXT: %row.elt2.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 2
; CHECK-NEXT: %row.elt2{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 8 %row.elt2.ptr{{[0-9]*}}, <4 x i1> %wave_entry_mask, <4 x float> zeroinitializer)
; CHECK-NEXT: %row.elt3.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 3
; CHECK-NEXT: %row.elt3{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 4 %row.elt3.ptr{{[0-9]*}}, <4 x i1> %wave_entry_mask, <4 x float> zeroinitializer)
; CHECK-NEXT: ret void
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %ptr = getelementptr inbounds [4 x <4 x float>], ptr addrspace(3) @shared, i32 0, i32 %tid
  %row = load <4 x float>, ptr addrspace(3) %ptr
  ret void
}
@shared = internal addrspace(3) global [4 x <4 x float>] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
