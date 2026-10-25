; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 0}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.0
; RUN: obj2yaml %t.0 | FileCheck %s --check-prefixes=V0,NOHASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 1}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.1
; RUN: obj2yaml %t.1 | FileCheck %s --check-prefixes=V1,NOHASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 2}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.2
; RUN: obj2yaml %t.2 | FileCheck %s --check-prefixes=V1,NOHASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 3}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.3
; RUN: obj2yaml %t.3 | FileCheck %s --check-prefixes=V1,NOHASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 4}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.4
; RUN: obj2yaml %t.4 | FileCheck %s --check-prefixes=V1,NOHASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 5}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.5
; RUN: obj2yaml %t.5 | FileCheck %s --check-prefixes=V1,HASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 6}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.6
; RUN: obj2yaml %t.6 | FileCheck %s --check-prefixes=V2,HASH
; RUN: sed 's/!1 = !{i32 1, i32 8}/!1 = !{i32 1, i32 7}/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.7
; RUN: obj2yaml %t.7 | FileCheck %s --check-prefixes=V2,HASH
; RUN: dxil-as --filetype=dxcontainer %S/compute-container.ll -o %t.8
; RUN: obj2yaml %t.8 | FileCheck %s --check-prefixes=V3,HASH

; V0: Name: PSV0
; V0-NEXT: Size: 52
; V0: Version: 0
; V0: MinimumWaveLaneCount: 0
; V0-NEXT: MaximumWaveLaneCount: 4294967295
; V0: ResourceStride: 16

; V1: Name: PSV0
; V1: Version: 1
; V1-NEXT: ShaderStage: 5
; V1: ResourceStride: 16
; V1-NEXT: RuntimeInfoSize: 36

; V2: Name: PSV0
; V2: Version: 2
; V2-NEXT: ShaderStage: 5
; V2: NumThreadsX: 8
; V2-NEXT: NumThreadsY: 2
; V2-NEXT: NumThreadsZ: 1
; V2: ResourceStride: 24
; V2-NEXT: RuntimeInfoSize: 48

; V3: Name: PSV0
; V3: Version: 3
; V3-NEXT: ShaderStage: 5
; V3: EntryName: main
; V3-NEXT: ResourceStride: 24
; V3-NEXT: RuntimeInfoSize: 52

; NOHASH-NOT: Name: HASH
; NOHASH: Name: DXIL
; HASH: Name: HASH
; HASH: Name: DXIL
