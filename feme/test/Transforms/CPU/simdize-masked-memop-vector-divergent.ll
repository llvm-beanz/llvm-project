; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6g-b-a-i-a-i-a: a vector-typed `feme.cpu.masked.store.*` value --
; a mesh entry point's own `gl_PrimitiveTriangleIndicesEXT[...] =
; uvec3(...)` write takes exactly this shape, since (unlike an ordinary
; output element write) it has no canonicalized `feme.stage.*` op of its
; own to become a `feme.cpu.resource.*`/masked-output-store call instead
; (see MeshOutputWrapper.h's file comment) -- is decomposed into
; per-component wide values exactly like a matched resource-store call's
; stored value, rather than being rejected by
; `checkVectorDecompositionSupported` as a divergent vector "used outside a
; supported ... pattern". `llvm.masked.scatter` has no vector-of-vector
; form to widen a `<3 x i32>` per-lane value into, so each lane's own
; reassembled vector is written individually instead, guarded by a
; load-select-store idiom (see `widenMaskedStore` in SIMDize.cpp).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <3 x i32>>
; CHECK: t:
; CHECK: select i1 %lane.mask, <3 x i32> %{{.*}}, <3 x i32> %{{.*}}
; CHECK-COUNT-4: store <3 x i32> %{{.*}}, ptr %lane.ptr{{[0-9]*}}
define void @main(ptr %p) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %base = mul i32 %tid, 3
  %e1 = add i32 %base, 1
  %e2 = add i32 %base, 2
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %off = zext i32 %tid to i64
  %addr = getelementptr <3 x i32>, ptr %p, i64 %off
  %v0 = insertelement <3 x i32> poison, i32 %base, i32 0
  %v1 = insertelement <3 x i32> %v0, i32 %e1, i32 1
  %v2 = insertelement <3 x i32> %v1, i32 %e2, i32 2
  store <3 x i32> %v2, ptr %addr
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
