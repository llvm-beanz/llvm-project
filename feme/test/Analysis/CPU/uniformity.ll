; RUN: feme-opt --llvm -passes='print<feme-cpu-uniformity>' -o %t.bc %s | FileCheck %s

; Exercises `feme::cpu::WaveTTIImpl` end to end through the
; `print<feme-cpu-uniformity>` printer (see "Phase 2: Uniformity Analysis" in
; feme/docs/FeMeCPUDesign.md): one function per scenario, covering the
; classification described there -- lane-varying builtins are divergence
; sources, `WaveActive*`/`WaveReadLaneAt`-style reductions are uniform, and
; ordinary values are divergent only if they transitively depend on a
; divergent one (including through a divergent branch's control dependence).

declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.flattened.thread.id.in.group()
declare i32 @llvm.dx.wave.getlaneindex()
declare i32 @llvm.dx.wave.prefix.usum.i32(i32)
declare i32 @llvm.dx.wave.reduce.usum.i32(i32)
declare i32 @llvm.dx.wave.readlane.i32(i32, i32)

; llvm.dx.thread.id is a per-lane divergence source.
; CHECK-LABEL: WaveUniformityInfo for function 'thread_id_is_divergent':
define void @thread_id_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ret void
}

; llvm.dx.flattened.thread.id.in.group is likewise a per-lane divergence
; source.
; CHECK-LABEL: WaveUniformityInfo for function 'flattened_thread_id_in_group_is_divergent':
define void @flattened_thread_id_in_group_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.flattened.thread.id.in.group
  %id = call i32 @llvm.dx.flattened.thread.id.in.group()
  ret void
}

; llvm.dx.wave.getlaneindex is a per-lane divergence source.
; CHECK-LABEL: WaveUniformityInfo for function 'wave_get_lane_index_is_divergent':
define void @wave_get_lane_index_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%lane = call i32 @llvm.dx.wave.getlaneindex
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  ret void
}

; WavePrefixUSum reduces over "lanes before mine", which differs per lane, so
; it is a divergence source.
; CHECK-LABEL: WaveUniformityInfo for function 'wave_prefix_sum_is_divergent':
define void @wave_prefix_sum_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%sum = call i32 @llvm.dx.wave.prefix.usum
  %sum = call i32 @llvm.dx.wave.prefix.usum.i32(i32 1)
  ret void
}

; WaveActiveUSum reduces over the whole wave, so its result is uniform even
; though its operand (a thread id) is divergent.
; CHECK-LABEL: WaveUniformityInfo for function 'wave_active_reduction_is_uniform':
define void @wave_active_reduction_is_uniform() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK-NOT: DIVERGENT:{{.*}}%sum = call i32 @llvm.dx.wave.reduce.usum
  %sum = call i32 @llvm.dx.wave.reduce.usum.i32(i32 %id)
  ret void
}

; WaveReadLaneAt broadcasts a single lane's value to the whole wave, so its
; result is uniform even though the broadcast value is itself divergent.
; CHECK-LABEL: WaveUniformityInfo for function 'wave_read_lane_is_uniform':
define void @wave_read_lane_is_uniform() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK-NOT: DIVERGENT:{{.*}}%bcast = call i32 @llvm.dx.wave.readlane
  %bcast = call i32 @llvm.dx.wave.readlane.i32(i32 %id, i32 0)
  ret void
}

; A value with no divergent inputs at all is uniform.
; CHECK-LABEL: WaveUniformityInfo for function 'constant_is_uniform':
define void @constant_is_uniform() {
  ; CHECK-NOT: DIVERGENT:{{.*}}%c = add
  %c = add i32 1, 1
  ret void
}

; A value computed from a divergent thread id should itself diverge.
; CHECK-LABEL: WaveUniformityInfo for function 'value_dependent_on_divergent_value_is_divergent':
define void @value_dependent_on_divergent_value_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK: DIVERGENT:{{.*}}%doubled = add i32 %id, %id
  %doubled = add i32 %id, %id
  ret void
}

; A phi merging constants along a divergent branch's arms is itself
; divergent, even though neither incoming value is itself divergent.
; CHECK-LABEL: WaveUniformityInfo for function 'divergent_branch_makes_phi_divergent':
define void @divergent_branch_makes_phi_divergent() {
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
  ; CHECK: DIVERGENT:{{.*}}%merged = phi
  %merged = phi i32 [ 1, %if_true ], [ 2, %if_false ]
  ret void
}

; A branch on a uniform value should not make its phi divergent.
; CHECK-LABEL: WaveUniformityInfo for function 'uniform_branch_keeps_phi_uniform':
define void @uniform_branch_keeps_phi_uniform(i32 %cond) {
entry:
  ; CHECK-NOT: DIVERGENT:{{.*}}%c = icmp
  %c = icmp eq i32 %cond, 0
  ; CHECK-NOT: DIVERGENT:{{.*}}br i1 %c
  br i1 %c, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ; CHECK-NOT: DIVERGENT:{{.*}}%merged = phi
  %merged = phi i32 [ 1, %if_true ], [ 2, %if_false ]
  ret void
}
