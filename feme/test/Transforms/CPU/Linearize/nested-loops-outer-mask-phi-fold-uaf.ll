; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L440: a real, captured pre-`feme-cpu-linearize` IR reduction of
; `WaveOps/WaveReadLaneFirst.fp16.test` (two nested uniform-trip-count
; `for` loops, the outer with no internal masking need at all, the inner
; with a genuinely divergent per-lane `if` guarding four
; `WaveReadLaneFirst`-style wave-readlane calls). This reproduces a real,
; nondeterministic (ASLR-dependent) use-after-free found via a live `gdb`
; repro of the real compiler pipeline crashing anywhere from
; `verifyModule` to bitcode writing, depending entirely on allocator-reuse
; timing -- confirmed independently, outside the full Vulkan pipeline, by
; running this exact reduced IR through a standalone `feme-opt
; -passes=feme-cpu-linearize` loop under `gdb`'s `disable-randomization
; off` (required; `gdb`'s default of disabling ASLR hides the bug
; entirely): roughly 1-in-15 to 1-in-75 iterations crashed with signatures
; ranging from a corrupted operand on a `ballot.pred.masked` `and`, to
; `Value::doRAUW`'s "different type" assertion, to
; `ValueHandleBase::AddToUseList`'s "HasValueHandle and ValueHandles out
; of sync" assertion, to a `SIGSEGV` deep inside the bitcode writer
; reading a garbage `Type *`/`Value *` -- all different downstream
; symptoms of the same root cause.
;
; Root cause: `LoopLinearizer::linearizeCycle`'s `addLatchIncoming` calls
; `foldNowTriviallyConstantMaskPhis` once both of a cycle's own
; `active.live`/`active.sideeffect` mask phis' incoming edges are finally
; known, folding either phi away entirely (down to a plain constant, via
; this function's own closure walk and fixed-point loop) when a cycle's
; masking, like this reduction's *outer* loop (it has no internal
; break/discard/demote/wave-reduce call that needs masking at all; only
; the genuinely divergent *inner* loop's conditional store does), turns
; out to never need to become non-constant at all. Once folded, every
; `Instruction` the fold loop erases is erased *immediately* after being
; recorded as a `Replacements` key -- so by the time `foldNow...`'s own
; final "chase every one of `MaskPhis` through `Replacements` to its
; final folded value" loop runs, a `MaskPhis` entry that was itself
; folded away is already a dangling pointer to freed memory. That loop's
; old form, `while (auto *I = dyn_cast<Instruction>(V)) { ... }`, called
; `dyn_cast`'s own `getValueID()` on `V` *before* ever checking whether
; `V`'s own pointer value is itself a (definitely-already-erased)
; `Replacements` key -- a genuine, reproduced use-after-free read the
; instant `V` (starting as one of `MaskPhis` itself) names an
; already-erased instruction. Fixed by looking the pointer *value* up in
; `Replacements` directly (a `DenseMap<Instruction *, Value *>` lookup
; only ever hashes/compares the bit pattern it is given, never
; dereferencing it), which can only "hit" when `V` really is one of those
; exact, already-erased keys, making it exactly as precise as the
; `dyn_cast` it replaces without ever reading through a freed pointer to
; get there.
;
; This test does not assert on the crash itself (inherently
; nondeterministic, and this lit test runs without `gdb`/ASLR
; instrumentation) -- it instead confirms the pass completes (a pre-fix
; build could abort partway through, under the right allocator-reuse
; timing, well before `FileCheck` ever sees output) and produces the
; exact, valid masking structure expected: the *outer* loop's own
; `active.live`/`active.sideeffect` phis fold away entirely (no masking
; ever appears in the outer loop's own body), while the *inner* loop's
; own phis remain genuinely non-constant (narrowed by the inner `if`'s
; own divergent condition), which is the structural condition that
; triggers the bug this fix addresses in the first place.

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
  %1 = call i32 @llvm.spv.thread.id.in.group.i32(i32 0)
  br label %7

7:                                                ; preds = %Flow1._crit_edge, %0
  %8 = phi i32 [ 0, %0 ], [ %27, %Flow1._crit_edge ]
  %9 = icmp ult i32 %8, 2
  br i1 %9, label %25, label %.Flow1_crit_edge

.Flow1_crit_edge:                                 ; preds = %7
  br label %Flow1

10:                                               ; preds = %34
  %11 = add i32 %8, 1
  br label %Flow1

12:                                               ; preds = %Flow1
  %13 = call <4 x i32> @llvm.spv.subgroup.ballot(i1 true)
  %14 = bitcast <4 x i32> %13 to i128
  %15 = call i32 @llvm.spv.subgroup.size()
  %16 = zext i32 %15 to i128
  %17 = shl i128 1, %16
  %18 = sub i128 %17, 1
  %19 = icmp uge i128 %16, 128
  %20 = select i1 %19, i128 -1, i128 %18
  %21 = and i128 %14, %20
  %22 = call i128 @llvm.cttz.i128(i128 %21, i1 false)
  %23 = trunc i128 %22 to i32
  %24 = call <4 x half> @llvm.spv.wave.readlane.v4f16(<4 x half> <half 1.000000e+00, half 2.000000e+00, half 3.000000e+00, half 4.000000e+00>, i32 %23)
  call void @feme.cpu.resource.store.raw.v4f16(ptr %resource_heap, i32 %resource_heap_count, i32 5, i64 0, <4 x half> %24, i1 true)
  ret void

25:                                               ; preds = %7
  %26 = mul i32 %8, 16
  br label %29

Flow1:                                            ; preds = %.Flow1_crit_edge, %10
  %27 = phi i32 [ %11, %10 ], [ poison, %.Flow1_crit_edge ]
  %28 = phi i1 [ false, %10 ], [ true, %.Flow1_crit_edge ]
  br i1 %28, label %12, label %Flow1._crit_edge

Flow1._crit_edge:                                 ; preds = %Flow1
  br label %7

29:                                               ; preds = %Flow._crit_edge, %25
  %30 = phi i32 [ %53, %Flow._crit_edge ], [ 0, %25 ]
  %31 = icmp ult i32 %30, 4
  br i1 %31, label %35, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %29
  br label %Flow

32:                                               ; preds = %55
  %33 = add i32 %30, 1
  br label %Flow

34:                                               ; preds = %Flow
  br label %10

35:                                               ; preds = %29
  %36 = mul i32 %8, %26
  %37 = mul i32 %30, 4
  %38 = add i32 %36, %37
  %39 = add i32 %38, %1
  %40 = zext i32 %39 to i64
  %41 = mul i64 %40, 8
  %42 = call <4 x half> @feme.cpu.resource.load.raw.v4f16(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %41, i1 true)
  %43 = add i32 %26, %37
  %44 = add i32 %43, %1
  %45 = zext i32 %30 to i64
  %46 = mul i64 %45, 16
  %47 = zext i32 %1 to i64
  %48 = mul i64 %47, 4
  %49 = add i64 0, %48
  %50 = add i64 %46, %49
  %51 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 6, i64 %50, i1 true)
  %52 = icmp ne i32 %51, 0
  br i1 %52, label %56, label %._crit_edge

._crit_edge:                                      ; preds = %35
  br label %55

Flow:                                             ; preds = %.Flow_crit_edge, %32
  %53 = phi i32 [ %33, %32 ], [ poison, %.Flow_crit_edge ]
  %54 = phi i1 [ false, %32 ], [ true, %.Flow_crit_edge ]
  br i1 %54, label %34, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %29

55:                                               ; preds = %._crit_edge, %56
  br label %32

56:                                               ; preds = %35
  %57 = extractelement <4 x half> %42, i32 0
  %58 = call <4 x i32> @llvm.spv.subgroup.ballot(i1 true)
  %59 = bitcast <4 x i32> %58 to i128
  %60 = call i32 @llvm.spv.subgroup.size()
  %61 = zext i32 %60 to i128
  %62 = shl i128 1, %61
  %63 = sub i128 %62, 1
  %64 = icmp uge i128 %61, 128
  %65 = select i1 %64, i128 -1, i128 %63
  %66 = and i128 %59, %65
  %67 = call i128 @llvm.cttz.i128(i128 %66, i1 false)
  %68 = trunc i128 %67 to i32
  %69 = call half @llvm.spv.wave.readlane.f16(half %57, i32 %68)
  %70 = zext i32 %44 to i64
  %71 = mul i64 %70, 2
  call void @feme.cpu.resource.store.raw.f16(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 %71, half %69, i1 true)
  br label %55
}

declare i32 @llvm.spv.thread.id.in.group.i32(i32)
declare <4 x i32> @llvm.spv.subgroup.ballot(i1)
declare i32 @llvm.spv.subgroup.size()
declare i128 @llvm.cttz.i128(i128, i1 immarg)
declare <4 x half> @llvm.spv.wave.readlane.v4f16(<4 x half>, i32)
declare half @llvm.spv.wave.readlane.f16(half, i32)
declare void @feme.cpu.resource.store.raw.v4f16(ptr, i32, i32, i64, <4 x half>, i1)
declare <4 x half> @feme.cpu.resource.load.raw.v4f16(ptr, i32, i32, i64, i1)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
declare void @feme.cpu.resource.store.raw.f16(ptr, i32, i32, i64, half, i1)

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="4,1,1" "hlsl.shader"="compute" }

!feme.cpu.resources = !{!0}
!feme.cpu.bound_resources = !{!1}

!0 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!1 = !{!"main", i32 7, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 0, i32 0, i32 2, i32 1, i32 2, i32 0, i32 0, i32 3, i32 1, i32 3, i32 0, i32 0, i32 4, i32 1, i32 4, i32 0, i32 0, i32 5, i32 1, i32 5, i32 0, i32 0, i32 6, i32 1, i32 6, i32 0}

; CHECK-LABEL: define void @main(
; The outer loop's own header (the block holding the `icmp ult i32 ..., 2`
; trip-count check) must not carry any `active.live`/`active.sideeffect`
; phi at all -- the fix must leave them fully folded away (not
; dangling/garbage), exactly as the pre-fix crash's own corrupted
; downstream consumers (a `ballot.pred.masked` `and`'s operand, a RAUW
; type mismatch, or a bitcode-writer `Type *` read) all eventually
; observed when this fold's own `Result`-chasing loop read freed memory
; instead. Nothing earlier than the inner loop's own, genuine phis
; (confirmed by the `CHECK` lines below) may ever mention `active.live`.
; CHECK-NOT: active.live
;
; The inner loop's own masks, by contrast, must remain genuinely
; non-constant (narrowed by its own divergent `if`), proving this is a
; real masking decision, not a case where masking never appears at all.
; CHECK: %active.live = phi i1
; CHECK: %active.sideeffect = phi i1
