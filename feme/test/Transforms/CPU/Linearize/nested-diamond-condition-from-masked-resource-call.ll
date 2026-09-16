; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H167: like `nested-diamond-condition-from-masked-load.ll`'s own
; plain-`load` case, but for a value read via an *already-masked*
; `feme.cpu.resource.*` call -- the shape `feme::cpu::ResourceLoweringPass`
; leaves behind, rather than a plain `load` this pass itself converts (see
; `applyStageMasks`'s own `MaskedLoads` comment). `t`'s own resource call
; starts with a constant `true` mask (as `ResourceLoweringPass` always
; leaves it -- see `resource-call-masked.ll`), which this pass rewrites in
; place to the block's real, non-constant mask (`%live.t`) the same way any
; other memory access in a divergent arm is rewritten -- but, unlike a
; plain `load`, that rewrite is *not* recorded into `MaskedLoadResults`,
; since it was never a plain `load` to begin with. Before this fix,
; `dependsOnTaintedValue` only consulted that recorded set, so the inner
; branch built from `%v` (now genuinely per-lane-varying: a masked-off lane
; reads the passthru, not the real value) kept its real, unflattened
; `br`/`phi` shape, since `UniformityInfo` -- computed once, before any of
; this masking happens -- still calls it uniform. Reduced from
; `InterlockedExchange.resources.32.test`'s own post-loop, single-
; invocation verification reads, where `feme::cpu::SIMDizePass`'s own
; fresh `UniformityInfo` (with no visibility into any of this pass's
; masking) went on to reject the surviving branch as an unremoved
; divergent one.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c1
; CHECK: t:
; CHECK: %v = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 %desc, i64 0, i1 %live.t)
; CHECK-NEXT: %c2 = icmp eq i32 %v, 32
; CHECK-NOT: br i1 %c2
; CHECK: inner.t:
; CHECK-NEXT: %x1 = add i32 %tid, 10
; CHECK-NEXT: br label %inner.f
; CHECK: inner.f:
; CHECK-NEXT: %x2 = add i32 %tid, 20
; CHECK-NEXT: br label %outer.end
; CHECK: outer.end:
; CHECK: %inner.v.linearized = select i1 %c2, i32 %x1, i32 %x2
; CHECK-NOT: phi
define void @main(ptr %heap, i32 %heap_count, i32 %desc) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp eq i32 %tid, 0
  br i1 %c1, label %t, label %f
t:
  %v = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 %desc, i64 0, i1 true)
  %c2 = icmp eq i32 %v, 32
  br i1 %c2, label %inner.t, label %inner.f
inner.t:
  %x1 = add i32 %tid, 10
  br label %outer.end
inner.f:
  %x2 = add i32 %tid, 20
  br label %outer.end
outer.end:
  %inner.v = phi i32 [%x1, %inner.t], [%x2, %inner.f]
  br label %end
f:
  br label %end
end:
  %v2 = phi i32 [%inner.v, %outer.end], [0, %f]
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
