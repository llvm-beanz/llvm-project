; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; A fragment input block whose only member is an array of vectors
; (`in LocationBlock { uvec4 elements[19]; } loc`, as in
; `mesh_shader.ext.properties.max_mesh_output_size_*`) is a struct-typed
; global; a dynamic element index must still resolve to a row access.

target triple = "spirv-unknown-vulkan1.3-pixel"

@in_blk = external addrspace(7) constant <{ [19 x <4 x i32>] }>, !spirv.Decorations !0

; CHECK-LABEL: define <4 x i32> @main(i32 %idx)
define <4 x i32> @main(i32 %idx) #0 {
  %idx64 = zext i32 %idx to i64
  %p = getelementptr [16 x i8], ptr addrspace(7) @in_blk, i64 %idx64
  ; CHECK-NOT: addrspace(7)
  ; CHECK: call {{.*}} @feme.stage.input.load
  %v = load <4 x i32>, ptr addrspace(7) %p, align 4
  ret <4 x i32> %v
}

attributes #0 = { "feme.shader.stage"="fragment" }

!0 = !{!1, !2}
!1 = !{i32 30, i32 0} ; Location 0
!2 = !{i32 14}        ; Flat
