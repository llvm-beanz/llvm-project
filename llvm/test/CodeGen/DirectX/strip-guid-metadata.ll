; RUN: opt -S -dxil-prepare %s | FileCheck %s --implicit-check-not=!guid

target triple = "dxil-unknown-shadermodel6.0-compute"

; CHECK: @scratch = addrspace(3) global [64 x i32] undef, align 4
@scratch = addrspace(3) global [64 x i32] undef, align 4, !guid !0

; CHECK: define void @main() !dbg ![[SP:[0-9]+]]
define void @main() #0 !dbg !5 !guid !1 {
; CHECK: ret void, !dbg ![[LOC:[0-9]+]]
  ret void, !dbg !7
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,4,2" }

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!8}
!dx.valver = !{!9}
!0 = !{i64 123}
!1 = !{i64 456}
!2 = distinct !DICompileUnit(language: DW_LANG_C99, file: !3, producer: "test",
                           isOptimized: false, runtimeVersion: 0,
                           emissionKind: FullDebug)
!3 = !DIFile(filename: "shader.hlsl", directory: ".")
!4 = !DISubroutineType(types: !6)
; CHECK: ![[SP]] = distinct !DISubprogram(name: "main"
!5 = distinct !DISubprogram(name: "main", scope: !3, file: !3, line: 1,
                          type: !4, scopeLine: 1,
                          spFlags: DISPFlagDefinition, unit: !2)
!6 = !{null}
; CHECK: ![[LOC]] = !DILocation(line: 2, column: 1, scope: ![[SP]])
!7 = !DILocation(line: 2, column: 1, scope: !5)
!8 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !{i32 1, i32 9}
