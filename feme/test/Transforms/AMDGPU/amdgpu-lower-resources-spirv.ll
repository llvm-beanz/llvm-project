; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-resources -S %s | FileCheck %s

; feme::amdgpu::ResourceLoweringPass also matches the SPIR-V-flavored raised
; resource ops feme::SPIRVToLLVMTranslator produces (see
; test/Conversion/SPIRVToLLVM/spirv-to-llvm-image-access.mlir): a
; `target("spirv.Image", ...)` handle whose accesses go through
; `llvm.spv.resource.getpointer` rather than DX's dedicated load/store
; intrinsics -- an ordinary `load`/`store` then reads or writes through the
; pointer it returns. See amdgpu-lower-resources.ll for the equivalent
; `llvm.dx.*` coverage, and the "Raised LLVM IR -> AMDGPU" section of
; feme/docs/Design.md.
;
; This also exercises that this pass reads the loaded/stored element type off
; the access itself rather than off the handle type, since (unlike DX's
; `target("dx.TypedBuffer", ElemTy, ...)`) SPIR-V's handle type does not spell
; it directly.
;
; Rewriting an entry point grows its signature, so the rewritten function is a
; new one appended to the module; the CHECK order below follows that, not the
; source order.

target triple = "amdgcn-amd-amdhsa"

; A binding array indexed at runtime would need one pointer per register,
; which a fixed argument list cannot express, so this entry point is left
; alone rather than partially rewritten.
; CHECK: define void @dynamic_binding_index(i32 %idx) [[ATTRS:#[0-9]+]]
; CHECK: call target("spirv.Image", float, 5, 0, 0, 0, 2, 1) @llvm.spv.resource.handlefrombinding
define void @dynamic_binding_index(i32 %idx) #0 {
  %h = call target("spirv.Image", float, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 4, i32 %idx, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer(
      target("spirv.Image", float, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  store <4 x float> zeroinitializer, ptr %ptr
  ret void
}

; A `getpointer` call used more than once addresses more than a single
; element access, which this pass does not attempt to reason about, so this
; entry point is left alone too.
; CHECK: define void @getpointer_reused(i32 %idx) [[ATTRS]]
; CHECK: call ptr @llvm.spv.resource.getpointer
define void @getpointer_reused(i32 %idx) #0 {
  %h = call target("spirv.Image", float, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer(
      target("spirv.Image", float, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  %a = load <4 x float>, ptr %ptr
  %b = load <4 x float>, ptr %ptr
  %sum = fadd <4 x float> %a, %b
  store <4 x float> %sum, ptr %ptr
  ret void
}

; Bindings become trailing arguments in (space, register) order, regardless of
; the order the shader happens to create handles in.
; CHECK: define void @entry(i32 %idx, ptr addrspace(1) %res.space0.reg0, ptr addrspace(1) %res.space1.reg3) [[ATTRS:#[0-9]+]]
; CHECK-NEXT: [[SRC:%.*]] = getelementptr <4 x float>, ptr addrspace(1) %res.space1.reg3, i32 %idx
; CHECK-NEXT: [[VAL:%.*]] = load <4 x float>, ptr addrspace(1) [[SRC]], align 16
; CHECK-NEXT: [[DST:%.*]] = getelementptr <4 x float>, ptr addrspace(1) %res.space0.reg0, i32 %idx
; CHECK-NEXT: store <4 x float> [[VAL]], ptr addrspace(1) [[DST]], align 16
; CHECK-NEXT: ret void
define void @entry(i32 %idx) #0 {
  %in = call target("spirv.Image", float, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 1, i32 3, i32 1, i32 0, ptr null)
  %out = call target("spirv.Image", float, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %in.ptr = call ptr @llvm.spv.resource.getpointer(
      target("spirv.Image", float, 5, 0, 0, 0, 2, 1) %in, i32 %idx)
  %value = load <4 x float>, ptr %in.ptr
  %out.ptr = call ptr @llvm.spv.resource.getpointer(
      target("spirv.Image", float, 5, 0, 0, 0, 2, 1) %out, i32 %idx)
  store <4 x float> %value, ptr %out.ptr
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="64,1,1" }

declare target("spirv.Image", float, 5, 0, 0, 0, 2, 1) @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 1), i32)
