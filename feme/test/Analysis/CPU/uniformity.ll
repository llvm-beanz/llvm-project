; RUN: feme-opt --llvm -passes='print<feme-cpu-uniformity>' -o %t.bc %s | FileCheck %s

; Exercises `feme::cpu::WaveTTIImpl` end to end through the
; `print<feme-cpu-uniformity>` printer (see "Phase 2: Uniformity Analysis" in
; feme/docs/FeMeCPUDesign.md): a lane-varying builtin (`llvm.dx.thread.id`)
; and everything control- or data-dependent on it are divergent, a
; `WaveActive*` reduction over a divergent operand is uniform (it reduces
; over the whole wave), and a value with no divergent inputs at all is
; uniform.

; CHECK-LABEL: WaveUniformityInfo for function 'main':
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.reduce.usum.i32(i32)

define void @main(i32 %uniform_arg) {
entry:
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK: DIVERGENT:{{.*}}%cond = icmp
  %cond = icmp eq i32 %id, 0
  ; CHECK: DIVERGENT:{{.*}}br i1 %cond
  br i1 %cond, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ; A phi merging constants along a divergent branch's arms is itself
  ; divergent, even though neither incoming value is.
  ; CHECK: DIVERGENT:{{.*}}%merged = phi
  %merged = phi i32 [ 1, %if_true ], [ 2, %if_false ]

  ; WaveActiveUSum reduces over the whole wave, so its result is uniform
  ; even though its operand (a thread id) is divergent.
  ; CHECK-NOT: DIVERGENT:{{.*}}%wavesum
  %wavesum = call i32 @llvm.dx.wave.reduce.usum.i32(i32 %id)

  ; A value with no divergent inputs at all is uniform.
  ; CHECK-NOT: DIVERGENT:{{.*}}%plain
  %plain = add i32 %uniform_arg, 1
  ret void
}
