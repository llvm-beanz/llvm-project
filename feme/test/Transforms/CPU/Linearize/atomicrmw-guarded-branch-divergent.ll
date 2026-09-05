; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L43: a real CTS mesh shader's own "allocate a unique output slot"
; pattern -- an unconditional, per-lane atomic increment (`%old`) whose
; result immediately decides a later divergent branch (`%cond`/`br`), with
; no intervening divergent control flow of its own -- confirmed via a real
; captured pre-`LinearizePass` IR dump of
; `dEQP-VK.mesh_shader.ext.query.no_queries.*.mesh_only.*`. Before this fix,
; `feme::cpu::WaveTTIImpl::getValueUniformity` only classified the
; `feme.cpu.masked.atomicrmw.*` *call* form `NeverUniform` (roadmap L41);
; `feme::cpu::UniformityInfo` here is computed once, up front, strictly
; before `feme::cpu::DiamondFlattener`'s own `applyStageMasks` ever converts
; a plain `atomicrmw` like `%old` into that call form, so at the point this
; analysis actually runs, `%old` is still a plain `atomicrmw`, which fell
; through to the generic operand-driven `Default` rule -- wrongly uniform,
; since every one of its own operands (a uniform pointer, uniform value)
; is, even though its own real per-lane result genuinely differs. This left
; `%cond`/`br i1 %cond` wrongly classified uniform too, so
; `DiamondFlattener` never attempted to flatten this branch at all, leaving
; it in place (with `%if_true`'s own store simply masked with whatever mask
; was already in scope, unnarrowed by `%cond`) for `feme::cpu::SIMDizePass`
; to reject later as an unremoved divergent branch.

; CHECK-LABEL: define void @main(
; CHECK: %old = atomicrmw add ptr %p, i32 1
; CHECK: %cond = icmp ult i32 %old, 32
; CHECK: %sideeffect.t = and i1 true, %cond
; CHECK-NOT: br i1 %cond
; CHECK: if_true:
; CHECK: call void @feme.cpu.masked.stage.output.store.i32({{.*}}, i1 %sideeffect.t)
define void @main(ptr %p) #0 {
entry:
  %old = atomicrmw add ptr %p, i32 1 seq_cst
  %cond = icmp ult i32 %old, 32
  br i1 %cond, label %if_true, label %if_false

if_true:
  call void @feme.stage.output.store.i32(i32 0, i32 0, i32 0, i32 %old, i32 0)
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
attributes #0 = { "hlsl.shader"="mesh" "hlsl.numthreads"="4,1,1" }
