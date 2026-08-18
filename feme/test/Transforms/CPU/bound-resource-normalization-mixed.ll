; RUN: feme-opt --llvm -passes=feme-cpu-normalize-bound-resources -S %s | FileCheck %s

; Covers a shader mixing a traditional binding with a native (already
; bindless) dynamic heap access -- roadmap milestone 11's completion-test
; shape. The bound handle is rewritten into the reserved prefix (base 0,
; since it is the only accepted range) and the *native* `handlefromheap`
; call's own index is offset by the reserved prefix's total size (4, the
; bound range's own size) so the two addressing schemes cannot alias (see
; "Bound-resource normalization"'s step 4 in feme/docs/FeMeCPUDesign.md).

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @main(
define void @main(i32 %idx, i32 %dyn) {
  ; The bound handle's range check compares against its own declared range
  ; size (4), and its clamped index is based at 0.
  ; CHECK: icmp uge i32 %idx, 4
  ; CHECK: add i64 0, %{{.*}}
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 4, i32 %idx, ptr null)

  ; The native dynamic access has no range check of its own -- only the
  ; overflow-clamped addition of the reserved prefix size (4).
  ; CHECK: [[EXT:%.*]] = zext i32 %dyn to i64
  ; CHECK: add i64 4, [[EXT]]
  %h2 = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 %dyn, i1 false)
  ret void
}

; CHECK-NOT: @llvm.dx.resource.handlefrombinding
; CHECK: !feme.cpu.bound_resources = !{![[MD:[0-9]+]]}
; CHECK: ![[MD]] = !{!"main", i32 4, i32 0, i32 0, i32 0, i32 0, i32 4, i32 0, i32 0}

declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32, i32, i32, i32, ptr)
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32, i1)
