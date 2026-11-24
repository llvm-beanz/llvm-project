; RUN: split-file %s %t
; RUN: not feme-run --reference --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll 2>&1 | FileCheck %s

; Unknown image dimension spellings remain rejected.

; CHECK: feme-run: unknown heap entry image 'dimension': '4d'

;--- shader.ll
define void @main() #0 {
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }

;--- heap.yaml
images:
  - index: 0
    dimension: 4d
    extent: [4, 4]
    format: r32g32b32a32_float
