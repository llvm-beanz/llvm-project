; RUN: not feme-opt --llvm -passes=feme-cpu-reference-lower-builtins -S %s 2>&1 | FileCheck %s

; A wave intrinsic (here `WaveGetLaneIndex`) is diagnosed rather than
; silently mis-lowered: it has no meaning one invocation at a time, which
; is exactly what `--reference` runs (see the "CFG restructurization test
; suite" section of feme/docs/FeMeCPUDesign.md).

; CHECK: error: feme-cpu-reference-lower-builtins: function 'main' uses a wave intrinsic
define void @main() #0 {
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  ret void
}
declare i32 @llvm.dx.wave.getlaneindex()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
