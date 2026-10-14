; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (Roadmap L419(k)) A `BuiltIn`-decorated `Output` global can carry a
; static SPIR-V `initial_value` (`OpVariable ... Output %initializer`)
; with zero real `OpStore`/`OpLoad` anywhere in the entry's body -- the
; shape every CTS
; `dEQP-VK.renderpasses.renderpass1.depth_stencil_write_conditions.
; depth_{demote,kill,terminate}_initialize_*` case takes for its
; `gl_FragDepth` global (declared `= 0.2` rather than conditionally
; written at runtime). Per the Vulkan/SPIR-V spec, that literal is the
; variable's value from the start of the invocation, exactly as if an
; implicit store executed before the entry point's first instruction.
; Three independent bugs previously made this silently read back as 0.0
; instead: (1) `StageIOGlobalVariablePattern` (SPIRVToLLVMPatterns.cpp)
; discarded `initial_value` when lowering to `llvm.mlir.global`, (2) this
; pass's own unused-global signature-discovery fallback unconditionally
; excluded every `BuiltIn` global, never building a `SignatureElement`
; for it at all, and (3) even with a `SignatureElement` in place, nothing
; ever seeded `FSOutput`'s per-invocation storage with the initializer's
; value, since the ordinary store-rewriting walk below only ever visits
; a *real* `StoreInst`. This test exercises the fix for (2) and (3): a
; synthetic initial `store` -- matching the global's own LLVM
; initializer, already correctly preserved by (1)'s fix -- gets inserted
; at the very top of the entry block, so it flows through the same,
; already-correct `feme.stage.output.store` rewrite an explicit
; `OpStore` would.

target triple = "spirv-unknown-vulkan1.3-pixel"

; CHECK: @frag_depth = addrspace(8) global float 2.000000e-01
@frag_depth = addrspace(8) global float 2.000000e-01, !spirv.Decorations !0

; CHECK-LABEL: define void @main()
define void @main() #0 {
  ; The synthetic initial store (matching the global's own initializer)
  ; is rewritten into an ordinary `feme.stage.output.store` call, exactly
  ; like a real `OpStore` would be -- giving `gl_FragDepth` a real,
  ; non-zero value in `FSOutput`'s storage even though this entry never
  ; itself loads or stores the global.
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 2.000000e-01, i32 0)
  ; CHECK-NOT: load {{.*}} @frag_depth
  ; CHECK-NOT: store {{.*}} @frag_depth
  ret void
}

attributes #0 = { "feme.shader.stage"="fragment" }

!0 = !{!1}
!1 = !{i32 11, i32 22} ; BuiltIn FragDepth
