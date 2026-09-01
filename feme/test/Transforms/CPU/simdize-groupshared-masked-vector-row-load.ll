; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L15: simdize-groupshared-vector-row-load.ll's own L11 fix
; (`widenGroupSharedLoad`'s vector-row gather) is only reachable through a
; plain, unmasked `load` -- not through the `feme.cpu.masked.load.*.as3`
; *call* form `feme::cpu::LinearizePass` produces once that same
; groupshared vector-row access is itself inside genuinely divergent
; control flow (e.g. a real `if (ThreadID.x == 0)` guard, reduced from a
; real `WaveOps/GroupSharedMatrixRowComponentDataRace.test` failure, which
; guards both its `SharedMat[ThreadID.y] = In[ThreadID.y]` write and its
; `Out[ThreadID.y] = SharedMat[ThreadID.y]` read this way).
; `checkVectorDecompositionSupported` did not recognize a
; `feme.cpu.masked.load.*` call producing a vector result as a supported
; producer at all (only an ordinary `LoadInst` was), so this hit the
; generic "has a divergent value ... of vector type" diagnostic instead of
; widening; `widenMaskedLoad` itself also built one illegal
; `<W x <4 x float>>` `llvm.masked.gather` unconditionally. Both are fixed:
; `widenMaskedLoad` now has its own vector-typed branch, mirroring
; `widenGroupSharedLoad`'s per-component decomposition (one narrower
; `llvm.masked.gather` per row component, off the same `<W x ptr>` row
; address), but gathering against this call's own governing mask (not the
; bare wave entry mask) and its own (possibly divergent) passthru operand,
; sliced into per-component values by `getVectorComponents` rather than a
; constant `zeroinitializer`.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: t:
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %ptr.wide{{[0-9]*}} = getelementptr inbounds [4 x <4 x float>], ptr %shared.flat, i32 0, <4 x i32> %tid{{[0-9]*}}
; CHECK: %masked.mask = and <4 x i1> %wave_entry_mask, %live.t.wide
; CHECK-NEXT: %row1.elt0.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 0
; CHECK-NEXT: %row1.elt0{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 16 %row1.elt0.ptr{{[0-9]*}}, <4 x i1> %masked.mask, <4 x float> zeroinitializer)
; CHECK-NEXT: %row1.elt1.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 1
; CHECK-NEXT: %row1.elt1{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 4 %row1.elt1.ptr{{[0-9]*}}, <4 x i1> %masked.mask, <4 x float> zeroinitializer)
; CHECK-NEXT: %row1.elt2.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 2
; CHECK-NEXT: %row1.elt2{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 8 %row1.elt2.ptr{{[0-9]*}}, <4 x i1> %masked.mask, <4 x float> zeroinitializer)
; CHECK-NEXT: %row1.elt3.ptr{{[0-9]*}} = getelementptr <4 x float>, <4 x ptr> %ptr.wide{{[0-9]*}}, i32 0, i32 3
; CHECK-NEXT: %row1.elt3{{[0-9]*}} = call <4 x float> @llvm.masked.gather.v4f32.v4p0(<4 x ptr> align 4 %row1.elt3.ptr{{[0-9]*}}, <4 x i1> %masked.mask, <4 x float> zeroinitializer)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %ptr = getelementptr inbounds [4 x <4 x float>], ptr addrspace(3) @shared, i32 0, i32 %tid
  %row = load <4 x float>, ptr addrspace(3) %ptr
  br label %end
f:
  br label %end
end:
  ret void
}
@shared = internal addrspace(3) global [4 x <4 x float>] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
