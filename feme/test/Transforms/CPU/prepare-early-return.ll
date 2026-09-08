; RUN: feme-opt --llvm -passes=feme-cpu-prepare -S %s | FileCheck %s

; Roadmap L71: a divergent early-return bounds check (the common GLSL/HLSL
; `if (cond) return;` idiom, e.g. a compute shader's own
; `if (gid.x >= size.x) return;`) used to leave Phase 1 (`PreparePass`) with
; more than one `ret` block -- a shape `StructurizeCFG` cannot represent as
; an ordinary reconverging branch (it can only route a branch to another
; block that itself eventually reconverges, not to a block that exits the
; function outright), so the resulting CFG failed
; `feme::cpu::verifyStructured`'s "every divergent branch has a
; reconvergence point" postcondition down the line, in Phase 3
; (Linearization). `feme::cpu::unifyDivergentExitNodes` now merges every
; `ret` block into one *before* `StructurizeCFG` runs, turning the
; early-return idiom into an ordinary reconverging `if` that
; `StructurizeCFG` already knows how to structure -- see
; `feme/lib/Transforms/CPU/Prepare.cpp`'s own updated doc comment.

; CHECK-LABEL: define void @main(
; There is exactly one `ret` left, and it postdominates every block:
; CHECK-NOT: ret void
; CHECK: ret void
; CHECK-NOT: ret void
define void @main(i32 %gid, i32 %size) #0 {
entry:
  %c = icmp sge i32 %gid, %size
  br i1 %c, label %early_ret, label %body
early_ret:
  ret void
body:
  br label %end
end:
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
