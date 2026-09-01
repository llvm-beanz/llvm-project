; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent value of aggregate (struct/array) type built by anything
; other than an `insertvalue` chain or a nested sub-aggregate
; `extractvalue` -- e.g. an ordinary, non-groupshared `load` of aggregate
; type through a divergent address -- is diagnosed rather than crashing
; (roadmap milestone L21 gave an `insertvalue`/`extractvalue`-built
; aggregate its own per-scalar-leaf decomposition, mirroring a divergent
; vector's own "Vectors become components, not nested vectors" scheme in
; "Phase 4: Widening" -- see `simdize-aggregate-struct.ll` -- but a
; divergent-address aggregate load has no such producer support: unlike a
; vector-typed one (roadmap H7o, `widenScalarizedFallback`'s own
; `WidenedVectorComponents` case), scalarizing this fallback's per-lane
; clone into `WidenedAggregateComponents` would need its own further
; per-leaf `extractvalue` decomposition this row's own narrow scope, closed
; by a real `packed.test` IR reduction, never needed).

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent value 'loaded' of aggregate type; component decomposition is not yet supported
define void @main(ptr %p) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %off = zext i32 %tid to i64
  %addr = getelementptr [2 x float], ptr %p, i64 %off
  %loaded = load [2 x float], ptr %addr
  %v0 = extractvalue [2 x float] %loaded, 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
