; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --groups=2,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; V3: "Verify workgroup barrier correctness for multi-wave groups under
; sequential wave execution" (see FeMeVulkanDesign.md's V3 milestone).
; `numthreads` is 8 and `--wave-size` is 4, so each of the two dispatched
; groups is *two* waves (`feme::cpu::EntryWrapperPass`'s wave loop runs
; sequentially, wave 0 to completion then wave 1 -- see
; "Group Execution and Barriers" in feme/docs/FeMeCPUDesign.md). Every
; existing barrier/groupshared end-to-end test (barrier-groupshared.hlsl,
; multi-group-barrier.hlsl) uses `numthreads(4, 1, 1)` with a wave size of
; at least 4, so every dispatched group there is exactly *one* wave; this
; is the first end-to-end coverage of a group spanning more than one.
;
; Every lane of a group writes the same (group-uniform) value into
; `@shared` -- derived from the group's own id, so group 0 writes 100 and
; group 1 writes 101 -- then, after
; `llvm.dx.group.memory.barrier.with.group.sync`, every lane (across both
; waves) reads it back and stores it to its own slot of the output heap,
; keyed by its *global* dispatch thread id (spanning both groups' 8 lanes
; each, 16 total). Every output slot of a group must equal that group's
; own published value, proving `@shared` is one allocation genuinely
; shared by every wave of a group, not accidentally reallocated per wave.
; (This model's own sequential wave execution already guarantees
; wave-order visibility regardless of the barrier call -- see "Group
; Execution and Barriers" in feme/docs/FeMeCPUDesign.md -- proving the
; barrier *itself* is load-bearing needs a value that is wave-, not
; group-, uniform, which the "only a compile-time-constant groupshared
; index is canonicalized" restriction `feme::cpu::
; rewriteGroupSharedGlobals` documents (GroupShared.cpp) does not yet let
; a shader express; see entry-wrapper-barrier-multi-wave.ll for the
; structural proof that the two-wave case still splits into one wave loop
; per barrier region rather than assuming a single wave, which is the part
; that could actually regress silently).

; CHECK: heap[0]: 100 100 100 100 100 100 100 100 101 101 101 101 101 101 101 101

;--- shader.ll
@shared = internal addrspace(3) global [1 x i32] undef

define void @main() #0 {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32 0, i1 false)
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %val = add i32 100, %gid
  %ptr = getelementptr inbounds [1 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  store i32 %val, ptr addrspace(3) %ptr
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %loaded = load i32, ptr addrspace(3) %ptr
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = mul i32 %tid, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %loaded)
  ret void
}
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap(i32, i1)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }

;--- heap.yaml
resource-heap:
  - index: 0
    size: 64
