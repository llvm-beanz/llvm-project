; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent call to an ordinary function (not a resource, wave, builtin or
; elementwise-vectorizable intrinsic call) has no widened form yet -- see
; `feme::cpu::FunctionWidener::widenElementwise`. Diagnosed rather than
; widened, and, like every other mid-widening diagnostic, emitted after
; `buildWidenedFunction` has already erased the pre-widening function, so
; this also covers that the diagnostic does not reach through it.

; CHECK: feme-cpu-simdize: unsupported divergent call to 'foo'
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %r = call i32 @foo(i32 %tid)
  ret void
}
declare i32 @foo(i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
