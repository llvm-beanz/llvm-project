; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; A valid, in-range, power-of-two --wave-size resolves without diagnostics
; and produces a real object file for the host target (the FeMe CPU target,
; see feme/docs/FeMeCPUDesign.md's "Wave Size Selection" and "Kernel ABI"
; sections).
; RUN: feme --target=%feme_host_triple --wave-size=8 %t.dxcontainer -o %t.o 2>&1 | FileCheck %s --check-prefix=NO-DIAG --allow-empty
; RUN: llvm-readobj --file-headers %t.o
; NO-DIAG-NOT: warning
; NO-DIAG-NOT: error

; A non-power-of-two --wave-size is rejected with a diagnostic naming the
; requirement, regardless of where it came from.
; RUN: not feme --target=%feme_host_triple --wave-size=6 %t.dxcontainer -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=NOT-POW2
; NOT-POW2: power of two

; An out-of-range --wave-size is likewise rejected.
; RUN: not feme --target=%feme_host_triple --wave-size=256 %t.dxcontainer -o %t.bad.o 2>&1 | FileCheck %s --check-prefix=OUT-OF-RANGE
; OUT-OF-RANGE: power of two

; --wave-size only has meaning for the CPU target: for any other --target it
; is ignored, with a diagnostic, rather than silently affecting codegen.
; RUN: feme --target=dxil --wave-size=8 %t.dxcontainer -o %t.dxil.o 2>&1 | FileCheck %s --check-prefix=IGNORED
; IGNORED: warning: --wave-size is ignored for target

target triple = "dxil-unknown-shadermodel6.5-compute"

; Deliberately does no resource access or thread-id query: the CPU pipeline's
; resource/builtin lowering passes are future roadmap milestones (see
; feme/docs/FeMeCPUDesign.md), so this only exercises wave size resolution
; and that a raised, resource-free shader still reaches the host backend.
define void @main() #0 {
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}
