; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H94/H94a: a real, captured pre-`feme-cpu-linearize` IR reduction
; (via `feme-translate --import-spirv` -> `feme-opt
; --feme-convert-spirv-to-llvm` -> `feme-translate --llvmdialect-to-llvmir`
; -> `feme-opt -passes='feme-cpu-fold-spirv-builtins,feme-cpu-prepare,...'`)
; of `dEQP-VK.mesh_shader.ext.properties.mesh_shared_memory_size`'s own
; workgroup-shared-memory verification loop -- a `for (elemIdx...) { if
; (sharedMem[elemIdx] != expected) break; }` shape whose one real,
; data-dependent divergent check (block `54`'s `icmp eq`) has its exit
; decision relayed through *two* further `StructurizeCFG`-built boolean-
; relay `CondBr` blocks (`Flow`, then `loop.exit.guard`) before reaching
; the loop's exit block, rather than the single relay hop
; `matchExitCheckWithRelay`'s own pre-existing (roadmap H19k) tolerance
; could follow.
;
; Before this fix, `feme::cpu::LoopLinearizer`'s `OtherCondBrBlocks`
; classification saw both `54` and `Flow` as separately-divergent exit-
; check candidates (`UniformityInfo` cannot distinguish a real check from
; its own provably-redundant relay purely by divergence) and conservatively
; diagnosed this loop as unsupported ("has more than one divergent exit
; check"), rather than recognizing them as one real check re-encoded twice.
;
; The fix generalizes `foldRedundantFlowBlock`/`peelConstantFlowPredecessors`
; (both already tolerant of an optional `xor(Cond, true)` negation wrapper
; via the new `getFlowConditionPhi` helper) with two further, narrowly-
; scoped helpers -- `collapseTriviallyRedundantPhisInCycle` (RAUW's any
; cycle phi `lookThroughTrivialPhi` can already prove trivially redundant)
; and `mergeTrivialRelayBlocksInCycle` (merges a now-phi-less, single-
; predecessor, purely-relay block into its own equally-pure-relay
; predecessor via `llvm::MergeBlockIntoPredecessor`) -- run to a fixed
; point alongside the two existing fold/peel passes. Both new helpers are
; deliberately restricted (via `RelayCandidates`, seeded and grown only by
; the fold/peel steps' own peeled/merged blocks, and `isSyntheticRelayBlockName`,
; a `StructurizeCFG`/`ControlFlowUtils` literal-naming heuristic: a `"Flow"`
; prefix or `".guard"` substring) to blocks structurally proven redundant by
; this milestone's own transforms, never a real, user-authored check block
; that happens to end up in a structurally identical (phi + conditional
; branch only) shape after some unrelated peel -- seen directly in this
; session's own regression-and-fix cycle (see agent_thoughts.md's "H94a
; session" entry for the full narrative).
;
; This reduction's own loop starts at block `45` (after an earlier,
; unrelated write-phase verification loop this file also contains, sharing
; the identical two-relay-hop shape one loop iteration earlier via
; `Flow26`/no third hop there since that loop's own exit is unconditional
; rather than routed through a `loop.exit.guard`).

; CHECK-LABEL: define void @main(
; CHECK-NOT: has more than one divergent exit check
; CHECK: feme.cpu.mask.any

; ModuleID = 'module0.ll'
source_filename = "LLVMDialectModule"
target datalayout = "e-ve-i64:64-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan-mesh"

@spirv_var_118.str = private constant [14 x i8] c"spirv_var_118\00"
@spirv_var_41 = external addrspace(3) global [1 x i32]

declare void @feme.stage.set_mesh_outputs(i32, i32)

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.ceil.f32(float) #0

; Function Attrs: convergent nocallback nofree nosync nounwind willreturn
declare void @llvm.spv.device.memory.barrier() #1

; Function Attrs: convergent nocallback nofree nosync nounwind willreturn
declare void @llvm.spv.group.memory.barrier.with.group.sync() #1

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.flattened.thread.id.in.group() #2

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #3 {
  %1 = call float @llvm.ceil.f32(float 7.812500e-03)
  %2 = fptoui float %1 to i32
  br label %3

3:                                                ; preds = %Flow27._crit_edge, %0
  %.022 = phi i32 [ 0, %0 ], [ %14, %Flow27._crit_edge ]
  br label %7

4:                                                ; preds = %Flow27
  call void @llvm.spv.device.memory.barrier()
  call void @llvm.spv.group.memory.barrier.with.group.sync()
  br label %21

5:                                                ; preds = %16
  %6 = add i32 %.022, 1
  br label %Flow27

7:                                                ; preds = %3
  %8 = icmp ult i32 %.022, %2
  br i1 %8, label %9, label %.Flow27_crit_edge

.Flow27_crit_edge:                                ; preds = %7
  br label %Flow27

9:                                                ; preds = %7
  %10 = call i32 @llvm.spv.flattened.thread.id.in.group()
  %11 = mul i32 %2, %10
  %12 = add i32 %11, %.022
  %13 = icmp ult i32 %12, 1
  br i1 %13, label %17, label %._crit_edge

._crit_edge:                                      ; preds = %9
  br label %16

Flow27:                                           ; preds = %.Flow27_crit_edge, %5
  %14 = phi i32 [ %6, %5 ], [ poison, %.Flow27_crit_edge ]
  %15 = phi i1 [ false, %5 ], [ true, %.Flow27_crit_edge ]
  br i1 %15, label %4, label %Flow27._crit_edge

Flow27._crit_edge:                                ; preds = %Flow27
  br label %3

16:                                               ; preds = %._crit_edge, %17
  br label %5

17:                                               ; preds = %9
  %18 = mul i32 %12, 2
  %19 = add i32 %18, 1000
  %20 = getelementptr [1 x i32], ptr addrspace(3) @spirv_var_41, i32 0, i32 %12
  store i32 %19, ptr addrspace(3) %20, align 4
  br label %16

21:                                               ; preds = %Flow26._crit_edge, %4
  %.023 = phi i32 [ 0, %4 ], [ %34, %Flow26._crit_edge ]
  br label %27

22:                                               ; preds = %Flow26
  call void @llvm.spv.device.memory.barrier()
  call void @llvm.spv.group.memory.barrier.with.group.sync()
  %23 = call i32 @llvm.spv.flattened.thread.id.in.group()
  %24 = icmp eq i32 %23, 0
  br i1 %24, label %44, label %.Flow25_crit_edge

.Flow25_crit_edge:                                ; preds = %22
  br label %Flow25

25:                                               ; preds = %36
  %26 = add i32 %.023, 1
  br label %Flow26

27:                                               ; preds = %21
  %28 = icmp ult i32 %.023, %2
  br i1 %28, label %29, label %.Flow26_crit_edge

.Flow26_crit_edge:                                ; preds = %27
  br label %Flow26

29:                                               ; preds = %27
  %30 = call i32 @llvm.spv.flattened.thread.id.in.group()
  %31 = mul i32 %2, %30
  %32 = add i32 %31, %.023
  %33 = icmp ult i32 %32, 1
  br i1 %33, label %37, label %._crit_edge28

._crit_edge28:                                    ; preds = %29
  br label %36

Flow26:                                           ; preds = %.Flow26_crit_edge, %25
  %34 = phi i32 [ %26, %25 ], [ poison, %.Flow26_crit_edge ]
  %35 = phi i1 [ false, %25 ], [ true, %.Flow26_crit_edge ]
  br i1 %35, label %22, label %Flow26._crit_edge

Flow26._crit_edge:                                ; preds = %Flow26
  br label %21

36:                                               ; preds = %._crit_edge28, %37
  br label %25

37:                                               ; preds = %29
  %38 = sub i32 0, %32
  %39 = getelementptr [1 x i32], ptr addrspace(3) @spirv_var_41, i32 0, i32 %38
  %40 = load i32, ptr addrspace(3) %39, align 4
  %41 = add i32 %40, %38
  %42 = getelementptr [1 x i32], ptr addrspace(3) @spirv_var_41, i32 0, i32 %38
  store i32 %41, ptr addrspace(3) %42, align 4
  br label %36

43:                                               ; preds = %Flow25
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i32 1, i1 true)
  call void @feme.stage.set_mesh_outputs(i32 0, i32 0)
  ret void

44:                                               ; preds = %22
  br label %45

Flow25:                                           ; preds = %.Flow25_crit_edge, %46
  br label %43

45:                                               ; preds = %Flow._crit_edge, %44
  %.0 = phi i32 [ %60, %Flow._crit_edge ], [ 0, %44 ]
  br label %52

46:                                               ; preds = %loop.exit.guard._crit_edge, %64
  %.021 = phi i1 [ false, %64 ], [ true, %loop.exit.guard._crit_edge ]
  %47 = select i1 %.021, i32 1, i32 0
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i32 %47, i1 true)
  br label %Flow25

Flow24:                                           ; preds = %.Flow24_crit_edge, %50
  %48 = phi i32 [ %51, %50 ], [ poison, %.Flow24_crit_edge ]
  %49 = phi i1 [ false, %50 ], [ true, %.Flow24_crit_edge ]
  br label %Flow

50:                                               ; preds = %63
  %51 = add i32 %.0, 1
  br label %Flow24

52:                                               ; preds = %45
  %53 = icmp ult i32 %.0, 1
  br i1 %53, label %54, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %52
  br label %Flow

54:                                               ; preds = %52
  %55 = getelementptr [1 x i32], ptr addrspace(3) @spirv_var_41, i32 0, i32 %.0
  %56 = load i32, ptr addrspace(3) %55, align 4
  %57 = mul i32 %.0, 3
  %58 = add i32 %57, 1000
  %59 = icmp eq i32 %56, %58
  br i1 %59, label %63, label %.Flow24_crit_edge

.Flow24_crit_edge:                                ; preds = %54
  br label %Flow24

Flow:                                             ; preds = %.Flow_crit_edge, %Flow24
  %60 = phi i32 [ %48, %Flow24 ], [ poison, %.Flow_crit_edge ]
  %61 = phi i1 [ false, %Flow24 ], [ true, %.Flow_crit_edge ]
  %62 = phi i1 [ %49, %Flow24 ], [ true, %.Flow_crit_edge ]
  br i1 %62, label %loop.exit.guard, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %45

63:                                               ; preds = %54
  br label %50

64:                                               ; preds = %loop.exit.guard
  br label %46

loop.exit.guard:                                  ; preds = %Flow
  %Guard..inv = xor i1 %61, true
  br i1 %Guard..inv, label %64, label %loop.exit.guard._crit_edge

loop.exit.guard._crit_edge:                       ; preds = %loop.exit.guard
  br label %46
}

; Function Attrs: nounwind willreturn memory(argmem: write)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1) #4

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { convergent nocallback nofree nosync nounwind willreturn }
attributes #2 = { nounwind willreturn memory(none) }
attributes #3 = { "feme.mesh.max_output_primitives"="1" "feme.mesh.max_output_vertices"="3" "feme.mesh.output_topology"="triangles" "feme.shader.stage"="mesh" "hlsl.numthreads"="128,1,1" "hlsl.shader"="mesh" }
attributes #4 = { nounwind willreturn memory(argmem: write) }

!llvm.module.flags = !{!1}
!feme.cpu.resources = !{!2}
!feme.cpu.bound_resources = !{!3}

!1 = !{i32 2, !"Debug Info Version", i32 3}
!2 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!3 = !{!"main", i32 1, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0}
