; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H72 (feme/docs/Roadmap.md): same barrier-free spin-wait shape as
; entry-wrapper-barrier-free-loop-self.ll, but with the loop's own
; increment (`iterations++`) split into its own latch block rather than
; folded into the header -- the two-block loop shape a `while` with a
; non-empty body more commonly lowers to. `matchBarrierFreeLoop` walks the
; straight, barrier-free chain of blocks from the header's other
; successor until it closes back to the header itself, same as the
; self-loop case, just with one block (`body`) to walk through first.

; CHECK-LABEL: define internal void @main(
; CHECK: header:
; CHECK-NEXT: %iters = phi i32 [ 0, %entry.split ], [ %iters.next, %body ]
; CHECK: %val = load i32, ptr %{{.*}}
; CHECK-NEXT: %cond = icmp ne i32 %val, 1
; CHECK-NEXT: br i1 %cond, label %body, label %after
; CHECK: body:
; CHECK-NEXT: %iters.next = add i32 %iters, 1
; CHECK-NEXT: br label %header
; CHECK: after:
; CHECK-NEXT: ret void

; CHECK-LABEL: define internal void @main.region0(
; CHECK: store i32 %wave_group_id_x, ptr %ptr0

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: wave.loop.header.0:
; CHECK: call void @main.region0(
; CHECK: wave.loop.exit.0:
; CHECK-NEXT: fence syncscope("singlethread") acq_rel
; CHECK: wave.loop.header.1:
; CHECK: call void @main(
; CHECK: wave.loop.exit.1:
; CHECK-NEXT: ret void
define void @main() #0 {
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %ptr0 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  store i32 %gid, ptr addrspace(3) %ptr0
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  br label %header

header:
  %iters = phi i32 [ 0, %entry ], [ %iters.next, %body ]
  %ptr1 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 1
  %val = load i32, ptr addrspace(3) %ptr1
  %cond = icmp ne i32 %val, 1
  br i1 %cond, label %body, label %after

body:
  %iters.next = add i32 %iters, 1
  br label %header

after:
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
