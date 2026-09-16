; RUN: feme-opt --llvm -passes='print<feme-cpu-uniformity>' -o %t.bc %s | FileCheck %s

; Exercises `feme::cpu::WaveTTIImpl` end to end through the
; `print<feme-cpu-uniformity>` printer (see "Phase 2: Uniformity Analysis" in
; feme/docs/FeMeCPUDesign.md): one function per scenario, covering the
; classification described there -- lane-varying builtins are divergence
; sources, `WaveActive*` reductions and DXIL's `WaveReadLaneAt` are always
; uniform, SPIR-V's broader shuffle-style read is uniform only when every
; operand is (see its own scenarios below), and ordinary values are
; divergent only if they transitively depend on a divergent one (including
; through a divergent branch's control dependence).

declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.flattened.thread.id.in.group()
declare i32 @llvm.dx.wave.getlaneindex()
declare i32 @llvm.dx.wave.prefix.usum.i32(i32)
declare i32 @llvm.dx.wave.reduce.usum.i32(i32)
declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
declare i32 @llvm.spv.wave.readlane.i32(i32, i32)

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

; DXIL's WaveReadLaneAt broadcasts a single, uniformly-indexed lane's value
; to the whole wave, so its result is uniform even though the broadcast
; value operand is itself divergent -- HLSL's language rule guarantees the
; lane-index operand is dynamically uniform, so `feme::cpu::WaveTTIImpl`
; classifies `dx_wave_readlane` `AlwaysUniform` regardless of its other
; operand (see its own comment; `combined.hlsl`'s `WaveReadLaneAt(sum, 0)`,
; where `sum` is a divergent per-lane accumulation, depends on this).
; CHECK-LABEL: WaveUniformityInfo for function 'wave_read_lane_broadcasts_divergent_value_uniformly':
define void @wave_read_lane_broadcasts_divergent_value_uniformly() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK-NOT: DIVERGENT:{{.*}}%bcast = call i32 @llvm.dx.wave.readlane
  %bcast = call i32 @llvm.dx.wave.readlane.i32(i32 %id, i32 0)
  ret void
}

; SPIR-V's `OpGroupNonUniformShuffle` (which `spv_wave_readlane` also
; covers, see `WaveCallKind::ReadLane` in WaveCalls.h) has no such
; uniform-index language guarantee, so it is deliberately left off the
; `AlwaysUniform` list -- the generic operand-divergence rule applies
; instead, conservative but sound: divergent whenever either operand is,
; including (unlike the DXIL case above) a divergent value read through a
; uniform index.
; CHECK-LABEL: WaveUniformityInfo for function 'spirv_wave_read_lane_of_divergent_value_is_divergent':
define void @spirv_wave_read_lane_of_divergent_value_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%id = call i32 @llvm.dx.thread.id
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK: DIVERGENT:{{.*}}%bcast = call i32 @llvm.spv.wave.readlane
  %bcast = call i32 @llvm.spv.wave.readlane.i32(i32 %id, i32 0)
  ret void
}

; A varying lane index makes SPIR-V's shuffle-style read divergent too --
; the case this classification exists to get right (a genuinely per-lane
; gather, not a broadcast; see WaveLowering.cpp's `lowerReadLane`).
; CHECK-LABEL: WaveUniformityInfo for function 'spirv_wave_read_lane_of_varying_lane_is_divergent':
define void @spirv_wave_read_lane_of_varying_lane_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%lane = call i32 @llvm.dx.wave.getlaneindex
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  ; CHECK: DIVERGENT:{{.*}}%v = call i32 @llvm.spv.wave.readlane.i32(i32 1, i32 %lane)
  %v = call i32 @llvm.spv.wave.readlane.i32(i32 1, i32 %lane)
  ret void
}

; With every operand uniform, SPIR-V's shuffle-style read is uniform too.
; CHECK-LABEL: WaveUniformityInfo for function 'spirv_wave_read_lane_of_uniform_operands_is_uniform':
define void @spirv_wave_read_lane_of_uniform_operands_is_uniform() {
  ; CHECK-NOT: DIVERGENT:{{.*}}%v = call i32 @llvm.spv.wave.readlane
  %v = call i32 @llvm.spv.wave.readlane.i32(i32 1, i32 0)
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

; feme.cpu.mask.any is always uniform, even though its operand is
; necessarily divergent (see "Mask representation between phases" in
; feme/docs/FeMeCPUDesign.md): it stands in for a cross-lane reduction that
; `feme::cpu::SIMDizePass` lowers to `llvm.vector.reduce.or`, which is by
; definition the same on every lane. This is what lets
; `feme::cpu::LinearizePass`'s mask-gated loop backedge be widened as a
; uniform branch (roadmap milestone 7).
; CHECK-LABEL: WaveUniformityInfo for function 'mask_any_is_uniform':
define void @mask_any_is_uniform() {
  %id = call i32 @llvm.dx.thread.id(i32 0)
  ; CHECK: DIVERGENT:{{.*}}%active = icmp
  %active = icmp eq i32 %id, 0
  ; CHECK-NOT: DIVERGENT:{{.*}}%any = call i1 @feme.cpu.mask.any
  %any = call i1 @feme.cpu.mask.any(i1 %active)
  ; CHECK-NOT: DIVERGENT:{{.*}}br i1 %any
  br i1 %any, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
declare i1 @feme.cpu.mask.any(i1)

; feme.cpu.masked.atomicrmw.* (roadmap milestone L41) is always divergent,
; even when every one of its own operands is uniform: it stands in for one
; genuine, real per-lane atomic read-modify-write (see
; `feme::cpu::FunctionWidener::widenMaskedAtomicRMW`, Transforms/CPU/
; SIMDize.cpp), whose result differs by lane by construction -- e.g. every
; lane racing to increment one shared counter via a uniform pointer,
; uniform value and uniform (always-true) mask still gets a genuinely
; different pre-increment value back. Before this fix, an all-uniform-
; operand call site like this one was wrongly classified `Default` (hence
; uniform), which left a real, load-bearing consumer branch (deciding
; whether *this* lane is one of the first `N` to get a slot -- the exact
; shape a real CTS mesh-shader "allocate a unique output slot" pattern
; hits) unwidened too, so `feme::cpu::FunctionWidener::widen`'s final
; "sever remaining uses of an erased instruction" fallback silently
; substituted `poison` for the read instead of a real per-lane value.
; CHECK-LABEL: WaveUniformityInfo for function 'masked_atomicrmw_is_divergent':
define void @masked_atomicrmw_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%old = call i32 @feme.cpu.masked.atomicrmw.i32
  %old = call i32 @feme.cpu.masked.atomicrmw.i32(i32 1, ptr @g, i32 1, i32 4, i1 true)
  ; CHECK: DIVERGENT:{{.*}}%cond = icmp
  %cond = icmp ult i32 %old, 32
  ; CHECK: DIVERGENT:{{.*}}br i1 %cond
  br i1 %cond, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
@g = global i32 0
declare i32 @feme.cpu.masked.atomicrmw.i32(i32, ptr, i32, i32, i1)

; Roadmap L43: a plain, real (not-yet-lowered) `atomicrmw` -- the shape
; `feme::cpu::UniformityInfo` actually sees, since it is computed once, up
; front, in `feme::cpu::LinearizePass::run`, strictly before
; `feme::cpu::DiamondFlattener`'s own `applyStageMasks` ever converts a real
; `atomicrmw` into the `feme.cpu.masked.atomicrmw.*` call form
; `masked_atomicrmw_is_divergent` above already covers -- needs the exact
; same `NeverUniform` treatment, and for the same reason: every one of its
; own operands can be uniform (a uniform pointer, value, and unconditional
; reach) while its own per-lane result still genuinely differs by
; construction. Before this fix, a plain `atomicrmw` fell through to
; `Default` (uniform, since every operand is), leaving a real, load-bearing
; consumer branch -- e.g. a real CTS mesh shader's own "allocate a unique
; output slot" `icmp`/`br` -- wrongly classified uniform too, so
; `feme::cpu::DiamondFlattener` never attempted to flatten it at all,
; leaving the genuine divergent branch in place for `feme::cpu::SIMDizePass`
; to reject later as an unremoved divergent branch (roadmap milestone L43).
; CHECK-LABEL: WaveUniformityInfo for function 'atomicrmw_is_divergent':
define void @atomicrmw_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%old = atomicrmw add ptr @g2, i32 1
  %old = atomicrmw add ptr @g2, i32 1 seq_cst
  ; CHECK: DIVERGENT:{{.*}}%cond = icmp
  %cond = icmp ult i32 %old, 32
  ; CHECK: DIVERGENT:{{.*}}br i1 %cond
  br i1 %cond, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
@g2 = global i32 0

; Roadmap H152: `feme.cpu.resource.atomic.*`/`feme.cpu.image.atomic.*` --
; the resource-heap runtime-call family `feme::cpu::SPIRVResourceLoweringPass`
; emits for every `RWStructuredBuffer`/`RWByteAddressBuffer`/`RWBuffer`/
; `RWTexture*`-destination `InterlockedAdd`/`InterlockedCompareExchange`/etc,
; as opposed to a groupshared destination's plain `AtomicRMWInst` --
; needs the exact same `NeverUniform` treatment as `atomicrmw_is_divergent`/
; `masked_atomicrmw_is_divergent` above, for the identical reason: dispatch
; is sequential, so each lane's own real atomic op observes whatever the
; resource holds at that lane's own turn, not one shared answer every lane
; agrees on, regardless of how uniform this call's own operands (heap
; handle, binding, offset, compare/exchange values) happen to be. Before
; this fix, a call site whose every operand was uniform (exactly this
; case, and the shape `Feature/HLSLLib/InterlockedCompareExchange.
; resources.32.test` hits) fell through to `Default` (uniform, since
; every operand is), leaving a real, load-bearing consumer branch (part
; of that test's own short-circuit `&&`-chain) wrongly classified uniform
; too, so `feme::cpu::DiamondFlattener` never attempted to flatten it,
; leaving the genuine divergent branch in place for `feme::cpu::
; SIMDizePass` to reject later as an unremoved divergent branch.
; CHECK-LABEL: WaveUniformityInfo for function 'resource_atomic_compare_exchange_is_divergent':
define void @resource_atomic_compare_exchange_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%old = call i32 @feme.cpu.resource.atomic.compare_exchange.raw.i32
  %old = call i32 @feme.cpu.resource.atomic.compare_exchange.raw.i32(ptr @heap, i32 0, i32 4, i32 10, i32 0)
  ; CHECK: DIVERGENT:{{.*}}%cond = icmp
  %cond = icmp eq i32 %old, 9
  ; CHECK: DIVERGENT:{{.*}}br i1 %cond
  br i1 %cond, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
@heap = global ptr null
declare i32 @feme.cpu.resource.atomic.compare_exchange.raw.i32(ptr, i32, i32, i32, i32)

; The `feme.cpu.image.atomic.*` sibling family (texture/image-destination
; atomics) needs the identical treatment, for the same reason.
; CHECK-LABEL: WaveUniformityInfo for function 'image_atomic_exchange_is_divergent':
define void @image_atomic_exchange_is_divergent() {
  ; CHECK: DIVERGENT:{{.*}}%old = call i32 @feme.cpu.image.atomic.exchange.2d.i32
  %old = call i32 @feme.cpu.image.atomic.exchange.2d.i32(ptr @imgheap, i32 0, i32 0, i32 0, i32 5)
  ; CHECK: DIVERGENT:{{.*}}%cond = icmp
  %cond = icmp eq i32 %old, 9
  ; CHECK: DIVERGENT:{{.*}}br i1 %cond
  br i1 %cond, label %if_true, label %if_false

if_true:
  br label %exit

if_false:
  br label %exit

exit:
  ret void
}
@imgheap = global ptr null
declare i32 @feme.cpu.image.atomic.exchange.2d.i32(ptr, i32, i32, i32, i32)
