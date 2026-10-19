; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L440(c): a `br i1 <literal constant>, label %live, label
; %unreachable` with one `unreachable` arm is valid, conformant SPIR-V on
; its own -- DXC's own LLVM-SPIRV backend emits exactly this shape as
; `OpBranchConditional %true %live %unreachable` boilerplate for a required
; `OpSelectionMerge` even when the branch's own condition is a compile-time
; constant (confirmed via `spirv-dis` on `offload-test-suite`'s own
; `Feature/WaveOps/WaveActiveAnyTrue.test`, whose `switch` statement's own
; SPIR-V codegen produces this exact pattern). No earlier pipeline stage
; folds constant branches or prunes dead blocks (see `Pipeline.cpp`), so
; without `foldConstantConditionBranches` this reaches
; `feme::cpu::DiamondFlattener`'s `immediatePostDom` unchanged, which finds
; no common post-dominator for the branch's two successors (`unreachable`
; has none of its own) and misdiagnoses a trivially, statically resolvable
; branch as a genuine "divergent branch ... has no reconvergence point".
;
; CHECK-LABEL: define void @main(
; CHECK-NOT: unreachable
; CHECK: call i1 @llvm.spv.wave.any(i1 %cond)
; CHECK-NEXT: ret void
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cond = icmp eq i32 %tid, 0
  br i1 true, label %live, label %dead

dead:
  unreachable

live:
  %any = call i1 @llvm.spv.wave.any(i1 %cond)
  ret void
}

declare i32 @llvm.dx.thread.id(i32)
declare i1 @llvm.spv.wave.any(i1)

attributes #0 = { "feme.shader.stage"="compute" }
