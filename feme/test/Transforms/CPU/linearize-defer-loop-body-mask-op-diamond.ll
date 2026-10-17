; RUN: split-file %s %t
; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %t/shader.ll -o %t/lowered.ll
; RUN: clang -O0 -c %t/lowered.ll -o %t/lowered.o
; RUN: clang -O0 %t/lowered.o %t/harness.c -o %t/run
; RUN: %t/run | FileCheck %s

; Roadmap L442/L447/L448 regression test: a nested-loop shape (outer `for
; (i = 0; i < 40; ++i)`, inner `for (j = 0; j < 4; ++j)`) whose inner loop
; body contains a divergent `if`/`elseif` diamond, each arm performing a
; side-effecting `feme.cpu.resource.atomic.add.raw.i32` call -- the exact
; shape `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.
; max_workgroup_memory.*` reduces to (see `/tmp/l442_min` in prior session
; notes; `shader.ll` below is that reduction's `shrink_prelin.ll`,
; unmodified). `StructurizeCFG` lays this loop out with its real
; header/exit-check dispatch blocks (`Flow21`/`Flow22`) placed *after* the
; loop body in the function's textual/walk order, rather than before it.
;
; Before the `L448` fix, `DiamondFlattener::run()`'s single, whole-function
; `validate()`/`flatten()` walk reached this `if`/`elseif` diamond (the
; `br i1 %44, ...` in block `39` below) before `LoopLinearizer` ever ran,
; and flattened it using only a blind, unconditionally-"true" mask -- not
; the inner loop's own real per-iteration bound -- permanently baking in a
; wrong mask that `narrowCallMask`'s one-shot guard then refused to ever
; correct. The net effect: thread 0's inner `j` loop ran one extra,
; out-of-bounds "ghost" iteration (`j == 4`) whose atomic fired ungated by
; the loop's own bound, double-counting every multiple-of-4 output slot.
;
; `L448` makes that whole-function walk defer (skip straight past) any
; in-cycle divergent diamond containing a side-effecting resource/image
; call, exactly like `LoopLinearizer::linearizeCycle`'s own
; `flattenLoopBodyDiamond` already does, so `LoopLinearizer` gets the
; chance to mask it with the real, loop-bound-narrowed mask instead.
;
; A purely textual (`FileCheck`-on-IR) regression test for this bug was
; tried first and discarded: the buggy, pre-`L448` code already produces a
; real (non-constant) SSA value for both atomic calls' mask operands at
; the `feme-cpu-linearize`-only IR level -- the computed mask is simply
; *wrong* (missing the inner loop's own bound term), not literally the
; constant `i1 true` placeholder, so no IR-text pattern reliably
; distinguishes the two. This test instead JITs/compiles the actual
; lowered IR to a real object, links it against a minimal C harness
; providing `feme.cpu.resource.atomic.add.raw.i32`, executes it, and
; checks the resulting heap buffer for the exact doubling this bug caused
; (confirmed both ways: this harness shows `MISMATCH-COUNT=39` with the
; `L448` fix reverted, and `ALL-160-CORRECT` with it applied).

; CHECK: ALL-160-CORRECT

;--- shader.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

@spirv_var_89.str = private constant [13 x i8] c"spirv_var_89\00"
@spirv_var_75 = internal addrspace(3) global [40 x <4 x i32>] zeroinitializer

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.thread.id.in.group.i32(i32) #0

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 {
  %1 = call i32 @llvm.spv.thread.id.in.group.i32(i32 0)
  %2 = insertelement <3 x i32> poison, i32 %1, i32 0
  %3 = call i32 @llvm.spv.thread.id.in.group.i32(i32 1)
  %4 = insertelement <3 x i32> %2, i32 %3, i32 1
  %5 = call i32 @llvm.spv.thread.id.in.group.i32(i32 2)
  %6 = insertelement <3 x i32> %4, i32 %5, i32 2
  %7 = mul i32 %5, 16
  %8 = mul i32 %7, 1
  %9 = call i32 @llvm.spv.thread.id.in.group.i32(i32 0)
  %10 = insertelement <3 x i32> poison, i32 %9, i32 0
  %11 = call i32 @llvm.spv.thread.id.in.group.i32(i32 1)
  %12 = insertelement <3 x i32> %10, i32 %11, i32 1
  %13 = call i32 @llvm.spv.thread.id.in.group.i32(i32 2)
  %14 = insertelement <3 x i32> %12, i32 %13, i32 2
  %15 = mul i32 %11, 16
  %16 = call i32 @llvm.spv.thread.id.in.group.i32(i32 0)
  %17 = insertelement <3 x i32> poison, i32 %16, i32 0
  %18 = call i32 @llvm.spv.thread.id.in.group.i32(i32 1)
  %19 = insertelement <3 x i32> %17, i32 %18, i32 1
  %20 = call i32 @llvm.spv.thread.id.in.group.i32(i32 2)
  %21 = insertelement <3 x i32> %19, i32 %20, i32 2
  %22 = add i32 %16, %15
  %23 = add i32 %22, %8
  br label %24

24:                                               ; preds = %Flow22._crit_edge, %0
  %.0 = phi i32 [ 0, %0 ], [ %31, %Flow22._crit_edge ]
  br label %28

25:                                               ; preds = %Flow22
  ret void

26:                                               ; preds = %34
  %27 = add i32 %.0, 1
  br label %Flow22

28:                                               ; preds = %24
  %29 = icmp ult i32 %.0, 40
  br i1 %29, label %30, label %.Flow22_crit_edge

.Flow22_crit_edge:                                ; preds = %28
  br label %Flow22

30:                                               ; preds = %28
  br label %33

Flow22:                                           ; preds = %.Flow22_crit_edge, %26
  %31 = phi i32 [ %27, %26 ], [ poison, %.Flow22_crit_edge ]
  %32 = phi i1 [ false, %26 ], [ true, %.Flow22_crit_edge ]
  br i1 %32, label %25, label %Flow22._crit_edge

Flow22._crit_edge:                                ; preds = %Flow22
  br label %24

33:                                               ; preds = %Flow21._crit_edge, %30
  %.020 = phi i32 [ %45, %Flow21._crit_edge ], [ 0, %30 ]
  br label %37

34:                                               ; preds = %Flow21
  br label %26

35:                                               ; preds = %47
  %36 = add i32 %.020, 1
  br label %Flow21

37:                                               ; preds = %33
  %38 = icmp ult i32 %.020, 4
  br i1 %38, label %39, label %.Flow21_crit_edge

.Flow21_crit_edge:                                ; preds = %37
  br label %Flow21

39:                                               ; preds = %37
  %40 = mul i32 %.0, 4
  %41 = add i32 %40, %.020
  %42 = getelementptr [40 x <4 x i32>], ptr addrspace(3) @spirv_var_75, i32 0, i32 %.0, i32 %.020
  %43 = load i32, ptr addrspace(3) %42, align 4
  %44 = icmp ne i32 %23, %41
  br i1 %44, label %54, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %39
  br label %Flow

Flow21:                                           ; preds = %.Flow21_crit_edge, %35
  %45 = phi i32 [ %36, %35 ], [ poison, %.Flow21_crit_edge ]
  %46 = phi i1 [ false, %35 ], [ true, %.Flow21_crit_edge ]
  br i1 %46, label %34, label %Flow21._crit_edge

Flow21._crit_edge:                                ; preds = %Flow21
  br label %33

47:                                               ; preds = %Flow._crit_edge, %48
  br label %35

48:                                               ; preds = %Flow
  %49 = zext i32 %23 to i64
  %50 = mul i64 %49, 4
  %51 = icmp eq i32 %43, 0
  %52 = select i1 %51, i32 1, i32 0
  %53 = call i32 @feme.cpu.resource.atomic.add.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %50, i32 %52, i1 true)
  br label %47

54:                                               ; preds = %39
  %55 = icmp eq i32 %23, 0
  %56 = icmp uge i32 %41, 16
  %57 = and i1 %55, %56
  br i1 %57, label %60, label %._crit_edge

._crit_edge:                                      ; preds = %54
  br label %59

Flow:                                             ; preds = %.Flow_crit_edge, %59
  %58 = phi i1 [ false, %59 ], [ true, %.Flow_crit_edge ]
  br i1 %58, label %48, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %47

59:                                               ; preds = %._crit_edge, %60
  br label %Flow

60:                                               ; preds = %54
  %61 = zext i32 %41 to i64
  %62 = mul i64 %61, 4
  %63 = icmp eq i32 %43, 0
  %64 = select i1 %63, i32 1, i32 0
  %65 = call i32 @feme.cpu.resource.atomic.add.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %62, i32 %64, i1 true)
  br label %59
}

; Function Attrs: nounwind willreturn memory(argmem: readwrite)
declare i32 @feme.cpu.resource.atomic.add.raw.i32(ptr, i32, i32, i64, i32, i1) #2

attributes #0 = { nounwind willreturn memory(none) }
attributes #1 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="16,1,1" "hlsl.shader"="compute" }
attributes #2 = { nounwind willreturn memory(argmem: readwrite) }

!feme.cpu.resources = !{!0}
!feme.cpu.bound_resources = !{!1}

!0 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!1 = !{!"main", i32 1, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0}

;--- harness.c
// Minimal host harness for `feme_cpu_entry_main`'s single-group,
// single-wave (`wave-size=4`) ABI, matching `feme::cpu::EntryWrapperPass`'s
// synthesized `Args` layout (see `AOTDispatchTest.cpp`/`JITEngine::dispatch`
// for the authoritative definition this duplicates; there is no public C
// header for it since it is codegen-internal to the CPU target's JIT/AOT
// dispatch path).
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
  void *resource_heap;
  int32_t resource_heap_count;
  void *sampler_heap;
  int32_t sampler_heap_count;
  void *root_constants;
  int32_t root_constant_size;
  void *image_heap;
  int32_t image_heap_count;
  void *unused_ptr;
  int32_t unused_i32;
  void *extra2[2];
} InnerArgs;

typedef struct {
  InnerArgs inner;
  int32_t group_id[3];
  int32_t group_count[3];
  void *field3;
  void *field4[4];
} Args;

extern void feme_cpu_entry_main(Args *args);

// Scalar fallback for the one atomic call this shader's lowered-and-
// SIMDized IR still issues per-lane (see `simdize-scalarize-atomic.ll` for
// why an `atomicrmw`-shaped op always scalarizes): a real heap write,
// masked by the lowered call's own trailing `i1` lane-mask operand.
int32_t feme_atomic_add(void *heap, int32_t heap_count, int32_t binding,
                         int64_t offset, int32_t value, int8_t mask)
    asm("feme.cpu.resource.atomic.add.raw.i32");
int32_t feme_atomic_add(void *heap, int32_t heap_count, int32_t binding,
                         int64_t offset, int32_t value, int8_t mask) {
  (void)heap_count;
  (void)binding;
  if (mask & 1) {
    int32_t *p = (int32_t *)((char *)heap + offset);
    int32_t old = *p;
    *p += value;
    return old;
  }
  return 0;
}

int main(void) {
  char heap[1024];
  memset(heap, 0, sizeof(heap));
  Args args;
  memset(&args, 0, sizeof(args));
  args.inner.resource_heap = heap;
  args.inner.resource_heap_count = sizeof(heap);
  args.group_id[0] = args.group_id[1] = args.group_id[2] = 0;
  args.group_count[0] = 1;
  args.group_count[1] = 1;
  args.group_count[2] = 1;

  feme_cpu_entry_main(&args);

  // `dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.
  // max_workgroup_memory`'s own shape: 160 (40*4) output slots, each
  // incremented exactly once by exactly one thread. `L442`'s bug doubled
  // every multiple-of-4 slot (39 of the 160) via one ghost loop iteration.
  int bad = 0;
  for (int i = 0; i < 160; i++) {
    int32_t v;
    memcpy(&v, heap + i * 4, 4);
    if (v != 1) {
      fprintf(stderr, "MISMATCH offset=%d value=%d\n", i, v);
      bad++;
    }
  }
  if (bad == 0)
    printf("ALL-160-CORRECT\n");
  else
    printf("MISMATCH-COUNT=%d\n", bad);
  return 0;
}
