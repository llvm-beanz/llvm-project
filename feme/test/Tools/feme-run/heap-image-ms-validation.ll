; RUN: split-file %s %t
; RUN: not feme-run --heap=%t/zero.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=SAMPLES
; RUN: not feme-run --reference --heap=%t/non-ms.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=SAMPLES
; RUN: not feme-run --heap=%t/empty.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=EXTENT
; RUN: not feme-run --heap=%t/depth.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=EXTENT
; RUN: not feme-run --heap=%t/zero-layers.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=LAYERS
; RUN: not feme-run --heap=%t/non-array.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=ARRAY
; RUN: not feme-run --heap=%t/row-overflow.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=OVERFLOW
; RUN: not feme-run --heap=%t/slice-overflow.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=OVERFLOW
; RUN: not feme-run --heap=%t/layer-overflow.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=OVERFLOW
; RUN: not feme-run --heap=%t/host-limit.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=HOST-LIMIT
; RUN: not feme-run --heap=%t/bound-zero.yaml %t/shader.ll 2>&1 | FileCheck %s --check-prefix=SAMPLES
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: not feme-run --object --heap=%t/zero.yaml %t/shader.o 2>&1 | FileCheck %s --check-prefix=SAMPLES
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; SAMPLES: heap entry image 0 requires a positive 'sample-count' (1 for non-MS dimensions)
; EXTENT: heap entry image 0 requires nonzero XY multisample extents
; LAYERS: heap entry image 0 multisample array requires positive 'array-layers'
; ARRAY: heap entry image 0 sets 'array-layers' but 'dimension: 2d-ms' is not an array dimension
; OVERFLOW: heap entry image 0 subresource size overflows
; HOST-LIMIT: heap entry image 0 subresource size exceeds host allocation limits
;--- shader.ll
define void @main() #0 {
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
;--- zero.yaml
images:
  - {index: 0, dimension: 2d-ms, sample-count: 0, format: r32_uint}
;--- non-ms.yaml
images:
  - {index: 0, dimension: 2d, sample-count: 2, format: r32_uint}
;--- empty.yaml
images:
  - {index: 0, dimension: 2d-ms, extent: [0, 1], format: r32_uint}
;--- depth.yaml
images:
  - {index: 0, dimension: 2d-ms, extent: [1, 1, 2], format: r32_uint}
;--- zero-layers.yaml
images:
  - {index: 0, dimension: 2d-ms-array, array-layers: 0, format: r32_uint}
;--- non-array.yaml
images:
  - {index: 0, dimension: 2d-ms, array-layers: 2, format: r32_uint}
;--- row-overflow.yaml
images:
  - {index: 0, dimension: 2d-ms, extent: [4294967295, 1], sample-count: 4294967295, format: r32g32b32a32_uint}
;--- slice-overflow.yaml
images:
  - {index: 0, dimension: 2d-ms, extent: [4294967295, 4294967295], sample-count: 2, format: r32_uint}
;--- layer-overflow.yaml
images:
  - {index: 0, dimension: 2d-ms-array, extent: [1073741824, 1073741824], sample-count: 2, array-layers: 3, format: r32_uint}
;--- host-limit.yaml
images:
  - {index: 0, dimension: 2d-ms, extent: [805306368, 805306368], sample-count: 4, format: r32_uint}
;--- bound-zero.yaml
image-bindings:
  - space: 0
    register: 0
    entries:
      - {index: 0, dimension: 2d-ms, sample-count: 0, format: r32_uint}
