; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L276: `llvm.assume`/`llvm.expect.iN` (lowered from
; `VK_KHR_shader_expect_assume`'s `OpAssumeTrueKHR`/`OpExpectKHR` via
; `AssumeTrueConversionPattern`/`ExpectConversionPattern`,
; SPIRVToLLVMPatterns.cpp) fed by a genuinely *divergent* (per-lane-varying)
; operand used to hit `widenElementwise`'s final "unsupported divergent
; call" diagnostic and reject the whole shader's pipeline creation --
; neither intrinsic has a vector-typed overload for
; `getDivergentCallOverloadShape`'s per-lane-masked-call widening to target
; (`Intrinsics.td` declares both scalar-only), so they fell through every
; other call-widening path. Reduced from
; `dEQP-VK.glsl.shader_expect_assume.compute.{assume,expect}.*`: both
; intrinsics are pure compiler hints with no runtime-observable side
; effect, so the fix simply drops the (by-then-redundant, since scalar
; per-lane divergence is inherently masked/reduced away by widening
; anyway) `llvm.assume` call outright, and rewrites any `llvm.expect` use
; to the (widened) value it wraps directly, rather than widening either
; into a real per-lane call.

; CHECK-LABEL: define void @main(
; CHECK-NOT: call {{.*}}llvm.assume
; CHECK-NOT: call {{.*}}llvm.expect
; The `%tidi` producing the assumed/expected condition is still widened
; and used, just without any surviving `llvm.assume`/`llvm.expect` call.
; CHECK: %tidi.wide
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidi = icmp sgt i32 %tid, 0
  call void @llvm.assume(i1 %tidi)

  %expect = call i32 @llvm.expect.i32(i32 %tid, i32 1)
  %e0 = insertelement <4 x i32> poison, i32 %expect, i32 0
  %sel = select i1 %tidi, <4 x i32> %e0, <4 x i32> %e0
  %r0 = extractelement <4 x i32> %sel, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare void @llvm.assume(i1)
declare i32 @llvm.expect.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
