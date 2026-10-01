; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L297: a reduction of `Basic/Mandelbrot.test`'s own per-pixel
; escape-iteration loop (and of the standalone minimal repro used to
; root-cause it -- see `agent_thoughts.md`'s own "L297" session entry) --
; a loop whose `Header` itself has a uniform trip-count exit (block `7`'s
; `%10`) *and* whose separate `CheckBlock` (block `18`) has a genuinely
; divergent, per-lane exit (`%20`, derived from the per-lane thread ID),
; where both exits relay through the *same* intermediate dispatch block
; (`Flow3`) one hop before `ExitBlock` (`loop.exit.guard`), each
; contributing a *different* value for one of `ExitBlock`'s own phis
; (`%22`, encoding the HLSL source's own `Diverged` flag) by the time
; `peelConstantFlowPredecessors` folds `Header`'s own route directly onto
; `ExitBlock`.
;
; Before this fix, `linearizeCycle`'s own `ExitBlockRelayValues` restore
; loop used "whichever capture runs first wins": since `Header`'s own
; capture (`true`, via `.Flow3_crit_edge`, now a *direct* `ExitBlock`
; predecessor after the peel) was always captured before `CheckBlock`'s
; own (`%22`'s real value, via `Flow3`, the *other* direct predecessor),
; `CheckBlock`'s own genuinely load-bearing contribution was silently
; discarded whenever the two differed -- here, this collapsed `%22` down
; to a hardcoded `true` constant once the now-fully-unreachable original
; relay chain through `Flow3`/`Flow4` was cleaned up, permanently losing
; the per-lane "did this lane actually take the divergent exit" signal
; and silently miscompiling the shader's own post-loop `Diverged`-gated
; select (seen at runtime as `Basic/Mandelbrot.test` rendering solid
; black instead of the expected fractal).
;
; The fix (see `ExitedViaCheck`'s own declaration comment in
; `Linearize.cpp`) threads a new, genuinely loop-carried boolean phi
; (`%exited.via.check` below) recording, per lane, whether *this* lane's
; own exit (if any, by the current iteration) was via `CheckBlock`
; specifically, and uses it to `select` between `Header`'s and
; `CheckBlock`'s own two captured values for the same phi
; (`%.exit.merge` below) at the final `Latch`->`ExitBlock` edge, rather
; than arbitrarily keeping only the first.

; CHECK-LABEL: define void @main(
; CHECK: %exited.via.check = phi i1 [ false, %0 ], [ %exited.via.check.next, {{.*}} ]
; CHECK: %[[THISITER:exited.via.check.this.iter.*]] = and i1 {{.*}}, %{{.*}}
; CHECK: %[[NEXT:exited.via.check.next]] = or i1 %[[THISITER]], %exited.via.check
; Roadmap L297: the two distinct captured values for the same `ExitBlock`
; phi (`true`, `Header`'s own route; `false`, `CheckBlock`'s own) must
; now be merged via a `select` keyed on the loop-carried
; `%exited.via.check.next` flag, not silently collapsed to one or the
; other.
; CHECK: %.exit.merge = select i1 %[[NEXT]], i1 false, i1 true
; CHECK: loop.exit.guard:
; CHECK-NEXT: %Guard..inv = xor i1 %.exit.merge, true

declare i32 @llvm.spv.thread.id.i32(i32) #0

define void @main() #1 {
  %1 = call i32 @llvm.spv.thread.id.i32(i32 0)
  br label %7

7:                                                ; preds = %Flow3._crit_edge, %0
  %8 = phi i32 [ %1, %0 ], [ %21, %Flow3._crit_edge ]
  %9 = phi i32 [ 0, %0 ], [ %23, %Flow3._crit_edge ]
  %10 = icmp ult i32 %9, 10
  br i1 %10, label %18, label %.Flow3_crit_edge

.Flow3_crit_edge:                                 ; preds = %7
  br label %Flow3

Flow4:                                            ; preds = %.Flow4_crit_edge, %14
  %11 = phi i32 [ %26, %14 ], [ poison, %.Flow4_crit_edge ]
  %12 = phi i32 [ %15, %14 ], [ %9, %.Flow4_crit_edge ]
  %13 = phi i1 [ false, %14 ], [ true, %.Flow4_crit_edge ]
  br label %Flow3

14:                                               ; preds = %25
  %15 = add i32 %9, 1
  br label %Flow4

16:                                               ; preds = %loop.exit.guard._crit_edge, %27
  %17 = phi i1 [ false, %loop.exit.guard._crit_edge ], [ true, %27 ]
  %.inv = xor i1 %17, true
  br i1 %.inv, label %34, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %16
  br label %Flow

18:                                               ; preds = %7
  %19 = mul i32 %8, %8
  %20 = icmp ule i32 %19, 4
  br i1 %20, label %25, label %.Flow4_crit_edge

.Flow4_crit_edge:                                 ; preds = %18
  br label %Flow4

Flow3:                                            ; preds = %.Flow3_crit_edge, %Flow4
  %21 = phi i32 [ %11, %Flow4 ], [ poison, %.Flow3_crit_edge ]
  %22 = phi i1 [ false, %Flow4 ], [ true, %.Flow3_crit_edge ]
  %23 = phi i32 [ %12, %Flow4 ], [ %9, %.Flow3_crit_edge ]
  %24 = phi i1 [ %13, %Flow4 ], [ true, %.Flow3_crit_edge ]
  br i1 %24, label %loop.exit.guard, label %Flow3._crit_edge

Flow3._crit_edge:                                 ; preds = %Flow3
  br label %7

25:                                               ; preds = %18
  %26 = add i32 %8, 1
  br label %14

27:                                               ; preds = %loop.exit.guard
  br label %16

28:                                               ; preds = %Flow._crit_edge, %32
  %29 = phi i32 [ 0, %Flow._crit_edge ], [ %33, %32 ]
  call void @use(i32 %29)
  ret void

Flow:                                             ; preds = %.Flow_crit_edge, %34
  %31 = phi i1 [ false, %34 ], [ true, %.Flow_crit_edge ]
  br i1 %31, label %32, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %28

32:                                               ; preds = %Flow
  %33 = add i32 %23, 1000
  br label %28

34:                                               ; preds = %16
  br label %Flow

loop.exit.guard:                                  ; preds = %Flow3
  %Guard..inv = xor i1 %22, true
  br i1 %Guard..inv, label %27, label %loop.exit.guard._crit_edge

loop.exit.guard._crit_edge:                       ; preds = %loop.exit.guard
  br label %16
}

declare void @use(i32)

attributes #0 = { nounwind willreturn memory(none) }
attributes #1 = { "hlsl.numthreads"="4,1,1" "hlsl.shader"="compute" }
