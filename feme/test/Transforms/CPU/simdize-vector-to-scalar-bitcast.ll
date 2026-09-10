; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s --check-prefixes=CHECK,W4
; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=8 -S %s | FileCheck %s --check-prefixes=CHECK,W8

; Roadmap L89g: `bitcast <4 x i32> %mask to i128` over a divergent,
; per-lane-decomposed vector -- the shape GLSL's `subgroupBallotBitCount`
; and the `gl_Subgroup{Eq,Ge,Gt,Le,Lt}Mask` builtins take once their `uvec4`
; ballot mask is folded back into one wide integer to be popcounted.
; Reduced from a real failing
; `dEQP-VK.subgroups.builtin_mask_var.compute.subgroupeqmask` case.
;
; This is the one `CastInst` shape whose result is *not* a vector, so it
; cannot go through `widenVectorElementwise`'s component-for-component rule.
; `widenVectorToScalarBitCast` instead recomposes the four already-decomposed
; `<W x i32>` components into a single `<W x i128>` by zero-extending each and
; OR-ing it into place at its own bit offset -- lane-wise exactly what the
; scalar `bitcast` meant for one lane.

; CHECK-LABEL: define void @main(
; CHECK-NOT: bitcast <4 x i32>
; W4: %[[C0:.*]] = zext <4 x i32> {{.*}} to <4 x i128>
; W4: %[[C1:.*]] = zext <4 x i32> {{.*}} to <4 x i128>
; W4: %[[S1:.*]] = shl <4 x i128> %[[C1]], splat (i128 32)
; W4: %[[O1:.*]] = or <4 x i128> %[[C0]], %[[S1]]
; W4: %[[C2:.*]] = zext <4 x i32> {{.*}} to <4 x i128>
; W4: %[[S2:.*]] = shl <4 x i128> %[[C2]], splat (i128 64)
; W4: %[[O2:.*]] = or <4 x i128> %[[O1]], %[[S2]]
; W4: %[[C3:.*]] = zext <4 x i32> {{.*}} to <4 x i128>
; W4: %[[S3:.*]] = shl <4 x i128> %[[C3]], splat (i128 96)
; W4: %{{.*}}.wide{{.*}} = or <4 x i128> %[[O2]], %[[S3]]
; W4: call <4 x i128> @llvm.ctpop.v4i128(<4 x i128>

; The same lowering at a different wave width only changes the `<W x ...>`
; vector length -- the number of components, and so the shift amounts, come
; from the *source vector's* own element count, not from `WaveSize`.
; W8: %[[E0:.*]] = zext <8 x i32> {{.*}} to <8 x i128>
; W8: shl <8 x i128> {{.*}}, splat (i128 32)
; W8: shl <8 x i128> {{.*}}, splat (i128 64)
; W8: shl <8 x i128> {{.*}}, splat (i128 96)
; W8: call <8 x i128> @llvm.ctpop.v8i128(<8 x i128>

define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %m0 = insertelement <4 x i32> poison, i32 %tid, i64 0
  %m1 = insertelement <4 x i32> %m0, i32 %tid, i64 1
  %m2 = insertelement <4 x i32> %m1, i32 %tid, i64 2
  %m3 = insertelement <4 x i32> %m2, i32 %tid, i64 3
  %wide = bitcast <4 x i32> %m3 to i128
  %count = call i128 @llvm.ctpop.i128(i128 %wide)
  ret void
}

declare i32 @llvm.dx.thread.id(i32)
declare i128 @llvm.ctpop.i128(i128)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
