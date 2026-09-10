; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s --check-prefixes=CHECK,W4
; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=8 -S %s | FileCheck %s --check-prefixes=CHECK,W8

; Roadmap L89g: `bitcast i128 %m to <4 x i32>` over a divergent scalar -- the
; exact inverse of `simdize-vector-to-scalar-bitcast.ll`'s shape, and the one
; a subgroup mask builtin (`gl_Subgroup{Eq,Ge,Gt,Le,Lt}Mask`) takes when its
; computed 128-bit value is repackaged into the `uvec4` the SPIR-V ballot ABI
; spells it as.
;
; This is the one vector-*producing* `CastInst` whose operand is a scalar, so
; `widenVectorElementwise`'s component-for-component rule cannot express it:
; before `widenScalarToVectorBitCast` it built an invalid
; `bitcast <W x i128> to <W x i32>` and tripped `CastInst::Create`'s own
; `castIsValid` assertion. The `N` components are recovered instead by
; shifting each down to the bottom and truncating.

; CHECK-LABEL: define void @main(
; CHECK-NOT: bitcast <{{[0-9]+}} x i128>

; Component 0 needs no shift at all; components 1-3 shift by their own bit
; offset. The shift amounts come from the *destination vector's* element
; width, not from `WaveSize`, so only the `<W x ...>` length changes below.
; W4: %[[C0:.*]] = trunc <4 x i128> %{{.*}} to <4 x i32>
; W4: %[[S1:.*]] = lshr <4 x i128> %{{.*}}, splat (i128 32)
; W4: trunc <4 x i128> %[[S1]] to <4 x i32>
; W4: %[[S2:.*]] = lshr <4 x i128> %{{.*}}, splat (i128 64)
; W4: trunc <4 x i128> %[[S2]] to <4 x i32>
; W4: %[[S3:.*]] = lshr <4 x i128> %{{.*}}, splat (i128 96)
; W4: trunc <4 x i128> %[[S3]] to <4 x i32>
; W4: call <4 x i32> @llvm.ctpop.v4i32(<4 x i32> %[[C0]])

; W8: %[[E0:.*]] = trunc <8 x i128> %{{.*}} to <8 x i32>
; W8: lshr <8 x i128> %{{.*}}, splat (i128 32)
; W8: lshr <8 x i128> %{{.*}}, splat (i128 64)
; W8: lshr <8 x i128> %{{.*}}, splat (i128 96)
; W8: call <8 x i32> @llvm.ctpop.v8i32(<8 x i32> %[[E0]])

define void @main() #0 {
  %lane = call i32 @llvm.spv.subgroup.local.invocation.id()
  %lane128 = zext i32 %lane to i128
  %bit = shl i128 1, %lane128
  %mask = sub i128 %bit, 1
  %vec = bitcast i128 %mask to <4 x i32>
  %word = extractelement <4 x i32> %vec, i64 0
  %count = call i32 @llvm.ctpop.i32(i32 %word)
  ret void
}

declare i32 @llvm.spv.subgroup.local.invocation.id()
declare i32 @llvm.ctpop.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
