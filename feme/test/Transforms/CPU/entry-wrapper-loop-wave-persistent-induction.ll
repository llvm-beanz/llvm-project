; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H159 (feme/docs/Roadmap.md): a "barriers inside a uniform loop"
; shape with two header phis of genuinely different kinds. `%i` is the
; loop's own uniform scalar trip counter, cloned into the wrapper as an
; ordinary scalar loop phi. `%acc` instead accumulates a per-lane value
; computed inside the loop's own barrier region, so its recurrence is
; never available to the wrapper at all -- it is threaded through its own
; slot of the per-wave barrier-spill context array (H159(b)), which is
; allocated outside the wrapper's loop and so carries the value across the
; loop's own backedge: seeded once per wave by the prefix region,
; reloaded at its use, and stored back right after the recurrence.

; CHECK: %main.barrier_spill = type { i32 }

; The prefix region seeds every wave's slot with the phi's initial value.
; CHECK-LABEL: define internal void @main.prefix0(
; CHECK: %acc.init.slot = getelementptr %main.barrier_spill, ptr %barrier_spill, i32 %wave_index
; CHECK: %acc.init = getelementptr {{.*}} %main.barrier_spill, ptr %acc.init.slot, i32 0, i32 0
; CHECK: store i32 0, ptr %acc.init

; The body region reloads the carried value, recomputes it, and stores it
; straight back into the same slot for the next iteration.
; CHECK-LABEL: define internal void @main.body0(
; CHECK: %acc.reload.val = load i32, ptr %acc.reload
; CHECK: %acc.next = add i32 %acc.reload.val, %wave_group_id_x
; CHECK: store i32 %acc.next, ptr %acc.carry

; Only `%i` survives as a scalar phi in the wrapper's own loop -- phis are
; contiguous at the top of a block, so the `icmp` right after it proves
; `%acc` has none. The spill array carrying `%acc` is allocated once,
; outside that loop.
; CHECK-LABEL: define void @feme_cpu_entry_main(
; CHECK: %barrier.spill = alloca [1 x %main.barrier_spill]
; CHECK: loop.header:
; CHECK-NEXT: %loopvar0 = phi i32 [ 0, %{{.*}} ], [ %{{.*}}, %loop.latch ]
; CHECK-NEXT: %[[COND:.*]] = icmp ult i32 %loopvar0, 4
; CHECK-NEXT: br i1 %[[COND]], label %loop.body.iter, label %loop.exit

define void @main() #0 {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %flow ]
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %flow, label %after

flow:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %acc.next = add i32 %acc, %gid
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %i.next = add i32 %i, 1
  br label %header

after:
  ret void
}

declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
