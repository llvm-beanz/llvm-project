; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (Roadmap L275) A raw `atomicrmw`/`cmpxchg` against a task entry's own
; bounded payload variable (address space 14, `@spirv_var_0` here, mirroring
; the un-mangled name SPIR-V import leaves behind) must canonicalize into
; `feme.stage.task.payload.atomicrmw`/`.cmpxchg`, exactly like an ordinary
; load/store through the same global already does -- left unrewritten, it
; survives to JIT-link time still referencing a SPIR-V-derived global feme's
; own CPU target never defines, failing with a raw
; "Symbols not found: [ spirv_var_0 ]" link error (the bug
; `dEQP-VK.glsl.atomic_operations.{add,and,or,xor,min,max}_signed_task_payload`
; and their `_unsigned`/`comp_swap`/`exchange` siblings all hit before this
; fix).

@spirv_var_0 = external addrspace(14) global i32

; CHECK-LABEL: define i32 @add_task_payload(i32 %v)
define i32 @add_task_payload(i32 %v) #0 {
  ; CHECK: call i32 @feme.stage.task.payload.atomicrmw.i32.i32(i32 0, i32 {{[0-9]+}}, i32 %v)
  %old = atomicrmw add ptr addrspace(14) @spirv_var_0, i32 %v seq_cst
  ret i32 %old
}

; A regression test for the `L275` `cmpxchg` canonicalization crash: the
; rewrite below must not corrupt IR or crash regardless of how many
; `ExtractValueInst` users a single `cmpxchg` has, and regardless of how many
; `cmpxchg`s a single function canonicalizes -- both `%old`'s consumers
; (below) come *after* the `cmpxchg` in program order, so eagerly erasing
; them from inside the per-instruction rewrite loop that is still walking
; toward them (rather than deferring their erasure until that loop has
; finished) is a use-after-free of the loop's own cached "next instruction"
; iterator, previously observed as a `SIGSEGV` inside
; `canonicalizeSPIRVStage` on
; `dEQP-VK.glsl.atomic_operations.comp_swap_signed_task_payload`.
; CHECK-LABEL: define { i32, i1 } @comp_swap_task_payload(i32 %cmp, i32 %new)
define { i32, i1 } @comp_swap_task_payload(i32 %cmp, i32 %new) #0 {
  ; CHECK: %[[OLD:.*]] = call i32 @feme.stage.task.payload.cmpxchg.i32.i32(i32 0, i32 %cmp, i32 %new)
  ; CHECK: %[[SUCCESS:.*]] = icmp eq i32 %[[OLD]], %cmp
  %old = cmpxchg ptr addrspace(14) @spirv_var_0, i32 %cmp, i32 %new seq_cst seq_cst
  %oldval = extractvalue { i32, i1 } %old, 0
  %success = extractvalue { i32, i1 } %old, 1
  ; CHECK: %ret0 = insertvalue { i32, i1 } poison, i32 %[[OLD]], 0
  %ret0 = insertvalue { i32, i1 } poison, i32 %oldval, 0
  ; CHECK: %ret1 = insertvalue { i32, i1 } %ret0, i1 %[[SUCCESS]], 1
  %ret1 = insertvalue { i32, i1 } %ret0, i1 %success, 1
  ret { i32, i1 } %ret1
}

attributes #0 = { "feme.shader.stage"="amplification" }
