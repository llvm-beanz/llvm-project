; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L452(a): `MaskedAllocas` (see `simdize-masked-alloca-gep-private.ll`
; and `FunctionWidener::collectMaskedAllocas`'s own comment) already forces a
; load through a uniformly-indexed local array back into a real, per-lane
; vector when the array's own *value* was written divergently earlier (here,
; `%result`'s four slots are each unconditionally seeded with the thread ID,
; a genuinely per-lane value, through a uniform address). That alone is not
; enough: `%sum`'s own `fadd` consumes that load's result under control flow
; that depends only on the uniform loop counter, so plain `UniformityInfo`
; (which has no memory model and cannot see the divergent store that landed
; through `%result`'s uniform address) still calls `%sum` itself uniform. Left
; unfixed, the generic widening gate takes the "uniform: leave it alone" path
; for `%sum`, which silently collapses the real per-lane vector down to one
; lane's value and broadcasts that single value back into every lane's own
; storage slot (reduced from a real `dEQP-VK.spirv_assembly.instruction.
; graphics.opphi.out_of_order_vert` full-framebuffer-corruption failure).
; `FunctionWidener::ForcedDivergentValues`/`computeForcedDivergentValues`
; closes this gap by propagating memory-forced divergence forward along
; ordinary SSA use edges, so `%sum` (and anything using it) is treated as
; divergent too, and a real per-lane `fadd`/four distinct stores reach
; `%result`'s storage, not one broadcast value.

; CHECK-LABEL: define void @main(
; CHECK: %result.perlane = alloca [4 x [4 x float]]
; CHECK-NOT: alloca [4 x float]
; CHECK: loop:
; CHECK: %addr.wide = getelementptr [4 x float], <4 x ptr> %result.perlane.ptrs{{[0-9]*}}, i32 0, i32 %i
; CHECK: merge:
; CHECK: %operand.splat.splat = shufflevector <4 x float>
; CHECK: %sum.wide = fadd <4 x float> %{{[0-9]+}}, %operand.splat.splat
; CHECK-NEXT: %{{.*lane0.*}} = extractelement <4 x ptr> %addr.wide, i32 0
; CHECK-NEXT: %{{[0-9]+}} = extractelement <4 x float> %sum.wide, i32 0
; CHECK-NEXT: store float %{{[0-9]+}}, ptr %{{.*lane0.*}}
; CHECK: %{{[0-9]+}} = extractelement <4 x float> %sum.wide, i32 1
; CHECK: %{{[0-9]+}} = extractelement <4 x float> %sum.wide, i32 2
; CHECK: %{{[0-9]+}} = extractelement <4 x float> %sum.wide, i32 3
define void @main(ptr %out) #0 {
entry:
  %result = alloca [4 x float]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = uitofp i32 %tid to float
  ; Unconditional (uniform control flow), but per-lane-divergent-*value*,
  ; seed store into every slot -- exactly the H170 shape
  ; `collectMaskedAllocas` already scans for.
  %r0 = getelementptr [4 x float], ptr %result, i32 0, i32 0
  store float %tidf, ptr %r0
  %r1 = getelementptr [4 x float], ptr %result, i32 0, i32 1
  store float %tidf, ptr %r1
  %r2 = getelementptr [4 x float], ptr %result, i32 0, i32 2
  store float %tidf, ptr %r2
  %r3 = getelementptr [4 x float], ptr %result, i32 0, i32 3
  store float %tidf, ptr %r3
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %addr = getelementptr [4 x float], ptr %result, i32 0, i32 %i
  %val = load float, ptr %addr
  ; A uniform (loop-counter-only) branch building a uniform `%operand` --
  ; deliberately not thread-ID-derived, to isolate this test from any
  ; (correct, unrelated) divergent-branch widening.
  %cmp = icmp eq i32 %i, 0
  br i1 %cmp, label %a, label %b
a:
  br label %merge
b:
  br label %merge
merge:
  %operand = phi float [1.0, %a], [2.0, %b]
  %sum = fadd float %val, %operand
  store float %sum, ptr %addr
  br label %latch
latch:
  %inc = add i32 %i, 1
  %cond = icmp slt i32 %inc, 4
  br i1 %cond, label %loop, label %exit
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
