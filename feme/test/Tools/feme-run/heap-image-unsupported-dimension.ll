; RUN: split-file %s %t
; RUN: not feme-run --reference --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll 2>&1 | FileCheck %s

; Roadmap R31's heap YAML `images` entry rejects a multisample dimension:
; multisampling is a later milestone (G4, roadmap step R33 in
; feme/docs/Roadmap.md), so its layout arithmetic is not implemented yet --
; see `ImageEntry`'s own comment in feme-run.cpp.

; CHECK: feme-run: heap entry image 'dimension: 2d-ms' is not yet supported

;--- shader.ll
define void @main() #0 {
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }

;--- heap.yaml
images:
  - index: 0
    dimension: 2d-ms
    extent: [4, 4]
    format: r32g32b32a32_float
