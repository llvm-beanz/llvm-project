; RUN: feme-opt --llvm -passes="function(instnamer),feme-cpu-linearize" -S %s | opt -passes=verify -S | FileCheck %s

; Roadmap ENV6(c)(2)(i): a real, captured pre-`feme-cpu-linearize` IR
; reduction of `dEQP-VK.graphicsfuzz.stable-mergesort-for-always-false-if-
; discard` (`FEME_DUMP_IR_PRELINEARIZE=1`). This shape nests a second,
; genuinely divergent `CondBrInst` diamond (`bb608`) inside a loop whose
; own body already contains a sibling divergent diamond (`bb584`):
; `peelConstantFlowPredecessors` (`H165`'s own cleanup pass) previously
; peeled a constant-valued predecessor edge feeding one of `bb584`'s own
; merge blocks without checking whether doing so would newly make that
; same merge block a *shared* reconvergence point for `bb608`'s sibling
; diamond too -- `bb608`'s own, unrelated `validate` walk then found that
; block with more incoming predecessors than its own diamond shape
; expected, and `flattenLoopBodyDiamond` rejected it outright ("has an
; internal branch in 'Flow'"), long before ever reaching `verifyModule`.
;
; Fixed by a new, complementary, *forward*-looking guard,
; `peelWouldExposeSiblingDiamondToSharedMerge` (next to the existing,
; *backward*-looking `peelWouldDestroyAncestorReconvergencePoint`): before
; peeling, it speculatively applies the candidate peel's own edge
; retargets and re-checks every other *divergent* `CondBrInst` in the
; enclosing cycle's own `DiamondFlattener::probeValidate` result,
; bailing out of just that one peel (not the whole cycle) if any
; candidate that validated cleanly beforehand would newly fail
; afterward. Restricted to `UniformityInfo::isDivergentTerminator`
; candidates only (not `LoopLinearizer::isDivergentBranch`'s broader,
; `KnownUniformValues`-aware notion, unavailable to this free function):
; a *uniform* `ChainGate`-style candidate's own merge block is, by
; design, meant to gain an extra predecessor this way (that is exactly
; the shape `ChainGate`'s own separate recognition mechanism recovers),
; so naively including uniform candidates in this scan produced a false-
; positive regression on the `ENV6(c)(2)(g)` `ChainGate` shape
; (`LinearizesUniformInnerCondGatingDivergentCheck`) during development.
;
; With this fix, `feme-cpu-linearize` now lowers this shape cleanly
; (`bb584`/`bb608` both fully flattened, `verifyModule`-clean) instead of
; rejecting it -- confirmed via the real Vulkan CTS corpus to also newly
; flip a second, previously-unrelated case
; (`cov-function-large-loop-always-return-first-iteration`) from Fail to
; Pass, with zero regressions across the full 106-case graphicsfuzz
; corpus. `stable-mergesort-for-always-false-if-discard` itself still
; `Fail`s under the real ICD, but now at a later pipeline stage
; (`feme-cpu-simdize` rejects a separate, pre-existing, independent
; divergent-branch shape synthesized by `LoopLinearizer` itself in block
; `loop.exit.guard`, outside this diamond entirely -- tracked separately,
; see `Roadmap.md`).

; CHECK-NOT: has an internal branch in
; CHECK: ret void
; ModuleID = '<in-memory object>'
source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

@spirv_var_161.str = private constant [14 x i8] c"spirv_var_161\00"
@gl_FragCoord = external addrspace(7) constant <4 x float>, !spirv.Decorations !1
@_GLF_color = external addrspace(8) global <4 x float>, !spirv.Decorations !3

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.spv.discard() #0

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i1 @llvm.is.fpclass.f32(float, i32 immarg) #1

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.copysign.f32(float, float) #1

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.smin.i32(i32, i32) #1

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

declare void @feme.stage.discard(i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #2 !feme.signature !4 {
  %1 = alloca [10 x i32], align 4
  %2 = alloca [10 x i32], align 4
  %3 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %4 = fptosi float %3 to i32
  br label %5

5:                                                ; preds = %._crit_edge, %0
  %6 = phi i32 [ %4, %0 ], [ %12, %._crit_edge ]
  br label %NodeBlock17

NodeBlock17:                                      ; preds = %5
  %Pivot18 = icmp sge i32 %6, 5
  br i1 %Pivot18, label %NodeBlock15, label %NodeBlock17.Flow98_crit_edge

NodeBlock17.Flow98_crit_edge:                     ; preds = %NodeBlock17
  br label %Flow98

NodeBlock15:                                      ; preds = %NodeBlock17
  %Pivot16 = icmp sge i32 %6, 7
  br i1 %Pivot16, label %NodeBlock13, label %NodeBlock15.Flow87_crit_edge

NodeBlock15.Flow87_crit_edge:                     ; preds = %NodeBlock15
  br label %Flow87

NodeBlock13:                                      ; preds = %NodeBlock15
  %Pivot14 = icmp sge i32 %6, 8
  br i1 %Pivot14, label %NodeBlock11, label %NodeBlock13.Flow83_crit_edge

NodeBlock13.Flow83_crit_edge:                     ; preds = %NodeBlock13
  br label %Flow83

NodeBlock11:                                      ; preds = %NodeBlock13
  %Pivot12 = icmp sge i32 %6, 9
  br i1 %Pivot12, label %LeafBlock9, label %NodeBlock11.Flow81_crit_edge

NodeBlock11.Flow81_crit_edge:                     ; preds = %NodeBlock11
  br label %Flow81

LeafBlock9:                                       ; preds = %NodeBlock11
  %SwitchLeaf10 = icmp eq i32 %6, 9
  br i1 %SwitchLeaf10, label %46, label %LeafBlock9.Flow80_crit_edge

LeafBlock9.Flow80_crit_edge:                      ; preds = %LeafBlock9
  br label %Flow80

Flow87:                                           ; preds = %NodeBlock15.Flow87_crit_edge, %Flow84
  %7 = phi i1 [ false, %Flow84 ], [ true, %NodeBlock15.Flow87_crit_edge ]
  br i1 %7, label %NodeBlock7, label %Flow87.Flow88_crit_edge

Flow87.Flow88_crit_edge:                          ; preds = %Flow87
  br label %Flow88

NodeBlock7:                                       ; preds = %Flow87
  %Pivot8 = icmp sge i32 %6, 6
  br i1 %Pivot8, label %35, label %NodeBlock7.Flow85_crit_edge

NodeBlock7.Flow85_crit_edge:                      ; preds = %NodeBlock7
  br label %Flow85

Flow98:                                           ; preds = %NodeBlock17.Flow98_crit_edge, %Flow88
  %8 = phi i1 [ false, %Flow88 ], [ true, %NodeBlock17.Flow98_crit_edge ]
  br i1 %8, label %NodeBlock5, label %Flow98._crit_edge

Flow98._crit_edge:                                ; preds = %Flow98
  br label %11

NodeBlock5:                                       ; preds = %Flow98
  %Pivot6 = icmp sge i32 %6, 2
  br i1 %Pivot6, label %NodeBlock3, label %NodeBlock5.Flow96_crit_edge

NodeBlock5.Flow96_crit_edge:                      ; preds = %NodeBlock5
  br label %Flow96

NodeBlock3:                                       ; preds = %NodeBlock5
  %Pivot4 = icmp sge i32 %6, 3
  br i1 %Pivot4, label %NodeBlock1, label %NodeBlock3.Flow91_crit_edge

NodeBlock3.Flow91_crit_edge:                      ; preds = %NodeBlock3
  br label %Flow91

NodeBlock1:                                       ; preds = %NodeBlock3
  %Pivot2 = icmp sge i32 %6, 4
  br i1 %Pivot2, label %28, label %NodeBlock1.Flow89_crit_edge

NodeBlock1.Flow89_crit_edge:                      ; preds = %NodeBlock1
  br label %Flow89

Flow96:                                           ; preds = %NodeBlock5.Flow96_crit_edge, %Flow92
  %9 = phi i1 [ false, %Flow92 ], [ true, %NodeBlock5.Flow96_crit_edge ]
  br i1 %9, label %NodeBlock, label %Flow96.Flow97_crit_edge

Flow96.Flow97_crit_edge:                          ; preds = %Flow96
  br label %Flow97

NodeBlock:                                        ; preds = %Flow96
  %Pivot = icmp sge i32 %6, 1
  br i1 %Pivot, label %17, label %NodeBlock.Flow94_crit_edge

NodeBlock.Flow94_crit_edge:                       ; preds = %NodeBlock
  br label %Flow94

Flow94:                                           ; preds = %NodeBlock.Flow94_crit_edge, %17
  %10 = phi i1 [ false, %17 ], [ true, %NodeBlock.Flow94_crit_edge ]
  br i1 %10, label %LeafBlock, label %Flow94.Flow95_crit_edge

Flow94.Flow95_crit_edge:                          ; preds = %Flow94
  br label %Flow95

LeafBlock:                                        ; preds = %Flow94
  %SwitchLeaf = icmp eq i32 %6, 0
  br i1 %SwitchLeaf, label %14, label %LeafBlock.Flow93_crit_edge

LeafBlock.Flow93_crit_edge:                       ; preds = %LeafBlock
  br label %Flow93

Flow80:                                           ; preds = %LeafBlock9.Flow80_crit_edge, %46
  br label %Flow81

Flow82:                                           ; preds = %Flow81.Flow82_crit_edge, %43
  br label %Flow83

Flow84:                                           ; preds = %Flow83.Flow84_crit_edge, %39
  br label %Flow87

Flow86:                                           ; preds = %Flow85.Flow86_crit_edge, %32
  br label %Flow88

Flow88:                                           ; preds = %Flow87.Flow88_crit_edge, %Flow86
  br label %Flow98

Flow90:                                           ; preds = %Flow89.Flow90_crit_edge, %25
  br label %Flow91

Flow92:                                           ; preds = %Flow91.Flow92_crit_edge, %21
  br label %Flow96

Flow93:                                           ; preds = %LeafBlock.Flow93_crit_edge, %14
  br label %Flow95

Flow95:                                           ; preds = %Flow94.Flow95_crit_edge, %Flow93
  br label %Flow97

Flow97:                                           ; preds = %Flow96.Flow97_crit_edge, %Flow95
  br label %11

11:                                               ; preds = %Flow98._crit_edge, %Flow97
  %12 = add i32 %6, 1
  %13 = icmp sge i32 %12, 10
  br i1 %13, label %49, label %._crit_edge

._crit_edge:                                      ; preds = %11
  br label %5

14:                                               ; preds = %LeafBlock
  %15 = sext i32 %6 to i64
  %16 = getelementptr [4 x i8], ptr %2, i64 %15
  store i32 4, ptr %16, align 4
  br label %Flow93

17:                                               ; preds = %NodeBlock
  %18 = sext i32 %6 to i64
  %19 = getelementptr [4 x i8], ptr %2, i64 %18
  store i32 3, ptr %19, align 4
  br label %Flow94

Flow91:                                           ; preds = %NodeBlock3.Flow91_crit_edge, %Flow90
  %20 = phi i1 [ false, %Flow90 ], [ true, %NodeBlock3.Flow91_crit_edge ]
  br i1 %20, label %21, label %Flow91.Flow92_crit_edge

Flow91.Flow92_crit_edge:                          ; preds = %Flow91
  br label %Flow92

21:                                               ; preds = %Flow91
  %22 = sext i32 %6 to i64
  %23 = getelementptr [4 x i8], ptr %2, i64 %22
  store i32 2, ptr %23, align 4
  br label %Flow92

Flow89:                                           ; preds = %NodeBlock1.Flow89_crit_edge, %28
  %24 = phi i1 [ false, %28 ], [ true, %NodeBlock1.Flow89_crit_edge ]
  br i1 %24, label %25, label %Flow89.Flow90_crit_edge

Flow89.Flow90_crit_edge:                          ; preds = %Flow89
  br label %Flow90

25:                                               ; preds = %Flow89
  %26 = sext i32 %6 to i64
  %27 = getelementptr [4 x i8], ptr %2, i64 %26
  store i32 1, ptr %27, align 4
  br label %Flow90

28:                                               ; preds = %NodeBlock1
  %29 = sext i32 %6 to i64
  %30 = getelementptr [4 x i8], ptr %2, i64 %29
  store i32 0, ptr %30, align 4
  br label %Flow89

Flow85:                                           ; preds = %NodeBlock7.Flow85_crit_edge, %35
  %31 = phi i1 [ false, %35 ], [ true, %NodeBlock7.Flow85_crit_edge ]
  br i1 %31, label %32, label %Flow85.Flow86_crit_edge

Flow85.Flow86_crit_edge:                          ; preds = %Flow85
  br label %Flow86

32:                                               ; preds = %Flow85
  %33 = sext i32 %6 to i64
  %34 = getelementptr [4 x i8], ptr %2, i64 %33
  store i32 -1, ptr %34, align 4
  br label %Flow86

35:                                               ; preds = %NodeBlock7
  %36 = sext i32 %6 to i64
  %37 = getelementptr [4 x i8], ptr %2, i64 %36
  store i32 -2, ptr %37, align 4
  br label %Flow85

Flow83:                                           ; preds = %NodeBlock13.Flow83_crit_edge, %Flow82
  %38 = phi i1 [ false, %Flow82 ], [ true, %NodeBlock13.Flow83_crit_edge ]
  br i1 %38, label %39, label %Flow83.Flow84_crit_edge

Flow83.Flow84_crit_edge:                          ; preds = %Flow83
  br label %Flow84

39:                                               ; preds = %Flow83
  %40 = sext i32 %6 to i64
  %41 = getelementptr [4 x i8], ptr %2, i64 %40
  store i32 -3, ptr %41, align 4
  br label %Flow84

Flow81:                                           ; preds = %NodeBlock11.Flow81_crit_edge, %Flow80
  %42 = phi i1 [ false, %Flow80 ], [ true, %NodeBlock11.Flow81_crit_edge ]
  br i1 %42, label %43, label %Flow81.Flow82_crit_edge

Flow81.Flow82_crit_edge:                          ; preds = %Flow81
  br label %Flow82

43:                                               ; preds = %Flow81
  %44 = sext i32 %6 to i64
  %45 = getelementptr [4 x i8], ptr %2, i64 %44
  store i32 -4, ptr %45, align 4
  br label %Flow82

46:                                               ; preds = %LeafBlock9
  %47 = sext i32 %6 to i64
  %48 = getelementptr [4 x i8], ptr %2, i64 %47
  store i32 -5, ptr %48, align 4
  br label %Flow80

49:                                               ; preds = %11
  %50 = load i32, ptr %2, align 4
  store i32 %50, ptr %1, align 4
  %51 = getelementptr inbounds nuw i8, ptr %2, i64 4
  %52 = load i32, ptr %51, align 4
  %53 = getelementptr inbounds nuw i8, ptr %1, i64 4
  store i32 %52, ptr %53, align 4
  %54 = getelementptr inbounds nuw i8, ptr %2, i64 8
  %55 = load i32, ptr %54, align 4
  %56 = getelementptr inbounds nuw i8, ptr %1, i64 8
  store i32 %55, ptr %56, align 4
  %57 = getelementptr inbounds nuw i8, ptr %2, i64 12
  %58 = load i32, ptr %57, align 4
  %59 = getelementptr inbounds nuw i8, ptr %1, i64 12
  store i32 %58, ptr %59, align 4
  %60 = getelementptr inbounds nuw i8, ptr %2, i64 16
  %61 = load i32, ptr %60, align 4
  %62 = getelementptr inbounds nuw i8, ptr %1, i64 16
  store i32 %61, ptr %62, align 4
  %63 = getelementptr inbounds nuw i8, ptr %2, i64 20
  %64 = load i32, ptr %63, align 4
  %65 = getelementptr inbounds nuw i8, ptr %1, i64 20
  store i32 %64, ptr %65, align 4
  %66 = getelementptr inbounds nuw i8, ptr %2, i64 24
  %67 = load i32, ptr %66, align 4
  %68 = getelementptr inbounds nuw i8, ptr %1, i64 24
  store i32 %67, ptr %68, align 4
  %69 = getelementptr inbounds nuw i8, ptr %2, i64 28
  %70 = load i32, ptr %69, align 4
  %71 = getelementptr inbounds nuw i8, ptr %1, i64 28
  store i32 %70, ptr %71, align 4
  %72 = getelementptr inbounds nuw i8, ptr %2, i64 32
  %73 = load i32, ptr %72, align 4
  %74 = getelementptr inbounds nuw i8, ptr %1, i64 32
  store i32 %73, ptr %74, align 4
  %75 = getelementptr inbounds nuw i8, ptr %2, i64 36
  %76 = load i32, ptr %75, align 4
  %77 = getelementptr inbounds nuw i8, ptr %1, i64 36
  store i32 %76, ptr %77, align 4
  br label %313

Flow66:                                           ; preds = %.Flow66_crit_edge, %109
  %78 = phi i32 [ %115, %109 ], [ poison, %.Flow66_crit_edge ]
  %79 = phi i1 [ false, %109 ], [ true, %.Flow66_crit_edge ]
  br i1 %79, label %Flow67, label %Flow66._crit_edge

Flow66._crit_edge:                                ; preds = %Flow66
  br label %107

Flow67:                                           ; preds = %Flow66
  br label %Flow72

80:                                               ; preds = %Flow79._crit_edge, %Flow72._crit_edge100
  %81 = phi i32 [ %89, %Flow72._crit_edge100 ], [ 0, %Flow79._crit_edge ]
  %82 = icmp slt i32 %81, 9
  br i1 %82, label %83, label %.Flow72_crit_edge

.Flow72_crit_edge:                                ; preds = %80
  br label %Flow72

83:                                               ; preds = %80
  %84 = or disjoint i32 %81, 2
  %85 = or disjoint i32 %81, 1
  %86 = add i32 %81, 4
  %87 = or disjoint i32 %81, 3
  %88 = call i32 @llvm.smin.i32(i32 %87, i32 9)
  br label %91

Flow72:                                           ; preds = %.Flow72_crit_edge, %Flow67
  %89 = phi i32 [ %86, %Flow67 ], [ poison, %.Flow72_crit_edge ]
  %90 = phi i1 [ false, %Flow67 ], [ true, %.Flow72_crit_edge ]
  br i1 %90, label %Flow72._crit_edge, label %Flow72._crit_edge100

Flow72._crit_edge100:                             ; preds = %Flow72
  br label %80

Flow72._crit_edge:                                ; preds = %Flow72
  br label %154

91:                                               ; preds = %Flow71._crit_edge102, %83
  %92 = phi i32 [ %147, %Flow71._crit_edge102 ], [ %81, %83 ]
  %93 = phi i32 [ %145, %Flow71._crit_edge102 ], [ %84, %83 ]
  %94 = phi i32 [ %146, %Flow71._crit_edge102 ], [ %81, %83 ]
  %95 = icmp sle i32 %94, %85
  %96 = icmp sle i32 %93, %88
  %97 = and i1 %95, %96
  br i1 %97, label %124, label %.Flow71_crit_edge

.Flow71_crit_edge:                                ; preds = %91
  br label %Flow71

98:                                               ; preds = %Flow71._crit_edge, %Flow68._crit_edge101
  %99 = phi i32 [ %105, %Flow68._crit_edge101 ], [ %147, %Flow71._crit_edge ]
  %100 = phi i32 [ %104, %Flow68._crit_edge101 ], [ %146, %Flow71._crit_edge ]
  %101 = icmp slt i32 %100, 10
  %102 = icmp sle i32 %100, %85
  %103 = and i1 %101, %102
  br i1 %103, label %116, label %.Flow68_crit_edge

.Flow68_crit_edge:                                ; preds = %98
  br label %Flow68

Flow68:                                           ; preds = %.Flow68_crit_edge, %116
  %104 = phi i32 [ %118, %116 ], [ poison, %.Flow68_crit_edge ]
  %105 = phi i32 [ %117, %116 ], [ poison, %.Flow68_crit_edge ]
  %106 = phi i1 [ false, %116 ], [ true, %.Flow68_crit_edge ]
  br i1 %106, label %Flow68._crit_edge, label %Flow68._crit_edge101

Flow68._crit_edge101:                             ; preds = %Flow68
  br label %98

Flow68._crit_edge:                                ; preds = %Flow68
  br label %107

107:                                              ; preds = %Flow68._crit_edge, %Flow66._crit_edge
  %108 = phi i32 [ %78, %Flow66._crit_edge ], [ %81, %Flow68._crit_edge ]
  %.not3 = icmp sle i32 %108, %88
  br i1 %.not3, label %109, label %.Flow66_crit_edge

.Flow66_crit_edge:                                ; preds = %107
  br label %Flow66

109:                                              ; preds = %107
  %110 = sext i32 %108 to i64
  %111 = getelementptr [4 x i8], ptr %1, i64 %110
  %112 = load i32, ptr %111, align 4
  %113 = sext i32 %108 to i64
  %114 = getelementptr [4 x i8], ptr %2, i64 %113
  store i32 %112, ptr %114, align 4
  %115 = add i32 %108, 1
  br label %Flow66

116:                                              ; preds = %98
  %117 = add i32 %99, 1
  %118 = add i32 %100, 1
  %119 = sext i32 %100 to i64
  %120 = getelementptr [4 x i8], ptr %2, i64 %119
  %121 = load i32, ptr %120, align 4
  %122 = sext i32 %99 to i64
  %123 = getelementptr [4 x i8], ptr %1, i64 %122
  store i32 %121, ptr %123, align 4
  br label %Flow68

124:                                              ; preds = %91
  %125 = sext i32 %94 to i64
  %126 = getelementptr [4 x i8], ptr %2, i64 %125
  %127 = load i32, ptr %126, align 4
  %128 = sext i32 %93 to i64
  %129 = getelementptr [4 x i8], ptr %2, i64 %128
  %130 = load i32, ptr %129, align 4
  %131 = icmp sge i32 %127, %130
  %132 = add i32 %92, 1
  br i1 %131, label %133, label %.Flow70_crit_edge

.Flow70_crit_edge:                                ; preds = %124
  br label %Flow70

133:                                              ; preds = %124
  %134 = add i32 %93, 1
  %135 = load i32, ptr %129, align 4
  %136 = sext i32 %92 to i64
  %137 = getelementptr [4 x i8], ptr %1, i64 %136
  store i32 %135, ptr %137, align 4
  br label %Flow70

Flow70:                                           ; preds = %.Flow70_crit_edge, %133
  %138 = phi i32 [ %134, %133 ], [ poison, %.Flow70_crit_edge ]
  %139 = phi i1 [ false, %133 ], [ true, %.Flow70_crit_edge ]
  br i1 %139, label %140, label %Flow70._crit_edge

Flow70._crit_edge:                                ; preds = %Flow70
  br label %149

140:                                              ; preds = %Flow70
  %141 = add i32 %94, 1
  %142 = load i32, ptr %126, align 4
  %143 = sext i32 %92 to i64
  %144 = getelementptr [4 x i8], ptr %1, i64 %143
  store i32 %142, ptr %144, align 4
  br label %149

Flow71:                                           ; preds = %.Flow71_crit_edge, %149
  %145 = phi i32 [ %150, %149 ], [ poison, %.Flow71_crit_edge ]
  %146 = phi i32 [ %151, %149 ], [ %94, %.Flow71_crit_edge ]
  %147 = phi i32 [ %132, %149 ], [ %92, %.Flow71_crit_edge ]
  %148 = phi i1 [ false, %149 ], [ true, %.Flow71_crit_edge ]
  br i1 %148, label %Flow71._crit_edge, label %Flow71._crit_edge102

Flow71._crit_edge102:                             ; preds = %Flow71
  br label %91

Flow71._crit_edge:                                ; preds = %Flow71
  br label %98

149:                                              ; preds = %Flow70._crit_edge, %140
  %150 = phi i32 [ %93, %140 ], [ %138, %Flow70._crit_edge ]
  %151 = phi i32 [ %141, %140 ], [ %94, %Flow70._crit_edge ]
  br label %Flow71

Flow59:                                           ; preds = %.Flow59_crit_edge, %183
  %152 = phi i32 [ %189, %183 ], [ poison, %.Flow59_crit_edge ]
  %153 = phi i1 [ false, %183 ], [ true, %.Flow59_crit_edge ]
  br i1 %153, label %Flow60, label %Flow59._crit_edge

Flow59._crit_edge:                                ; preds = %Flow59
  br label %181

Flow60:                                           ; preds = %Flow59
  br label %Flow65

154:                                              ; preds = %Flow65._crit_edge103, %Flow72._crit_edge
  %155 = phi i32 [ %163, %Flow65._crit_edge103 ], [ 0, %Flow72._crit_edge ]
  %156 = icmp slt i32 %155, 9
  br i1 %156, label %157, label %.Flow65_crit_edge

.Flow65_crit_edge:                                ; preds = %154
  br label %Flow65

157:                                              ; preds = %154
  %158 = or disjoint i32 %155, 4
  %159 = or disjoint i32 %155, 3
  %160 = add i32 %155, 8
  %161 = or disjoint i32 %155, 7
  %162 = call i32 @llvm.smin.i32(i32 %161, i32 9)
  br label %165

Flow65:                                           ; preds = %.Flow65_crit_edge, %Flow60
  %163 = phi i32 [ %160, %Flow60 ], [ poison, %.Flow65_crit_edge ]
  %164 = phi i1 [ false, %Flow60 ], [ true, %.Flow65_crit_edge ]
  br i1 %164, label %Flow65._crit_edge, label %Flow65._crit_edge103

Flow65._crit_edge103:                             ; preds = %Flow65
  br label %154

Flow65._crit_edge:                                ; preds = %Flow65
  br label %228

165:                                              ; preds = %Flow64._crit_edge105, %157
  %166 = phi i32 [ %221, %Flow64._crit_edge105 ], [ %155, %157 ]
  %167 = phi i32 [ %219, %Flow64._crit_edge105 ], [ %158, %157 ]
  %168 = phi i32 [ %220, %Flow64._crit_edge105 ], [ %155, %157 ]
  %169 = icmp sle i32 %168, %159
  %170 = icmp sle i32 %167, %162
  %171 = and i1 %169, %170
  br i1 %171, label %198, label %.Flow64_crit_edge

.Flow64_crit_edge:                                ; preds = %165
  br label %Flow64

172:                                              ; preds = %Flow64._crit_edge, %Flow61._crit_edge104
  %173 = phi i32 [ %179, %Flow61._crit_edge104 ], [ %221, %Flow64._crit_edge ]
  %174 = phi i32 [ %178, %Flow61._crit_edge104 ], [ %220, %Flow64._crit_edge ]
  %175 = icmp slt i32 %174, 10
  %176 = icmp sle i32 %174, %159
  %177 = and i1 %175, %176
  br i1 %177, label %190, label %.Flow61_crit_edge

.Flow61_crit_edge:                                ; preds = %172
  br label %Flow61

Flow61:                                           ; preds = %.Flow61_crit_edge, %190
  %178 = phi i32 [ %192, %190 ], [ poison, %.Flow61_crit_edge ]
  %179 = phi i32 [ %191, %190 ], [ poison, %.Flow61_crit_edge ]
  %180 = phi i1 [ false, %190 ], [ true, %.Flow61_crit_edge ]
  br i1 %180, label %Flow61._crit_edge, label %Flow61._crit_edge104

Flow61._crit_edge104:                             ; preds = %Flow61
  br label %172

Flow61._crit_edge:                                ; preds = %Flow61
  br label %181

181:                                              ; preds = %Flow61._crit_edge, %Flow59._crit_edge
  %182 = phi i32 [ %152, %Flow59._crit_edge ], [ %155, %Flow61._crit_edge ]
  %.not2 = icmp sle i32 %182, %162
  br i1 %.not2, label %183, label %.Flow59_crit_edge

.Flow59_crit_edge:                                ; preds = %181
  br label %Flow59

183:                                              ; preds = %181
  %184 = sext i32 %182 to i64
  %185 = getelementptr [4 x i8], ptr %1, i64 %184
  %186 = load i32, ptr %185, align 4
  %187 = sext i32 %182 to i64
  %188 = getelementptr [4 x i8], ptr %2, i64 %187
  store i32 %186, ptr %188, align 4
  %189 = add i32 %182, 1
  br label %Flow59

190:                                              ; preds = %172
  %191 = add i32 %173, 1
  %192 = add i32 %174, 1
  %193 = sext i32 %174 to i64
  %194 = getelementptr [4 x i8], ptr %2, i64 %193
  %195 = load i32, ptr %194, align 4
  %196 = sext i32 %173 to i64
  %197 = getelementptr [4 x i8], ptr %1, i64 %196
  store i32 %195, ptr %197, align 4
  br label %Flow61

198:                                              ; preds = %165
  %199 = sext i32 %168 to i64
  %200 = getelementptr [4 x i8], ptr %2, i64 %199
  %201 = load i32, ptr %200, align 4
  %202 = sext i32 %167 to i64
  %203 = getelementptr [4 x i8], ptr %2, i64 %202
  %204 = load i32, ptr %203, align 4
  %205 = icmp sge i32 %201, %204
  %206 = add i32 %166, 1
  br i1 %205, label %207, label %.Flow63_crit_edge

.Flow63_crit_edge:                                ; preds = %198
  br label %Flow63

207:                                              ; preds = %198
  %208 = add i32 %167, 1
  %209 = load i32, ptr %203, align 4
  %210 = sext i32 %166 to i64
  %211 = getelementptr [4 x i8], ptr %1, i64 %210
  store i32 %209, ptr %211, align 4
  br label %Flow63

Flow63:                                           ; preds = %.Flow63_crit_edge, %207
  %212 = phi i32 [ %208, %207 ], [ poison, %.Flow63_crit_edge ]
  %213 = phi i1 [ false, %207 ], [ true, %.Flow63_crit_edge ]
  br i1 %213, label %214, label %Flow63._crit_edge

Flow63._crit_edge:                                ; preds = %Flow63
  br label %223

214:                                              ; preds = %Flow63
  %215 = add i32 %168, 1
  %216 = load i32, ptr %200, align 4
  %217 = sext i32 %166 to i64
  %218 = getelementptr [4 x i8], ptr %1, i64 %217
  store i32 %216, ptr %218, align 4
  br label %223

Flow64:                                           ; preds = %.Flow64_crit_edge, %223
  %219 = phi i32 [ %224, %223 ], [ poison, %.Flow64_crit_edge ]
  %220 = phi i32 [ %225, %223 ], [ %168, %.Flow64_crit_edge ]
  %221 = phi i32 [ %206, %223 ], [ %166, %.Flow64_crit_edge ]
  %222 = phi i1 [ false, %223 ], [ true, %.Flow64_crit_edge ]
  br i1 %222, label %Flow64._crit_edge, label %Flow64._crit_edge105

Flow64._crit_edge105:                             ; preds = %Flow64
  br label %165

Flow64._crit_edge:                                ; preds = %Flow64
  br label %172

223:                                              ; preds = %Flow63._crit_edge, %214
  %224 = phi i32 [ %167, %214 ], [ %212, %Flow63._crit_edge ]
  %225 = phi i32 [ %215, %214 ], [ %168, %Flow63._crit_edge ]
  br label %Flow64

Flow52:                                           ; preds = %.Flow52_crit_edge, %257
  %226 = phi i32 [ %263, %257 ], [ poison, %.Flow52_crit_edge ]
  %227 = phi i1 [ false, %257 ], [ true, %.Flow52_crit_edge ]
  br i1 %227, label %Flow53, label %Flow52._crit_edge

Flow52._crit_edge:                                ; preds = %Flow52
  br label %255

Flow53:                                           ; preds = %Flow52
  br label %Flow58

228:                                              ; preds = %Flow58._crit_edge, %Flow65._crit_edge
  %229 = phi i32 [ %237, %Flow58._crit_edge ], [ 0, %Flow65._crit_edge ]
  %230 = icmp slt i32 %229, 9
  br i1 %230, label %231, label %.Flow58_crit_edge

.Flow58_crit_edge:                                ; preds = %228
  br label %Flow58

231:                                              ; preds = %228
  %232 = or disjoint i32 %229, 8
  %233 = or disjoint i32 %229, 7
  %234 = add i32 %229, 16
  %235 = or disjoint i32 %229, 15
  %236 = call i32 @llvm.smin.i32(i32 %235, i32 9)
  br label %239

Flow58:                                           ; preds = %.Flow58_crit_edge, %Flow53
  %237 = phi i32 [ %234, %Flow53 ], [ poison, %.Flow58_crit_edge ]
  %238 = phi i1 [ false, %Flow53 ], [ true, %.Flow58_crit_edge ]
  br i1 %238, label %300, label %Flow58._crit_edge

Flow58._crit_edge:                                ; preds = %Flow58
  br label %228

239:                                              ; preds = %Flow57._crit_edge107, %231
  %240 = phi i32 [ %295, %Flow57._crit_edge107 ], [ %229, %231 ]
  %241 = phi i32 [ %293, %Flow57._crit_edge107 ], [ %232, %231 ]
  %242 = phi i32 [ %294, %Flow57._crit_edge107 ], [ %229, %231 ]
  %243 = icmp sle i32 %242, %233
  %244 = icmp sle i32 %241, %236
  %245 = and i1 %243, %244
  br i1 %245, label %272, label %.Flow57_crit_edge

.Flow57_crit_edge:                                ; preds = %239
  br label %Flow57

246:                                              ; preds = %Flow57._crit_edge, %Flow54._crit_edge106
  %247 = phi i32 [ %253, %Flow54._crit_edge106 ], [ %295, %Flow57._crit_edge ]
  %248 = phi i32 [ %252, %Flow54._crit_edge106 ], [ %294, %Flow57._crit_edge ]
  %249 = icmp slt i32 %248, 10
  %250 = icmp sle i32 %248, %233
  %251 = and i1 %249, %250
  br i1 %251, label %264, label %.Flow54_crit_edge

.Flow54_crit_edge:                                ; preds = %246
  br label %Flow54

Flow54:                                           ; preds = %.Flow54_crit_edge, %264
  %252 = phi i32 [ %266, %264 ], [ poison, %.Flow54_crit_edge ]
  %253 = phi i32 [ %265, %264 ], [ poison, %.Flow54_crit_edge ]
  %254 = phi i1 [ false, %264 ], [ true, %.Flow54_crit_edge ]
  br i1 %254, label %Flow54._crit_edge, label %Flow54._crit_edge106

Flow54._crit_edge106:                             ; preds = %Flow54
  br label %246

Flow54._crit_edge:                                ; preds = %Flow54
  br label %255

255:                                              ; preds = %Flow54._crit_edge, %Flow52._crit_edge
  %256 = phi i32 [ %226, %Flow52._crit_edge ], [ %229, %Flow54._crit_edge ]
  %.not = icmp sle i32 %256, %236
  br i1 %.not, label %257, label %.Flow52_crit_edge

.Flow52_crit_edge:                                ; preds = %255
  br label %Flow52

257:                                              ; preds = %255
  %258 = sext i32 %256 to i64
  %259 = getelementptr [4 x i8], ptr %1, i64 %258
  %260 = load i32, ptr %259, align 4
  %261 = sext i32 %256 to i64
  %262 = getelementptr [4 x i8], ptr %2, i64 %261
  store i32 %260, ptr %262, align 4
  %263 = add i32 %256, 1
  br label %Flow52

264:                                              ; preds = %246
  %265 = add i32 %247, 1
  %266 = add i32 %248, 1
  %267 = sext i32 %248 to i64
  %268 = getelementptr [4 x i8], ptr %2, i64 %267
  %269 = load i32, ptr %268, align 4
  %270 = sext i32 %247 to i64
  %271 = getelementptr [4 x i8], ptr %1, i64 %270
  store i32 %269, ptr %271, align 4
  br label %Flow54

272:                                              ; preds = %239
  %273 = sext i32 %242 to i64
  %274 = getelementptr [4 x i8], ptr %2, i64 %273
  %275 = load i32, ptr %274, align 4
  %276 = sext i32 %241 to i64
  %277 = getelementptr [4 x i8], ptr %2, i64 %276
  %278 = load i32, ptr %277, align 4
  %279 = icmp sge i32 %275, %278
  %280 = add i32 %240, 1
  br i1 %279, label %281, label %.Flow56_crit_edge

.Flow56_crit_edge:                                ; preds = %272
  br label %Flow56

281:                                              ; preds = %272
  %282 = add i32 %241, 1
  %283 = load i32, ptr %277, align 4
  %284 = sext i32 %240 to i64
  %285 = getelementptr [4 x i8], ptr %1, i64 %284
  store i32 %283, ptr %285, align 4
  br label %Flow56

Flow56:                                           ; preds = %.Flow56_crit_edge, %281
  %286 = phi i32 [ %282, %281 ], [ poison, %.Flow56_crit_edge ]
  %287 = phi i1 [ false, %281 ], [ true, %.Flow56_crit_edge ]
  br i1 %287, label %288, label %Flow56._crit_edge

Flow56._crit_edge:                                ; preds = %Flow56
  br label %297

288:                                              ; preds = %Flow56
  %289 = add i32 %242, 1
  %290 = load i32, ptr %274, align 4
  %291 = sext i32 %240 to i64
  %292 = getelementptr [4 x i8], ptr %1, i64 %291
  store i32 %290, ptr %292, align 4
  br label %297

Flow57:                                           ; preds = %.Flow57_crit_edge, %297
  %293 = phi i32 [ %298, %297 ], [ poison, %.Flow57_crit_edge ]
  %294 = phi i32 [ %299, %297 ], [ %242, %.Flow57_crit_edge ]
  %295 = phi i32 [ %280, %297 ], [ %240, %.Flow57_crit_edge ]
  %296 = phi i1 [ false, %297 ], [ true, %.Flow57_crit_edge ]
  br i1 %296, label %Flow57._crit_edge, label %Flow57._crit_edge107

Flow57._crit_edge107:                             ; preds = %Flow57
  br label %239

Flow57._crit_edge:                                ; preds = %Flow57
  br label %246

297:                                              ; preds = %Flow56._crit_edge, %288
  %298 = phi i32 [ %241, %288 ], [ %286, %Flow56._crit_edge ]
  %299 = phi i32 [ %289, %288 ], [ %242, %Flow56._crit_edge ]
  br label %Flow57

300:                                              ; preds = %Flow58
  %301 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %302 = insertelement <4 x float> poison, float %301, i64 0
  %303 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %304 = insertelement <4 x float> %302, float %303, i64 1
  %305 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %306 = insertelement <4 x float> %304, float %305, i64 2
  %307 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %308 = insertelement <4 x float> %306, float %307, i64 3
  %309 = fptosi float %303 to i32
  %310 = icmp sge i32 %309, 30
  br i1 %310, label %406, label %.Flow32_crit_edge

.Flow32_crit_edge:                                ; preds = %300
  br label %Flow32

Flow73:                                           ; preds = %.Flow73_crit_edge, %377
  %311 = phi i32 [ %383, %377 ], [ poison, %.Flow73_crit_edge ]
  %312 = phi i1 [ false, %377 ], [ true, %.Flow73_crit_edge ]
  br i1 %312, label %Flow74, label %Flow73._crit_edge

Flow73._crit_edge:                                ; preds = %Flow73
  br label %375

Flow74:                                           ; preds = %Flow73
  br label %Flow79

313:                                              ; preds = %Flow79._crit_edge108, %49
  %314 = phi i32 [ 0, %49 ], [ %321, %Flow79._crit_edge108 ]
  %315 = icmp slt i32 %314, 9
  br i1 %315, label %316, label %.Flow79_crit_edge

.Flow79_crit_edge:                                ; preds = %313
  br label %Flow79

316:                                              ; preds = %313
  %317 = or disjoint i32 %314, 1
  %318 = add i32 %314, 2
  %319 = or disjoint i32 %314, 1
  %320 = call i32 @llvm.smin.i32(i32 %319, i32 9)
  br label %323

Flow79:                                           ; preds = %.Flow79_crit_edge, %Flow74
  %321 = phi i32 [ %318, %Flow74 ], [ poison, %.Flow79_crit_edge ]
  %322 = phi i1 [ false, %Flow74 ], [ true, %.Flow79_crit_edge ]
  br i1 %322, label %Flow79._crit_edge, label %Flow79._crit_edge108

Flow79._crit_edge108:                             ; preds = %Flow79
  br label %313

Flow79._crit_edge:                                ; preds = %Flow79
  br label %80

323:                                              ; preds = %Flow78._crit_edge109, %316
  %324 = phi i32 [ %341, %Flow78._crit_edge109 ], [ %314, %316 ]
  %325 = phi i32 [ %339, %Flow78._crit_edge109 ], [ %317, %316 ]
  %326 = phi i32 [ %340, %Flow78._crit_edge109 ], [ %314, %316 ]
  %327 = icmp sle i32 %326, %314
  %328 = icmp sle i32 %325, %320
  %329 = and i1 %327, %328
  br i1 %329, label %330, label %.Flow78_crit_edge

.Flow78_crit_edge:                                ; preds = %323
  br label %Flow78

330:                                              ; preds = %323
  %331 = sext i32 %326 to i64
  %332 = getelementptr [4 x i8], ptr %2, i64 %331
  %333 = load i32, ptr %332, align 4
  %334 = sext i32 %325 to i64
  %335 = getelementptr [4 x i8], ptr %2, i64 %334
  %336 = load i32, ptr %335, align 4
  %337 = icmp sge i32 %333, %336
  %338 = add i32 %324, 1
  br i1 %337, label %353, label %.Flow77_crit_edge

.Flow77_crit_edge:                                ; preds = %330
  br label %Flow77

Flow78:                                           ; preds = %.Flow78_crit_edge, %343
  %339 = phi i32 [ %344, %343 ], [ poison, %.Flow78_crit_edge ]
  %340 = phi i32 [ %345, %343 ], [ %326, %.Flow78_crit_edge ]
  %341 = phi i32 [ %338, %343 ], [ %324, %.Flow78_crit_edge ]
  %342 = phi i1 [ false, %343 ], [ true, %.Flow78_crit_edge ]
  br i1 %342, label %Flow78._crit_edge, label %Flow78._crit_edge109

Flow78._crit_edge109:                             ; preds = %Flow78
  br label %323

Flow78._crit_edge:                                ; preds = %Flow78
  br label %358

343:                                              ; preds = %Flow77._crit_edge, %348
  %344 = phi i32 [ %325, %348 ], [ %346, %Flow77._crit_edge ]
  %345 = phi i32 [ %349, %348 ], [ %326, %Flow77._crit_edge ]
  br label %Flow78

Flow77:                                           ; preds = %.Flow77_crit_edge, %353
  %346 = phi i32 [ %354, %353 ], [ poison, %.Flow77_crit_edge ]
  %347 = phi i1 [ false, %353 ], [ true, %.Flow77_crit_edge ]
  br i1 %347, label %348, label %Flow77._crit_edge

Flow77._crit_edge:                                ; preds = %Flow77
  br label %343

348:                                              ; preds = %Flow77
  %349 = add i32 %326, 1
  %350 = load i32, ptr %332, align 4
  %351 = sext i32 %324 to i64
  %352 = getelementptr [4 x i8], ptr %1, i64 %351
  store i32 %350, ptr %352, align 4
  br label %343

353:                                              ; preds = %330
  %354 = add i32 %325, 1
  %355 = load i32, ptr %335, align 4
  %356 = sext i32 %324 to i64
  %357 = getelementptr [4 x i8], ptr %1, i64 %356
  store i32 %355, ptr %357, align 4
  br label %Flow77

358:                                              ; preds = %Flow75._crit_edge110, %Flow78._crit_edge
  %359 = phi i32 [ %373, %Flow75._crit_edge110 ], [ %341, %Flow78._crit_edge ]
  %360 = phi i32 [ %372, %Flow75._crit_edge110 ], [ %340, %Flow78._crit_edge ]
  %361 = icmp slt i32 %360, 10
  %362 = icmp sle i32 %360, %314
  %363 = and i1 %361, %362
  br i1 %363, label %364, label %.Flow75_crit_edge

.Flow75_crit_edge:                                ; preds = %358
  br label %Flow75

364:                                              ; preds = %358
  %365 = add i32 %359, 1
  %366 = add i32 %360, 1
  %367 = sext i32 %360 to i64
  %368 = getelementptr [4 x i8], ptr %2, i64 %367
  %369 = load i32, ptr %368, align 4
  %370 = sext i32 %359 to i64
  %371 = getelementptr [4 x i8], ptr %1, i64 %370
  store i32 %369, ptr %371, align 4
  br label %Flow75

Flow75:                                           ; preds = %.Flow75_crit_edge, %364
  %372 = phi i32 [ %366, %364 ], [ poison, %.Flow75_crit_edge ]
  %373 = phi i32 [ %365, %364 ], [ poison, %.Flow75_crit_edge ]
  %374 = phi i1 [ false, %364 ], [ true, %.Flow75_crit_edge ]
  br i1 %374, label %Flow75._crit_edge, label %Flow75._crit_edge110

Flow75._crit_edge110:                             ; preds = %Flow75
  br label %358

Flow75._crit_edge:                                ; preds = %Flow75
  br label %375

375:                                              ; preds = %Flow75._crit_edge, %Flow73._crit_edge
  %376 = phi i32 [ %311, %Flow73._crit_edge ], [ %314, %Flow75._crit_edge ]
  %.not4 = icmp sle i32 %376, %320
  br i1 %.not4, label %377, label %.Flow73_crit_edge

.Flow73_crit_edge:                                ; preds = %375
  br label %Flow73

377:                                              ; preds = %375
  %378 = sext i32 %376 to i64
  %379 = getelementptr [4 x i8], ptr %1, i64 %378
  %380 = load i32, ptr %379, align 4
  %381 = sext i32 %376 to i64
  %382 = getelementptr [4 x i8], ptr %2, i64 %381
  store i32 %380, ptr %382, align 4
  %383 = add i32 %376, 1
  br label %Flow73

common.ret:                                       ; preds = %Flow51.common.ret_crit_edge, %386
  ret void

Flow51:                                           ; preds = %Flow32.Flow51_crit_edge, %392
  %384 = phi float [ %405, %392 ], [ %425, %Flow32.Flow51_crit_edge ]
  %385 = phi i1 [ true, %392 ], [ %426, %Flow32.Flow51_crit_edge ]
  br i1 %385, label %386, label %Flow51.common.ret_crit_edge

Flow51.common.ret_crit_edge:                      ; preds = %Flow51
  br label %common.ret

386:                                              ; preds = %Flow51
  %387 = insertelement <4 x float> <float poison, float poison, float poison, float 1.000000e+00>, float %384, i64 0
  %388 = insertelement <4 x float> %387, float %384, i64 1
  %389 = insertelement <4 x float> %388, float %384, i64 2
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %384, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %384, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %384, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.000000e+00, i32 0)
  br label %common.ret

Flow50:                                           ; preds = %Flow33.Flow50_crit_edge, %410
  %390 = phi float [ %424, %410 ], [ %447, %Flow33.Flow50_crit_edge ]
  %391 = phi i1 [ true, %410 ], [ %448, %Flow33.Flow50_crit_edge ]
  br label %Flow32

392:                                              ; preds = %Flow32
  %393 = load i32, ptr %2, align 4
  %394 = sitofp i32 %393 to float
  %395 = fmul nnan float %394, 1.000000e-01
  %396 = call i1 @llvm.is.fpclass.f32(float %395, /* (sub) */ i32 144)
  %397 = call float @llvm.copysign.f32(float 0.000000e+00, float %395)
  %398 = select i1 %396, float %397, float %395
  %399 = call i1 @llvm.is.fpclass.f32(float %398, /* (sub) */ i32 144)
  %400 = call float @llvm.copysign.f32(float 0.000000e+00, float %398)
  %401 = select i1 %399, float %400, float %398
  %402 = fadd float %401, 5.000000e-01
  %403 = call i1 @llvm.is.fpclass.f32(float %402, /* (sub) */ i32 144)
  %404 = call float @llvm.copysign.f32(float 0.000000e+00, float %402)
  %405 = select i1 %403, float %404, float %402
  br label %Flow51

406:                                              ; preds = %300
  %407 = icmp sge i32 %309, 60
  br i1 %407, label %428, label %.Flow33_crit_edge

.Flow33_crit_edge:                                ; preds = %406
  br label %Flow33

Flow49:                                           ; preds = %Flow34.Flow49_crit_edge, %432
  %408 = phi float [ %446, %432 ], [ %467, %Flow34.Flow49_crit_edge ]
  %409 = phi i1 [ true, %432 ], [ %468, %Flow34.Flow49_crit_edge ]
  br label %Flow33

410:                                              ; preds = %Flow33
  %411 = getelementptr inbounds nuw i8, ptr %2, i64 4
  %412 = load i32, ptr %411, align 4
  %413 = sitofp i32 %412 to float
  %414 = fmul nnan float %413, 1.000000e-01
  %415 = call i1 @llvm.is.fpclass.f32(float %414, /* (sub) */ i32 144)
  %416 = call float @llvm.copysign.f32(float 0.000000e+00, float %414)
  %417 = select i1 %415, float %416, float %414
  %418 = call i1 @llvm.is.fpclass.f32(float %417, /* (sub) */ i32 144)
  %419 = call float @llvm.copysign.f32(float 0.000000e+00, float %417)
  %420 = select i1 %418, float %419, float %417
  %421 = fadd float %420, 5.000000e-01
  %422 = call i1 @llvm.is.fpclass.f32(float %421, /* (sub) */ i32 144)
  %423 = call float @llvm.copysign.f32(float 0.000000e+00, float %421)
  %424 = select i1 %422, float %423, float %421
  br label %Flow50

Flow32:                                           ; preds = %.Flow32_crit_edge, %Flow50
  %425 = phi float [ %390, %Flow50 ], [ poison, %.Flow32_crit_edge ]
  %426 = phi i1 [ %391, %Flow50 ], [ false, %.Flow32_crit_edge ]
  %427 = phi i1 [ false, %Flow50 ], [ true, %.Flow32_crit_edge ]
  br i1 %427, label %392, label %Flow32.Flow51_crit_edge

Flow32.Flow51_crit_edge:                          ; preds = %Flow32
  br label %Flow51

428:                                              ; preds = %406
  %429 = icmp sge i32 %309, 90
  br i1 %429, label %450, label %.Flow34_crit_edge

.Flow34_crit_edge:                                ; preds = %428
  br label %Flow34

Flow48:                                           ; preds = %Flow35.Flow48_crit_edge, %452
  %430 = phi float [ %466, %452 ], [ %474, %Flow35.Flow48_crit_edge ]
  %431 = phi i1 [ true, %452 ], [ %475, %Flow35.Flow48_crit_edge ]
  br label %Flow34

432:                                              ; preds = %Flow34
  %433 = getelementptr inbounds nuw i8, ptr %2, i64 8
  %434 = load i32, ptr %433, align 4
  %435 = sitofp i32 %434 to float
  %436 = fmul nnan float %435, 1.000000e-01
  %437 = call i1 @llvm.is.fpclass.f32(float %436, /* (sub) */ i32 144)
  %438 = call float @llvm.copysign.f32(float 0.000000e+00, float %436)
  %439 = select i1 %437, float %438, float %436
  %440 = call i1 @llvm.is.fpclass.f32(float %439, /* (sub) */ i32 144)
  %441 = call float @llvm.copysign.f32(float 0.000000e+00, float %439)
  %442 = select i1 %440, float %441, float %439
  %443 = fadd float %442, 5.000000e-01
  %444 = call i1 @llvm.is.fpclass.f32(float %443, /* (sub) */ i32 144)
  %445 = call float @llvm.copysign.f32(float 0.000000e+00, float %443)
  %446 = select i1 %444, float %445, float %443
  br label %Flow49

Flow33:                                           ; preds = %.Flow33_crit_edge, %Flow49
  %447 = phi float [ %408, %Flow49 ], [ poison, %.Flow33_crit_edge ]
  %448 = phi i1 [ %409, %Flow49 ], [ false, %.Flow33_crit_edge ]
  %449 = phi i1 [ false, %Flow49 ], [ true, %.Flow33_crit_edge ]
  br i1 %449, label %410, label %Flow33.Flow50_crit_edge

Flow33.Flow50_crit_edge:                          ; preds = %Flow33
  br label %Flow50

450:                                              ; preds = %428
  %451 = icmp sge i32 %309, 120
  br i1 %451, label %470, label %.Flow35_crit_edge

.Flow35_crit_edge:                                ; preds = %450
  br label %Flow35

Flow47:                                           ; preds = %Flow36.Flow47_crit_edge, %473
  br label %Flow35

452:                                              ; preds = %Flow35
  %453 = getelementptr inbounds nuw i8, ptr %2, i64 12
  %454 = load i32, ptr %453, align 4
  %455 = sitofp i32 %454 to float
  %456 = fmul nnan float %455, 1.000000e-01
  %457 = call i1 @llvm.is.fpclass.f32(float %456, /* (sub) */ i32 144)
  %458 = call float @llvm.copysign.f32(float 0.000000e+00, float %456)
  %459 = select i1 %457, float %458, float %456
  %460 = call i1 @llvm.is.fpclass.f32(float %459, /* (sub) */ i32 144)
  %461 = call float @llvm.copysign.f32(float 0.000000e+00, float %459)
  %462 = select i1 %460, float %461, float %459
  %463 = fadd float %462, 5.000000e-01
  %464 = call i1 @llvm.is.fpclass.f32(float %463, /* (sub) */ i32 144)
  %465 = call float @llvm.copysign.f32(float 0.000000e+00, float %463)
  %466 = select i1 %464, float %465, float %463
  br label %Flow48

Flow34:                                           ; preds = %.Flow34_crit_edge, %Flow48
  %467 = phi float [ %430, %Flow48 ], [ poison, %.Flow34_crit_edge ]
  %468 = phi i1 [ %431, %Flow48 ], [ false, %.Flow34_crit_edge ]
  %469 = phi i1 [ false, %Flow48 ], [ true, %.Flow34_crit_edge ]
  br i1 %469, label %432, label %Flow34.Flow49_crit_edge

Flow34.Flow49_crit_edge:                          ; preds = %Flow34
  br label %Flow49

470:                                              ; preds = %450
  %471 = icmp sge i32 %309, 150
  br i1 %471, label %477, label %.Flow36_crit_edge

.Flow36_crit_edge:                                ; preds = %470
  br label %Flow36

Flow46:                                           ; preds = %loop.exit.guard.Flow46_crit_edge, %623
  %472 = phi i1 [ false, %623 ], [ true, %loop.exit.guard.Flow46_crit_edge ]
  br label %Flow45

473:                                              ; preds = %Flow36
  call void @feme.stage.discard(i1 true)
  br label %Flow47

Flow35:                                           ; preds = %.Flow35_crit_edge, %Flow47
  %474 = phi float [ %503, %Flow47 ], [ poison, %.Flow35_crit_edge ]
  %475 = phi i1 [ %504, %Flow47 ], [ false, %.Flow35_crit_edge ]
  %476 = phi i1 [ false, %Flow47 ], [ true, %.Flow35_crit_edge ]
  br i1 %476, label %452, label %Flow35.Flow48_crit_edge

Flow35.Flow48_crit_edge:                          ; preds = %Flow35
  br label %Flow48

477:                                              ; preds = %470
  %478 = icmp sge i32 %309, 180
  br i1 %478, label %506, label %.Flow37_crit_edge

.Flow37_crit_edge:                                ; preds = %477
  br label %Flow37

Flow44:                                           ; preds = %Flow37.Flow44_crit_edge, %488
  %479 = phi float [ %502, %488 ], [ %526, %Flow37.Flow44_crit_edge ]
  %480 = phi float [ %502, %488 ], [ %527, %Flow37.Flow44_crit_edge ]
  %481 = phi i1 [ true, %488 ], [ %528, %Flow37.Flow44_crit_edge ]
  br i1 %481, label %482, label %Flow44.Flow45_crit_edge

Flow44.Flow45_crit_edge:                          ; preds = %Flow44
  br label %Flow45

482:                                              ; preds = %Flow44
  %483 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i1 true)
  %484 = fcmp ogt float %3, %483
  %.inv = xor i1 %484, true
  br i1 %484, label %580, label %._crit_edge111

._crit_edge111:                                   ; preds = %482
  br label %579

Flow43:                                           ; preds = %Flow38.Flow43_crit_edge, %511
  %485 = phi float [ %525, %511 ], [ %550, %Flow38.Flow43_crit_edge ]
  %486 = phi float [ %525, %511 ], [ %551, %Flow38.Flow43_crit_edge ]
  %487 = phi i1 [ true, %511 ], [ %552, %Flow38.Flow43_crit_edge ]
  br label %Flow37

488:                                              ; preds = %Flow37
  %489 = getelementptr inbounds nuw i8, ptr %2, i64 20
  %490 = load i32, ptr %489, align 4
  %491 = sitofp i32 %490 to float
  %492 = fmul nnan float %491, 1.000000e-01
  %493 = call i1 @llvm.is.fpclass.f32(float %492, /* (sub) */ i32 144)
  %494 = call float @llvm.copysign.f32(float 0.000000e+00, float %492)
  %495 = select i1 %493, float %494, float %492
  %496 = call i1 @llvm.is.fpclass.f32(float %495, /* (sub) */ i32 144)
  %497 = call float @llvm.copysign.f32(float 0.000000e+00, float %495)
  %498 = select i1 %496, float %497, float %495
  %499 = fadd float %498, 5.000000e-01
  %500 = call i1 @llvm.is.fpclass.f32(float %499, /* (sub) */ i32 144)
  %501 = call float @llvm.copysign.f32(float 0.000000e+00, float %499)
  %502 = select i1 %500, float %501, float %499
  br label %Flow44

Flow36:                                           ; preds = %.Flow36_crit_edge, %Flow45
  %503 = phi float [ %577, %Flow45 ], [ poison, %.Flow36_crit_edge ]
  %504 = phi i1 [ %578, %Flow45 ], [ false, %.Flow36_crit_edge ]
  %505 = phi i1 [ false, %Flow45 ], [ true, %.Flow36_crit_edge ]
  br i1 %505, label %473, label %Flow36.Flow47_crit_edge

Flow36.Flow47_crit_edge:                          ; preds = %Flow36
  br label %Flow47

506:                                              ; preds = %477
  %507 = icmp sge i32 %309, 210
  br i1 %507, label %530, label %.Flow38_crit_edge

.Flow38_crit_edge:                                ; preds = %506
  br label %Flow38

Flow42:                                           ; preds = %Flow39.Flow42_crit_edge, %535
  %508 = phi float [ %549, %535 ], [ %572, %Flow39.Flow42_crit_edge ]
  %509 = phi float [ %549, %535 ], [ %573, %Flow39.Flow42_crit_edge ]
  %510 = phi i1 [ true, %535 ], [ %574, %Flow39.Flow42_crit_edge ]
  br label %Flow38

511:                                              ; preds = %Flow38
  %512 = getelementptr inbounds nuw i8, ptr %2, i64 24
  %513 = load i32, ptr %512, align 4
  %514 = sitofp i32 %513 to float
  %515 = fmul nnan float %514, 1.000000e-01
  %516 = call i1 @llvm.is.fpclass.f32(float %515, /* (sub) */ i32 144)
  %517 = call float @llvm.copysign.f32(float 0.000000e+00, float %515)
  %518 = select i1 %516, float %517, float %515
  %519 = call i1 @llvm.is.fpclass.f32(float %518, /* (sub) */ i32 144)
  %520 = call float @llvm.copysign.f32(float 0.000000e+00, float %518)
  %521 = select i1 %519, float %520, float %518
  %522 = fadd float %521, 5.000000e-01
  %523 = call i1 @llvm.is.fpclass.f32(float %522, /* (sub) */ i32 144)
  %524 = call float @llvm.copysign.f32(float 0.000000e+00, float %522)
  %525 = select i1 %523, float %524, float %522
  br label %Flow43

Flow37:                                           ; preds = %.Flow37_crit_edge, %Flow43
  %526 = phi float [ %485, %Flow43 ], [ poison, %.Flow37_crit_edge ]
  %527 = phi float [ %486, %Flow43 ], [ poison, %.Flow37_crit_edge ]
  %528 = phi i1 [ %487, %Flow43 ], [ false, %.Flow37_crit_edge ]
  %529 = phi i1 [ false, %Flow43 ], [ true, %.Flow37_crit_edge ]
  br i1 %529, label %488, label %Flow37.Flow44_crit_edge

Flow37.Flow44_crit_edge:                          ; preds = %Flow37
  br label %Flow44

530:                                              ; preds = %506
  %531 = icmp sge i32 %309, 240
  br i1 %531, label %554, label %.Flow39_crit_edge

.Flow39_crit_edge:                                ; preds = %530
  br label %Flow39

Flow41:                                           ; preds = %Flow40.Flow41_crit_edge, %557
  %532 = phi float [ %571, %557 ], [ poison, %Flow40.Flow41_crit_edge ]
  %533 = phi float [ %571, %557 ], [ poison, %Flow40.Flow41_crit_edge ]
  %534 = phi i1 [ true, %557 ], [ false, %Flow40.Flow41_crit_edge ]
  br label %Flow39

535:                                              ; preds = %Flow39
  %536 = getelementptr inbounds nuw i8, ptr %2, i64 28
  %537 = load i32, ptr %536, align 4
  %538 = sitofp i32 %537 to float
  %539 = fmul nnan float %538, 1.000000e-01
  %540 = call i1 @llvm.is.fpclass.f32(float %539, /* (sub) */ i32 144)
  %541 = call float @llvm.copysign.f32(float 0.000000e+00, float %539)
  %542 = select i1 %540, float %541, float %539
  %543 = call i1 @llvm.is.fpclass.f32(float %542, /* (sub) */ i32 144)
  %544 = call float @llvm.copysign.f32(float 0.000000e+00, float %542)
  %545 = select i1 %543, float %544, float %542
  %546 = fadd float %545, 5.000000e-01
  %547 = call i1 @llvm.is.fpclass.f32(float %546, /* (sub) */ i32 144)
  %548 = call float @llvm.copysign.f32(float 0.000000e+00, float %546)
  %549 = select i1 %547, float %548, float %546
  br label %Flow42

Flow38:                                           ; preds = %.Flow38_crit_edge, %Flow42
  %550 = phi float [ %508, %Flow42 ], [ poison, %.Flow38_crit_edge ]
  %551 = phi float [ %509, %Flow42 ], [ poison, %.Flow38_crit_edge ]
  %552 = phi i1 [ %510, %Flow42 ], [ false, %.Flow38_crit_edge ]
  %553 = phi i1 [ false, %Flow42 ], [ true, %.Flow38_crit_edge ]
  br i1 %553, label %511, label %Flow38.Flow43_crit_edge

Flow38.Flow43_crit_edge:                          ; preds = %Flow38
  br label %Flow43

554:                                              ; preds = %530
  %555 = icmp sge i32 %309, 270
  br i1 %555, label %576, label %.Flow40_crit_edge

.Flow40_crit_edge:                                ; preds = %554
  br label %Flow40

Flow40:                                           ; preds = %.Flow40_crit_edge, %576
  %556 = phi i1 [ false, %576 ], [ true, %.Flow40_crit_edge ]
  br i1 %556, label %557, label %Flow40.Flow41_crit_edge

Flow40.Flow41_crit_edge:                          ; preds = %Flow40
  br label %Flow41

557:                                              ; preds = %Flow40
  %558 = getelementptr inbounds nuw i8, ptr %2, i64 32
  %559 = load i32, ptr %558, align 4
  %560 = sitofp i32 %559 to float
  %561 = fmul nnan float %560, 1.000000e-01
  %562 = call i1 @llvm.is.fpclass.f32(float %561, /* (sub) */ i32 144)
  %563 = call float @llvm.copysign.f32(float 0.000000e+00, float %561)
  %564 = select i1 %562, float %563, float %561
  %565 = call i1 @llvm.is.fpclass.f32(float %564, /* (sub) */ i32 144)
  %566 = call float @llvm.copysign.f32(float 0.000000e+00, float %564)
  %567 = select i1 %565, float %566, float %564
  %568 = fadd float %567, 5.000000e-01
  %569 = call i1 @llvm.is.fpclass.f32(float %568, /* (sub) */ i32 144)
  %570 = call float @llvm.copysign.f32(float 0.000000e+00, float %568)
  %571 = select i1 %569, float %570, float %568
  br label %Flow41

Flow39:                                           ; preds = %.Flow39_crit_edge, %Flow41
  %572 = phi float [ %532, %Flow41 ], [ poison, %.Flow39_crit_edge ]
  %573 = phi float [ %533, %Flow41 ], [ poison, %.Flow39_crit_edge ]
  %574 = phi i1 [ %534, %Flow41 ], [ false, %.Flow39_crit_edge ]
  %575 = phi i1 [ false, %Flow41 ], [ true, %.Flow39_crit_edge ]
  br i1 %575, label %535, label %Flow39.Flow42_crit_edge

Flow39.Flow42_crit_edge:                          ; preds = %Flow39
  br label %Flow42

576:                                              ; preds = %554
  call void @feme.stage.discard(i1 true)
  br label %Flow40

Flow45:                                           ; preds = %Flow44.Flow45_crit_edge, %Flow46
  %577 = phi float [ %480, %Flow46 ], [ %479, %Flow44.Flow45_crit_edge ]
  %578 = phi i1 [ %472, %Flow46 ], [ false, %Flow44.Flow45_crit_edge ]
  br label %Flow36

579:                                              ; preds = %._crit_edge111, %580
  br label %581

580:                                              ; preds = %482
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 1.000000e+00, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float 1.000000e+00, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float 1.000000e+00, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.000000e+00, i32 0)
  br label %579

581:                                              ; preds = %Flow._crit_edge, %579
  %582 = phi <2 x float> [ %606, %Flow._crit_edge ], [ splat (float 1.000000e+00), %579 ]
  %583 = phi i32 [ %605, %Flow._crit_edge ], [ 0, %579 ]
  %584 = icmp slt i32 %583, 33
  br i1 %584, label %585, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %581
  br label %Flow

585:                                              ; preds = %581
  %586 = extractelement <2 x float> %582, i64 0
  %587 = fcmp olt float %586, 0.000000e+00
  br i1 %587, label %609, label %.Flow19_crit_edge

.Flow19_crit_edge:                                ; preds = %585
  br label %Flow19

Flow20:                                           ; preds = %.Flow20_crit_edge, %613
  %588 = phi <2 x float> [ %622, %613 ], [ poison, %.Flow20_crit_edge ]
  %589 = phi i1 [ true, %613 ], [ false, %.Flow20_crit_edge ]
  br label %Flow19

590:                                              ; preds = %Flow19
  %591 = extractelement <2 x float> %611, i64 0
  %592 = call i1 @llvm.is.fpclass.f32(float %591, /* (sub) */ i32 144)
  %593 = call float @llvm.copysign.f32(float 0.000000e+00, float %591)
  %594 = select i1 %592, float %593, float %591
  %595 = extractelement <2 x float> %611, i64 1
  %596 = call i1 @llvm.is.fpclass.f32(float %595, /* (sub) */ i32 144)
  %597 = call float @llvm.copysign.f32(float 0.000000e+00, float %595)
  %598 = select i1 %596, float %597, float %595
  %599 = fadd float %594, %598
  %600 = call i1 @llvm.is.fpclass.f32(float %599, /* (sub) */ i32 144)
  %601 = call float @llvm.copysign.f32(float 0.000000e+00, float %599)
  %602 = select i1 %600, float %601, float %599
  %603 = insertelement <2 x float> %611, float %602, i64 0
  %604 = add i32 %583, 1
  br label %Flow21

Flow:                                             ; preds = %.Flow_crit_edge, %Flow21
  %605 = phi i32 [ %624, %Flow21 ], [ poison, %.Flow_crit_edge ]
  %606 = phi <2 x float> [ %625, %Flow21 ], [ poison, %.Flow_crit_edge ]
  %607 = phi i1 [ %610, %Flow21 ], [ true, %.Flow_crit_edge ]
  %608 = phi i1 [ %626, %Flow21 ], [ true, %.Flow_crit_edge ]
  %.inv31 = xor i1 %607, true
  br i1 %608, label %loop.exit.guard, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %581

609:                                              ; preds = %585
  br i1 %.inv, label %613, label %.Flow20_crit_edge

.Flow20_crit_edge:                                ; preds = %609
  br label %Flow20

Flow19:                                           ; preds = %.Flow19_crit_edge, %Flow20
  %610 = phi i1 [ false, %Flow20 ], [ true, %.Flow19_crit_edge ]
  %611 = phi <2 x float> [ %588, %Flow20 ], [ %582, %.Flow19_crit_edge ]
  %612 = phi i1 [ %589, %Flow20 ], [ true, %.Flow19_crit_edge ]
  br i1 %612, label %590, label %Flow19.Flow21_crit_edge

Flow19.Flow21_crit_edge:                          ; preds = %Flow19
  br label %Flow21

613:                                              ; preds = %609
  %614 = extractelement <2 x float> %582, i64 1
  %615 = call i1 @llvm.is.fpclass.f32(float %614, /* (sub) */ i32 144)
  %616 = call float @llvm.copysign.f32(float 0.000000e+00, float %614)
  %617 = select i1 %615, float %616, float %614
  %618 = fadd float %617, 1.000000e+00
  %619 = call i1 @llvm.is.fpclass.f32(float %618, /* (sub) */ i32 144)
  %620 = call float @llvm.copysign.f32(float 0.000000e+00, float %618)
  %621 = select i1 %619, float %620, float %618
  %622 = insertelement <2 x float> %582, float %621, i64 1
  br label %Flow20

623:                                              ; preds = %loop.exit.guard
  call void @feme.stage.discard(i1 true)
  br label %Flow46

Flow21:                                           ; preds = %Flow19.Flow21_crit_edge, %590
  %624 = phi i32 [ %604, %590 ], [ poison, %Flow19.Flow21_crit_edge ]
  %625 = phi <2 x float> [ %603, %590 ], [ %611, %Flow19.Flow21_crit_edge ]
  %626 = phi i1 [ false, %590 ], [ true, %Flow19.Flow21_crit_edge ]
  br label %Flow

loop.exit.guard:                                  ; preds = %Flow
  br i1 %.inv31, label %623, label %loop.exit.guard.Flow46_crit_edge

loop.exit.guard.Flow46_crit_edge:                 ; preds = %loop.exit.guard
  br label %Flow46
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1) #3

attributes #0 = { nocallback nofree nosync nounwind willreturn }
attributes #1 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #2 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #3 = { nounwind willreturn memory(argmem: read) }

!feme.cpu.resources = !{!5}
!feme.cpu.bound_resources = !{!6}

!0 = !{i32 11, i32 15}
!1 = !{!0}
!2 = !{i32 30, i32 0}
!3 = !{!2}
!4 = !{[208 x i8] c"\08\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
!5 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!6 = !{!"main", i32 1, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0}
