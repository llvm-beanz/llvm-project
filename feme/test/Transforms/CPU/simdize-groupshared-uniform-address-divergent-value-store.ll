; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H168: `FunctionWidener::widenGroupSharedStore` reaches a store
; whenever `UniformityInfo` marks the store itself divergent -- which
; tracks the more divergent of the store's own pointer *and* value
; operands, not the pointer alone. A uniform-address, divergent-value
; store -- e.g. `groupshared int Shared; Shared = ThreadID;`, every lane
; racing to write its own value to the identical fixed address -- is
; exactly such a case: the pointer is never widened by
; `widenGroupSharedGEP` (there is nothing divergent about it to widen), so
; looking it up directly in `Widened` (as a load's always-divergent
; pointer safely can, see `widenGroupSharedLoad`) returned null, and
; `CreateMaskedScatter` crashed dereferencing it. Before this fix, this
; crashed `feme-opt` outright (confirmed via a standalone
; `-passes=feme-cpu-simdize` reduction, discovered as a detour across at
; least two prior sessions constructing loop-body unit tests for other
; milestones). Fixed by routing the pointer operand through `getWidened`
; instead, which broadcasts a uniform pointer into a `<W x ptr>` splat
; (matching the raw, unwidened program's own last-writer-wins race)
; exactly like a genuinely divergent value already does.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: %tid1 = call <4 x i32> @feme.cpu.builtin.thread_id_in_group.v4(
; CHECK: %shared.flat.splat.splat = shufflevector <4 x ptr> {{.*}}, <4 x ptr> poison, <4 x i32> zeroinitializer
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> %tid1, <4 x ptr> align 4 %shared.flat.splat.splat, <4 x i1> %wave_sideeffect_mask)
define void @main() #0 {
  %tid = call i32 @llvm.spv.thread.id.in.group.i32(i32 0)
  store i32 %tid, ptr addrspace(3) @shared, align 4
  ret void
}
@shared = internal addrspace(3) global i32 undef
declare i32 @llvm.spv.thread.id.in.group.i32(i32) #1
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
attributes #1 = { nounwind willreturn memory(none) }
