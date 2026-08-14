; RUN: not feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; `nand` (like `uincwrap`/`udecwrap`, not exercised here since none of them
; arise from HLSL's `Interlocked*` builtins -- see
; `feme::cpu::getAtomicRMWIdentity`'s comment) has no identity element `Id`
; with `nand(old, Id) == old` for every `old`, so a masked-off lane's
; contribution cannot be neutralized in place the way
; `feme/test/Transforms/CPU/simdize-scalarize-atomic-masked.ll`'s `add` can.
; Diagnosed rather than silently computing the wrong answer for a masked-off
; lane.

; CHECK: feme-cpu-simdize: function 'main' has a divergent atomicrmw 'nand' with no maskable identity element
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %old = atomicrmw nand ptr @g, i32 1 monotonic
  br label %end
f:
  br label %end
end:
  ret void
}
@g = global i32 0
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
