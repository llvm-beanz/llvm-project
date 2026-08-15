; RUN: not feme-opt --llvm -passes=feme-dxil-raise-metadata -S %s 2>&1 | FileCheck %s

; A stage-specific shader model profile (`cs` here) fixes the module triple's
; environment, so an entry point whose own `ShaderKind` property (tag 8) names
; a different stage is malformed input. "Stage identity" in
; feme/docs/FeMeGraphicsDesign.md has the imported stage *validated* against
; the module triple's environment rather than one of the two silently winning.

target triple = "dxil-ms-dx"

; CHECK: error: feme-dxil-raise-metadata: entry point 'main' declares stage 'vertex', which disagrees with the module's 'compute' shader model
define void @main() {
  ret void
}

!dx.shaderModel = !{!0}
!dx.entryPoints = !{!1}

!0 = !{!"cs", i32 6, i32 5}
!1 = !{ptr @main, !"main", null, null, !2}
; ShaderKind 1 is `vertex`; the module says `cs`.
!2 = !{i32 8, i32 1}
