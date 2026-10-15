; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s --check-prefix=LINEARIZE-ALONE
; RUN: feme-opt --llvm -passes='module(function(loop-simplify),feme-cpu-linearize)' -S %s | FileCheck %s --check-prefix=WITH-LOOP-SIMPLIFY

; Roadmap K6x (CTS `dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrdiff.
; ssbo_comparisons_diff`/`wg_comparisons_diff`): a divergent if/else
; diamond immediately upstream of a loop, with *both* arms merging
; directly into the loop header itself (no dedicated preheader block per
; arm -- `then`/`else` both branch straight to `loop`), gives `loop` 3
; total predecessors: the two diamond arms plus its own backedge from
; `latch`. `DiamondFlattener::validate` (`feme/lib/Transforms/CPU/
; Linearize.cpp`) only ever expects a reconvergence block to have exactly
; 2 predecessors (`R->hasNPredecessors(2)`), with no special case for "the
; 3rd predecessor is actually this same block's own loop backedge, not a
; genuine 3rd divergent arm" -- so it correctly, conservatively rejects
; this shape today, exactly as `LINEARIZE-ALONE` below still documents.
;
; The fix is not a `Linearize.cpp` change (`DiamondFlattener::flatten`
; assumes, once a 2-predecessor merge is validated, that it can safely
; collapse every phi at the reconvergence block into a single
; `select`-and-erase -- unsafe surgery to attempt on a genuine loop
; header's own multi-way, loop-carried phis without much deeper, riskier
; changes to both `validate` and `flatten`). Instead, `feme::cpu::
; Pipeline.cpp`'s CPU backend pipeline now runs LLVM's own, standard
; `LoopSimplifyPass` immediately before `feme-cpu-linearize`, guaranteeing
; every natural loop has a single dedicated preheader -- `WITH-LOOP-
; SIMPLIFY` below shows this turns the 3-predecessor loop header back
; into the ordinary, already-correctly-handled 2-predecessor diamond
; merge (now at a synthesized preheader block, not the loop header
; itself), needing zero changes to `DiamondFlattener` itself.

; LINEARIZE-ALONE: reconvergence block '{{.*}}' does not have exactly two predecessors

; WITH-LOOP-SIMPLIFY-LABEL: define void @main(
; WITH-LOOP-SIMPLIFY: loop.preheader:
; WITH-LOOP-SIMPLIFY: %i.ph.linearized = select i1 %c, i32 %a, i32 %b
; WITH-LOOP-SIMPLIFY: br label %loop
; WITH-LOOP-SIMPLIFY: loop:
; WITH-LOOP-SIMPLIFY: phi i32 [ %inc, %loop ], [ %i.ph.linearized, %loop.preheader ]
define void @main(i32 %n, ptr %buf) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %a = add i32 %n, 1
  br label %loop
f:
  %b = add i32 %n, 2
  br label %loop
loop:
  %i = phi i32 [%a, %t], [%b, %f], [%inc, %loop]
  %inc = add i32 %i, 1
  store i32 %i, ptr %buf
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
