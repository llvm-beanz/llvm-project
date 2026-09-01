; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A shader entry point takes no parameters of its own -- its inputs arrive
; through stage-IO or resource accesses -- so every parameter of a widened
; wave body belongs to the `feme::cpu::WaveBodyEnv` ABI `feme-cpu-simdize`
; appends (plus the `loopvarN` scalars the wrapper itself adds). An entry
; point that still carries one of its own keeps it ahead of those, and
; `feme::cpu::EntryWrapperPass` has no argument to supply for it when it
; builds the wave-body call.
;
; That must be diagnosed -- turned into a clean pipeline failure by
; `feme::cpu::runPipeline`'s `ErrorDiagnosticGuard` -- rather than reaching
; the `llvm_unreachable` in that call's own parameter dispatch, which
; crashed the process instead (found through the DXIL round-trip in
; feme/test/Tools/feme/feme-cpu-entry-point-parameter.ll).

; CHECK: error: feme-cpu-wrap-entry: function 'main' has an unsupported parameter 'n'; a shader entry point takes no parameters of its own
define void @main(i32 %n) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %sum = add i32 %tid, %n
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
