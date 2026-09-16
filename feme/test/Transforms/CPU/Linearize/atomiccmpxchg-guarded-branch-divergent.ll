; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H164: the `AtomicCmpXchgInst` sibling of
; `atomicrmw-guarded-branch-divergent.ll`'s own `atomicrmw` shape -- a real
; `Feature/HLSLLib/InterlockedCompareExchange.32.test` pattern reduced to
; its essential control flow: a groupshared-destination `cmpxchg` (`%old`)
; whose `extractvalue`d result (`%oldval`) immediately decides a later
; divergent branch (`%cond`/`br`), with no intervening divergent control
; flow of its own. Before this fix, `feme::cpu::WaveTTIImpl::
; getValueUniformity` only classified a plain `atomicrmw` `NeverUniform`
; (roadmap L43); a plain `cmpxchg` fell through to the generic
; operand-driven `Default` rule -- wrongly uniform, since every one of its
; own operands (a uniform pointer, uniform compare/new values) is, even
; though its own real per-lane result genuinely differs. This left
; `%oldval`/`%cond`/`br i1 %cond` wrongly classified uniform too, so
; `DiamondFlattener` never attempted to flatten this branch at all, leaving
; it in place for `feme::cpu::SIMDizePass` to reject later as an unremoved
; divergent branch with no widened value to give it, substituting `poison`
; for the erased scalar `cmpxchg`'s remaining uses and producing a branch
; condition that is provably `poison` -- undefined behaviour that surfaced
; as a JIT-compiled stage segfaulting with no usable stack.

; CHECK-LABEL: define void @main(
; CHECK: %old = cmpxchg ptr %p, i32 0, i32 1
; CHECK: %oldval = extractvalue { i32, i1 } %old, 0
; CHECK: %cond = icmp eq i32 %oldval, 0
; CHECK: %sideeffect.t = and i1 true, %cond
; CHECK-NOT: br i1 %cond
; CHECK: if_true:
; CHECK: call void @feme.cpu.masked.stage.output.store.i32({{.*}}, i1 %sideeffect.t)
define void @main(ptr %p) #0 {
entry:
  %old = cmpxchg ptr %p, i32 0, i32 1 seq_cst seq_cst
  %oldval = extractvalue { i32, i1 } %old, 0
  %cond = icmp eq i32 %oldval, 0
  br i1 %cond, label %if_true, label %if_false

if_true:
  call void @feme.stage.output.store.i32(i32 0, i32 0, i32 0, i32 %oldval, i32 0)
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="4,1,1" }
