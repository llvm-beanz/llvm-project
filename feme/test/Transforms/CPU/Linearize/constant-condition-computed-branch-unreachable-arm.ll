; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(5): `spirv_assembly.instruction.compute.opunreachable.all`'s
; `modulo4(1000)` branch condition is not a literal `ConstantInt` but a pure
; function of literals (`urem` + `icmp` + a phi of constants merged through
; `LowerSwitch`'s flow blocks), whose `br` has an `unreachable` arm.
; `foldConstantConditionBranches` must simplify the feeding instructions and
; the phis left by pruning to a fixed point so the branch folds; otherwise it
; is misdiagnosed as a divergent branch with no reconvergence point.
;
; CHECK-LABEL: define void @main(
; CHECK-NOT: unreachable
; CHECK-NOT: br i1
; CHECK: call i1 @llvm.spv.wave.any(i1 %cond)
; CHECK-NEXT: ret void
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cond = icmp eq i32 %tid, 0
  %m = urem i32 1000, 4
  %c0 = icmp eq i32 %m, 0
  br i1 %c0, label %a, label %b

a:
  br label %merge

b:
  br label %merge

merge:
  %v = phi i32 [ 3, %a ], [ 2, %b ]
  %gt = icmp ugt i32 5, %v
  br i1 %gt, label %live, label %dead

dead:
  unreachable

live:
  %any = call i1 @llvm.spv.wave.any(i1 %cond)
  ret void
}

declare i32 @llvm.dx.thread.id(i32)
declare i1 @llvm.spv.wave.any(i1)

attributes #0 = { "feme.shader.stage"="compute" }
