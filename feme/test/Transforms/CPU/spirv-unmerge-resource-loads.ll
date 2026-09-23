; RUN: feme-opt --llvm -passes=feme-cpu-spirv-unmerge-resource-loads -S %s | FileCheck %s

; Covers feme::cpu::SPIRVUnmergeResourceLoadsPass (roadmap L174/L155):
; undoes InstCombine's `foldPHIArgLoadIntoPHI` canonicalization for a
; `load` whose per-predecessor pointer operands are each a distinct
; `llvm.spv.resource.getpointer` call -- the shape a graphics-stage
; pipeline's own upstream `InstCombinePass` (run by
; `feme::graphics::UnrollConstantTripCountStageLoopsPass`, see that
; pass's header comment) produces from the CTS's own `quadrant_id`-based
; if/else-if resource-access idiom, and that
; `feme::cpu::SPIRVResourceLoweringPass`'s own "flat access" matchers
; otherwise reject outright (see spirv-resource-lowering-raw-buffer.ll's
; own accepted shape for comparison: a `getpointer` call's result used
; directly by a `load`/`store` in that call's own block).

target triple = "spirv-unknown-vulkan-fragment"

; CHECK-LABEL: define <4 x float> @merged_pointer_phi(
define <4 x float> @merged_pointer_phi(i32 %idx) {
entry:
  switch i32 %idx, label %case2 [
    i32 0, label %case0
    i32 1, label %case1
  ]

case0:
  %h0 = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  %p0 = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h0, <2 x i32> <i32 1, i32 1>)
  br label %merge

case1:
  %h1 = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  %p1 = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h1, <2 x i32> <i32 2, i32 2>)
  br label %merge

case2:
  %h2 = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  %p2 = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h2, <2 x i32> <i32 3, i32 3>)
  br label %merge

merge:
  ; The pre-rewrite shape being tested: one shared `load` reading through
  ; a `phi ptr` of the three branches' own `getpointer` results.
  %p = phi ptr [ %p0, %case0 ], [ %p1, %case1 ], [ %p2, %case2 ]
  %v = load <4 x float>, ptr %p, align 4
  ret <4 x float> %v
}

; The `getpointer`/`load` pair now lives in each of the three branches
; (no more shared, indirected `load`); each entry point's own
; `SPIRVResourceLoweringPass` matcher can see a load directly using its
; own block's own `getpointer` result.
; CHECK: case0:
; CHECK: [[P0:%.+]] = call ptr @llvm.spv.resource.getpointer{{.*}}(target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h0,
; CHECK-NEXT: load <4 x float>, ptr [[P0]], align 4
; CHECK: case1:
; CHECK: [[P1:%.+]] = call ptr @llvm.spv.resource.getpointer{{.*}}(target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h1,
; CHECK-NEXT: load <4 x float>, ptr [[P1]], align 4
; CHECK: case2:
; CHECK: [[P2:%.+]] = call ptr @llvm.spv.resource.getpointer{{.*}}(target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h2,
; CHECK-NEXT: load <4 x float>, ptr [[P2]], align 4
; CHECK: merge:
; CHECK-NOT: phi ptr
; CHECK: phi <4 x float>
declare target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
    @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
    target("spirv.Image", float, 1, 0, 0, 0, 2, 4), <2 x i32>)

; A `phi ptr` whose incoming values are *not* all `getpointer` calls (here,
; one incoming value is an ordinary `alloca`) is left untouched -- this
; pass only recognizes the specific resource-handle shape it exists to fix.
; CHECK-LABEL: define float @non_resource_pointer_phi(
; CHECK: phi ptr
define float @non_resource_pointer_phi(i1 %cond) {
entry:
  %local = alloca <4 x float>
  br i1 %cond, label %a, label %b

a:
  %h = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  %p = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h, <2 x i32> <i32 1, i32 1>)
  br label %merge

b:
  br label %merge

merge:
  %ptr = phi ptr [ %p, %a ], [ %local, %b ]
  %v = load <4 x float>, ptr %ptr, align 4
  %s = extractelement <4 x float> %v, i64 0
  ret float %s
}

; A `load` reading through a resource-pointer `phi` but *sunk* into a
; later block (here, only one arm of an unrelated, subsequent `if`
; actually uses the loaded value) is left untouched: `%p`'s incoming
; blocks (`case0`/`case1`) are only guaranteed to be the real CFG
; predecessors of `%p`'s own parent block (`merge`), not of `%use` (the
; sunk load's block) -- rewriting here would produce a new `PHINode` in
; `%use` whose incoming blocks don't match `%use`'s actual predecessor
; (`merge`), i.e. invalid IR. Found via CTS's
; `binding_model.shader_access.*vertex_fragment*` (roadmap L175/L176):
; the combined-vertex+fragment shape's own extra, outer `if` (choosing
; between this stage's own resource access and a passed-through value
; from the other stage) is exactly this shape, and an earlier revision
; of this pass silently produced invalid IR for it instead of leaving it
; untouched.
; CHECK-LABEL: define float @sunk_load_after_merge(
; CHECK: phi ptr
define float @sunk_load_after_merge(i32 %idx, i1 %cond) {
entry:
  switch i32 %idx, label %case1 [
    i32 0, label %case0
  ]

case0:
  %h0 = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  %p0 = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h0, <2 x i32> <i32 1, i32 1>)
  br label %merge

case1:
  %h1 = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_0_0_0_2_4t(
          i32 0, i32 0, i32 1, i32 0, ptr null)
  %p1 = call ptr @llvm.spv.resource.getpointer.p0.tspirv.Image_f32_1_0_0_0_2_4t.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %h1, <2 x i32> <i32 2, i32 2>)
  br label %merge

merge:
  %p = phi ptr [ %p0, %case0 ], [ %p1, %case1 ]
  br i1 %cond, label %use, label %skip

use:
  %v = load <4 x float>, ptr %p, align 4
  %s = extractelement <4 x float> %v, i64 0
  ret float %s

skip:
  ret float 0.000000e+00
}
