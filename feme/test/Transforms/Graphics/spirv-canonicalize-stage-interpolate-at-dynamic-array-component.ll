; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (Roadmap L502) A pull-model interpolation of one constant lane/component
; of a vector-typed element living inside a dynamically row-indexed array
; (GLSL `interpolateAtCentroid(arr[dynamicIdx].y)`, the shape
; `dEQP-VK.pipeline.*.multisample_interpolation.
; nonuniform_interpolant_indexing.*` compiles to) resolves its own
; `Interpolant` address (`resolveInterpolantAddress`,
; SPIRVToLLVMPatterns.cpp) through a *pair* of nested `getelementptr`s
; rather than one combined access chain: an inner, `[N x i8]`-sourced GEP
; selecting the dynamic row, wrapped by an outer, plain-`i8`-sourced GEP
; with one *constant* index giving the leaf component's raw byte offset
; within that row (`.y` of a `vec2` -> byte offset 4).
; `getDynamicRowIndexedAccess` only ever recognized the inner GEP by
; itself (an ordinary whole-row dynamic array index, with no further
; lane-select wrapper) -- reaching the outer wrapper instead left the
; `feme.spirv.interpolate_at_centroid.*` marker call unconverted, which
; `feme::cpu::SIMDizePass` then rejected outright as an "unsupported
; divergent call" compile-time abort, since it has no generic rewrite for
; an arbitrary unrecognized call (roadmap milestone 7).
;
; `resolveStageIOAccess` now peels the outer constant-byte-offset wrapper
; off first (using the marker call's own result type to convert the raw
; byte offset back into a lane index), letting the existing dynamic-row
; recognition resolve what is left exactly as it already does for the
; one-level shape.

target triple = "spirv-unknown-vulkan1.3-pixel"

; `sample in vec2 inPosScreenArr[10]` at location 0.
@in_arr = external addrspace(7) constant [10 x <2 x float>], !spirv.Decorations !0

declare float @feme.spirv.interpolate_at_centroid.f32(ptr addrspace(7))

; CHECK-LABEL: define float @main(i32 %idx)
define float @main(i32 %idx) #0 {
  %idx64 = sext i32 %idx to i64
  %row = getelementptr [8 x i8], ptr addrspace(7) @in_arr, i64 %idx64
  %elem = getelementptr i8, ptr addrspace(7) %row, i64 4
  ; CHECK: [[ROW:%.*]] = trunc i64 %idx64 to i32
  ; CHECK: call float @feme.stage.interpolate.at.centroid.f32(i32 0, i32 [[ROW]], i32 1)
  %v = call float @feme.spirv.interpolate_at_centroid.f32(ptr addrspace(7) %elem)
  ret float %v
}

attributes #0 = { "feme.shader.stage"="fragment" }

!0 = !{!1, !2}
!1 = !{i32 30, i32 0} ; Location 0
!2 = !{i32 17}        ; Sample
