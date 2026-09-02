; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6n: a divergently-indexed `store` of a whole *vector*-typed
; value (as opposed to a divergent vector-typed *result*, which
; `widenScalarizedFallback` already decomposed into `WidenedVectorComponents`
; before this fix) previously crashed `getWidened` -- both `widenElementwise`'s
; own eager operand-widening loop and `widenScalarizedFallback`'s -- with an
; `llvm::FixedVectorType::get` assertion failure ("Element type of a
; VectorType must be an integer, floating point, pointer type...") by
; trying to build an illegal `<4 x <4 x float>>` nested vector. Neither
; `widenMaskedStore` (reserved for the canonical `feme.cpu.masked.store.*`
; call shape) nor `widenGroupSharedStore` (reserved for a groupshared
; address) applies to an ordinary `store` through an ordinary address --
; this falls through to the fully-generic scalarization fallback, which
; now clones the `store` once per lane, reassembling each lane's own real
; `<4 x float>` operand from its decomposed per-component wide form
; (`getVectorComponents`) rather than ever widening the vector value
; itself.

@arr = external global [4 x <4 x float>]

; CHECK-LABEL: define void @main(
; CHECK-COUNT-4: store <4 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00, float 4.000000e+00>, ptr %{{.*}}, align 16
; CHECK-NOT: store
define void @main() #0 {
  %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
  %ptr = getelementptr [4 x <4 x float>], ptr @arr, i32 0, i32 %tid
  store <4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, ptr %ptr, align 16
  ret void
}
declare i32 @llvm.spv.flattened.thread.id.in.group()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
