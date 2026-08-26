; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (Roadmap H2e) Unlike DXIL's `loadInput`/`storeOutput` split (where an
; output is genuinely write-only), SPIR-V's `Output` storage class permits
; reading back a value already written in the same invocation -- the shape
; `dEQP-VK.multiview.input_instance`'s own vertex shader takes (a compound
; `gl_Position.y += 1.0f;` guarded by an `if`). Such a read-back resolves
; directly to the reaching stored value instead of a (semantically
; wrong-direction) `feme.stage.input.load`, correctly threading through a
; real control-flow join via `PromoteMemToReg`.

target triple = "spirv-unknown-vulkan1.3-vertex"

@out_var = external addrspace(8) global float, !spirv.Decorations !0

; CHECK-LABEL: define void @main(i1 %cond)
define void @main(i1 %cond) #0 {
entry:
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.000000e+00, i32 0)
  store float 1.000000e+00, ptr addrspace(8) @out_var
  br i1 %cond, label %if.then, label %if.end

if.then:
  ; The read-back resolves to the entry block's own stored value, with no
  ; `feme.stage.input.load` at all.
  ; CHECK-NOT: feme.stage.input.load
  %v = load float, ptr addrspace(8) @out_var
  %v2 = fadd float %v, 1.000000e+00
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %v2, i32 0)
  store float %v2, ptr addrspace(8) @out_var
  br label %if.end

if.end:
  ret void
}

attributes #0 = { "feme.shader.stage"="vertex" }

!0 = !{!1}
!1 = !{i32 30, i32 0} ; Location 0
