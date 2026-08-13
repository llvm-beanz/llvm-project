; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; An `atomicrmw` has no vector form, so it falls back to the generic,
; "always applicable" scalarization: `W` clones of the instruction, each fed
; its lane's extracted scalar operands, with the per-lane results
; reassembled into a vector (see "Scalarization fallback" in "Phase 4:
; Widening"). This is what lets widening be total rather than reject an
; unsupported divergent opcode -- a divergent `getelementptr` computing the
; per-lane address is scalarized the same way, one instance up the chain.

; CHECK-LABEL: define void @main(
; CHECK-COUNT-4: %{{.*}} = getelementptr i32, ptr %{{.*}}, i64 %{{.*}}
; CHECK-COUNT-4: atomicrmw add ptr %{{.*}}, i32 1 monotonic
define void @main(ptr %p) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tid64 = zext i32 %tid to i64
  %ptr = getelementptr i32, ptr %p, i64 %tid64
  %old = atomicrmw add ptr %ptr, i32 1 monotonic
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
