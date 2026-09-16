; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H163 (feme/docs/Roadmap.md): the shape real DXC output produces
; for the `Interlocked*` tests. A per-lane loop-carried value is read
; after the recurrence that updates it (`%late`), and again after the
; loop has finished (`%out`); a value computed before the loop (`%pre`)
; is read inside it, crossing a region boundary that is not a barrier.

; The pre-loop value is spilled into the per-wave context array rather
; than referenced across region functions.
; CHECK-LABEL: define internal void @main.prefix0(
; CHECK: store <4 x i32> %{{.*}}, ptr %{{.*}}

; The loop body reloads the persistent accumulator at the top of its
; region -- ahead of both the late use and the store-back -- so it still
; observes this iteration's value.
; CHECK-LABEL: define internal void @main.body0(
; CHECK: load <4 x i32>, ptr
; CHECK: %[[CARRY:.*]] = add <4 x i32>
; CHECK: store <4 x i32> %[[CARRY]], ptr
; CHECK: mul <4 x i32> %{{.*}}.reload.val{{[0-9]*}}, splat (i32 3)

; After the loop the suffix region reloads the same slot, which now holds
; the final iteration's value.
; CHECK-LABEL: define internal void @main.suffix0(
; CHECK: load <4 x i32>, ptr %{{.*}}

define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %pre = mul i32 %tid, 3
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %flow ]
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %flow, label %after

flow:
  %acc.next = add i32 %acc, %pre
  %late = mul i32 %acc, 3
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %i.next = add i32 %i, 1
  br label %header

after:
  %out = add i32 %acc, 7
  ret void
}

declare i32 @llvm.dx.thread.id.in.group(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
