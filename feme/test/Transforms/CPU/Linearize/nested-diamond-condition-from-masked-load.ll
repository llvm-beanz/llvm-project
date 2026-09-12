; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H75: an inner if/else diamond nested inside an outer divergent
; diamond's arm, where the inner diamond's own condition is built from a
; value this pass's own masking of the outer arm's `load` has already made
; per-lane-varying (a masked-off lane reads the passthru -- zero -- instead
; of whatever `%p` actually holds). `UniformityInfo` is computed once,
; before any masking happens (see `feme::cpu::LinearizePass::run`), so it
; sees the original, unmasked `load` -- whose address (`%p`) is a plain
; function argument -- and (correctly, for the *unmasked* IR) classifies it,
; and everything computed from it, as uniform. Left uncorrected, the inner
; branch would keep its real `br`/`phi` shape (see
; `uniform-nested-in-divergent.ll`'s similar-looking, but genuinely uniform,
; case), reintroducing a real divergent branch downstream passes cannot
; widen -- this is exactly the shape reduced from
; `dEQP-VK.mesh_shader.ext.misc.barrier_in_mesh`/`barrier_in_task`'s own
; single-invocation-gated `if (counter == 32) {...} else {...}` reading a
; barrier-synchronized `shared` counter. The fix: any branch condition that
; transitively depends on a masked load's result must be flattened like any
; other divergent branch, regardless of what the (necessarily stale)
; `UniformityInfo` says about it.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c1
; CHECK: t:
; CHECK: %v1 = call i32 @feme.cpu.masked.load.i32(ptr %p, i32 4, i1 %live.t, i32 0)
; CHECK-NEXT: %c2 = icmp eq i32 %v1, 32
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
define void @main(ptr %p) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp eq i32 %tid, 0
  br i1 %c1, label %t, label %f
t:
  %v = load i32, ptr %p
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
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
