; RUN: not feme-opt --llvm -passes=feme-cpu-reference-lower-builtins -S %s 2>&1 | FileCheck %s

; Every wave op (not just `WaveGetLaneIndex`) is diagnosed under
; `--reference`: it has no meaning one invocation at a time, which is
; exactly what `--reference` runs (see the "CFG restructurization test
; suite" section of feme/docs/FeMeCPUDesign.md).

; CHECK: error: feme-cpu-reference-lower-builtins: function 'main' uses a wave intrinsic ('llvm.dx.wave.any')
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %pred = icmp eq i32 %tid, 0
  %any = call i1 @llvm.dx.wave.any(i1 %pred)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i1 @llvm.dx.wave.any(i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
