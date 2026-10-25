; RUN: dxil-as --filetype=dxcontainer %S/compute-container.ll -o %t.0
; RUN: obj2yaml %t.0 | FileCheck %s -D#MODEL=0
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 1}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 1/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.1
; RUN: obj2yaml %t.1 | FileCheck %s -D#MODEL=1
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 2}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 2/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.2
; RUN: obj2yaml %t.2 | FileCheck %s -D#MODEL=2
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 3}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 3/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.3
; RUN: obj2yaml %t.3 | FileCheck %s -D#MODEL=3
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 4}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 4/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.4
; RUN: obj2yaml %t.4 | FileCheck %s -D#MODEL=4
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 5}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 5/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.5
; RUN: obj2yaml %t.5 | FileCheck %s -D#MODEL=5
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 6}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 6/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.6
; RUN: obj2yaml %t.6 | FileCheck %s -D#MODEL=6
; RUN: sed -e 's/!0 = !{i32 1, i32 0}/!0 = !{i32 1, i32 7}/' -e 's/!"cs", i32 6, i32 0/!"cs", i32 6, i32 7/' %S/compute-container.ll | dxil-as --filetype=dxcontainer -o %t.7
; RUN: obj2yaml %t.7 | FileCheck %s -D#MODEL=7

; CHECK: Name: DXIL
; CHECK: MajorVersion: 6
; CHECK-NEXT: MinorVersion: [[#MODEL]]
; CHECK-NEXT: ShaderKind: 5
; CHECK: DXILMajorVersion: 1
; CHECK-NEXT: DXILMinorVersion: [[#MODEL]]
