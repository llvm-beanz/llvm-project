; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L452(d): a loop-body switch-decode diamond chain (`NodeBlock5`/
; `NodeBlock`/`Flow`/`LeafBlock`/`LeafBlock3`/`Flow7`/`Flow8`/`Flow9`/
; `Flow10`/`Flow11`) feeds a value into a mask-affecting resource load and
; atomic further down the loop body. `Flow11` is a genuine, clean
; two-predecessor reconvergence point for the `Flow10` diamond above it
; (both of `Flow10`'s successors -- `57` and the
; `Flow10.Flow11_crit_edge` relay -- reach `Flow11` and nothing else
; first). `peelConstantFlowPredecessors`, run earlier in the same
; fixed-point fold sequence over an *unrelated* constant-condition phi
; elsewhere in the loop, used to be blind to this: nothing stopped it
; from bypassing a constant predecessor of `Flow11` itself whenever one
; happened to exist, leaving `Flow11` with only one real predecessor and
; destroying its standing as `Flow10`'s own clean reconvergence point --
; exactly the "outer diamond arm directly reaches a nested sibling
; diamond's own target" shape `DiamondFlattener::validate`'s own
; `L375`/`L383(a)` commentary documents as unsupported, which
; `LoopLinearizer`'s later `flattenLoopBodyRegion` walk then rejected
; with "has an internal branch in a body shape this milestone does not
; yet lower". Fixed by
; `peelWouldDestroyAncestorReconvergencePoint` detecting -- before
; peeling -- that the sole surviving predecessor an in-flight peel would
; leave behind for a block still traces back to a real `CondBrInst`
; whose own two successors both reach that same block through nothing
; but relay hops, and bailing out of the peel for that block in that
; case. Block/value names and shape preserved verbatim (only renamed
; from their original numeric form) from the real failing case isolated
; via `FEME_DUMP_IR_PRELINEARIZE=1` against
; `dEQP-VK.spirv_assembly.instruction.compute.opcompositeextract.
; f16arr3`'s compiled compute shader; see `Roadmap.md`'s own `L452(d)`
; entry for the full writeup.

; CHECK-LABEL: define void @main(
; CHECK-NOT: unsupported
define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
  %1 = call i32 @llvm.spv.thread.id.i32(i32 0)
  %2 = call i32 @llvm.spv.thread.id.i32(i32 1)
  %3 = call i32 @llvm.spv.thread.id.i32(i32 2)
  %30 = or i32 %1, %2
  %31 = or i32 %3, %30
  %32 = icmp eq i32 %31, 0
  br i1 %32, label %33, label %.Flow13_crit_edge

.Flow13_crit_edge:
  br label %Flow13

33:
  br label %34

Flow13:
  br label %spirv_fn_123.exit

34:
  %.0 = phi i32 [ %49, %Flow12._crit_edge ], [ 0, %33 ]
  %35 = icmp slt i32 %.0, 4
  br i1 %35, label %37, label %.Flow12_crit_edge

.Flow12_crit_edge:
  br label %Flow12

36:
  br label %Flow13

37:
  %38 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %39 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i1 true)
  %40 = bitcast i32 %38 to <2 x half>
  %41 = bitcast i32 %39 to <2 x half>
  %42 = extractelement <2 x half> %40, i32 0
  %43 = extractelement <2 x half> %40, i32 1
  %44 = extractelement <2 x half> %41, i32 0
  %45 = insertvalue [3 x half] poison, half %42, 0
  %46 = insertvalue [3 x half] %45, half %43, 1
  %47 = insertvalue [3 x half] %46, half %44, 2
  %48 = insertvalue <{ [3 x half] }> poison, [3 x half] %47, 0
  br label %NodeBlock5

Flow12:
  %49 = phi i32 [ %82, %spirv_fn_141.exit ], [ poison, %.Flow12_crit_edge ]
  %50 = phi i1 [ false, %spirv_fn_141.exit ], [ true, %.Flow12_crit_edge ]
  br i1 %50, label %36, label %Flow12._crit_edge

Flow12._crit_edge:
  br label %34

NodeBlock5:
  %Pivot6 = icmp sge i32 %.0, 1
  br i1 %Pivot6, label %NodeBlock, label %NodeBlock5.Flow_crit_edge

NodeBlock5.Flow_crit_edge:
  br label %Flow

NodeBlock:
  %Pivot = icmp sge i32 %.0, 2
  br i1 %Pivot, label %LeafBlock3, label %NodeBlock.Flow7_crit_edge

NodeBlock.Flow7_crit_edge:
  br label %Flow7

Flow:
  %51 = phi half [ %54, %Flow9 ], [ poison, %NodeBlock5.Flow_crit_edge ]
  %52 = phi i1 [ %67, %Flow9 ], [ false, %NodeBlock5.Flow_crit_edge ]
  %53 = phi i1 [ false, %Flow9 ], [ true, %NodeBlock5.Flow_crit_edge ]
  br i1 %53, label %LeafBlock, label %Flow.Flow10_crit_edge

Flow.Flow10_crit_edge:
  br label %Flow10

LeafBlock3:
  %SwitchLeaf4 = icmp eq i32 %.0, 2
  br i1 %SwitchLeaf4, label %69, label %LeafBlock3.Flow8_crit_edge

LeafBlock3.Flow8_crit_edge:
  br label %Flow8

Flow9:
  %54 = phi half [ %65, %64 ], [ %66, %Flow7.Flow9_crit_edge ]
  br label %Flow

LeafBlock:
  %SwitchLeaf = icmp ne i32 %.0, 0
  br label %Flow10

Flow10:
  %55 = phi i1 [ true, %LeafBlock ], [ false, %Flow.Flow10_crit_edge ]
  %56 = phi i1 [ %SwitchLeaf, %LeafBlock ], [ %52, %Flow.Flow10_crit_edge ]
  br i1 %56, label %57, label %Flow10.Flow11_crit_edge

Flow10.Flow11_crit_edge:
  br label %Flow11

57:
  br label %Flow11

Flow11:
  %58 = phi half [ -1.000000e+00, %57 ], [ %51, %Flow10.Flow11_crit_edge ]
  %59 = phi i1 [ false, %57 ], [ %55, %Flow10.Flow11_crit_edge ]
  br i1 %59, label %60, label %Flow11.spirv_fn_141.exit_crit_edge

Flow11.spirv_fn_141.exit_crit_edge:
  br label %spirv_fn_141.exit

60:
  %61 = extractvalue <{ [3 x half] }> %48, 0, 0
  br label %spirv_fn_141.exit

Flow8:
  %62 = phi half [ %70, %69 ], [ poison, %LeafBlock3.Flow8_crit_edge ]
  %63 = phi i1 [ false, %69 ], [ true, %LeafBlock3.Flow8_crit_edge ]
  br label %Flow7

64:
  %65 = extractvalue <{ [3 x half] }> %48, 0, 1
  br label %Flow9

Flow7:
  %66 = phi half [ %62, %Flow8 ], [ poison, %NodeBlock.Flow7_crit_edge ]
  %67 = phi i1 [ %63, %Flow8 ], [ false, %NodeBlock.Flow7_crit_edge ]
  %68 = phi i1 [ false, %Flow8 ], [ true, %NodeBlock.Flow7_crit_edge ]
  br i1 %68, label %64, label %Flow7.Flow9_crit_edge

Flow7.Flow9_crit_edge:
  br label %Flow9

69:
  %70 = extractvalue <{ [3 x half] }> %48, 0, 2
  br label %Flow8

spirv_fn_141.exit:
  %71 = phi half [ %61, %60 ], [ %58, %Flow11.spirv_fn_141.exit_crit_edge ]
  %72 = and i32 %.0, 1
  %73 = insertelement <2 x half> zeroinitializer, half %71, i32 %72
  %74 = icmp eq i32 %72, 1
  %75 = select i1 %74, i32 65535, i32 -65536
  %76 = bitcast <2 x half> %73 to i32
  %77 = or i32 %76, %75
  %78 = udiv i32 %.0, 2
  %79 = zext i32 %78 to i64
  %80 = mul i64 %79, 4
  %81 = call i32 @feme.cpu.resource.atomic.and.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 %80, i32 %77, i1 true)
  %82 = add i32 %.0, 1
  br label %Flow12

spirv_fn_123.exit:
  ret void
}

declare i32 @llvm.spv.thread.id.i32(i32)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
declare i32 @feme.cpu.resource.atomic.and.raw.i32(ptr, i32, i32, i64, i32, i1)

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
