; RUN: feme-opt --llvm -passes=feme-nvptx-lower-resources -S %s | FileCheck %s

; feme::nvptx::ResourceLoweringPass turns the resource bindings a raised
; shader refers to by (register space, register) into kernel pointer
; arguments, since NVPTX kernels (CUDA's) receive everything they operate on as
; arguments rather than through a descriptor table, exactly like AMDGPU's
; counterpart (feme::amdgpu::ResourceLoweringPass, see its own test
; amdgpu-lower-resources.ll, which this mirrors). See Roadmap.md's
; "Retargeting" section for NVPTX's own remaining work.
;
; Rewriting an entry point grows its signature, so the rewritten function is a
; new one appended to the module; the CHECK order below follows that, not the
; source order.

target triple = "nvptx64-nvidia-cuda"

; A binding array indexed at runtime would need one pointer per register, which
; a fixed argument list cannot express, so this entry point is left alone
; rather than partially rewritten.
; CHECK: define void @dynamic_binding_index(i32 %idx) [[ATTRS:#[0-9]+]]
; CHECK: call target("dx.TypedBuffer", <4 x float>, 1, 0, 0) @llvm.dx.resource.handlefrombinding
define void @dynamic_binding_index(i32 %idx) #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32 0, i32 0, i32 4, i32 %idx, ptr null)
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx, <4 x float> zeroinitializer)
  ret void
}

; Bindings become trailing arguments in (space, register) order, regardless of
; the order the shader happens to create handles in.
; CHECK: define void @entry(i32 %idx, ptr addrspace(1) %res.space0.reg0, ptr addrspace(1) %res.space1.reg3) [[ATTRS]]
; CHECK-NEXT: [[SRC:%.*]] = getelementptr <4 x float>, ptr addrspace(1) %res.space1.reg3, i32 %idx
; CHECK-NEXT: [[VAL:%.*]] = load <4 x float>, ptr addrspace(1) [[SRC]], align 16
; CHECK-NEXT: [[DST:%.*]] = getelementptr <4 x float>, ptr addrspace(1) %res.space0.reg0, i32 %idx
; CHECK-NEXT: store <4 x float> [[VAL]], ptr addrspace(1) [[DST]], align 16
; CHECK-NEXT: ret void
define void @entry(i32 %idx) #0 {
  %in = call target("dx.TypedBuffer", <4 x float>, 0, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_0_0_0t(i32 1, i32 3, i32 1, i32 0, ptr null)
  %out = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %loaded = call {<4 x float>, i1}
      @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_0_0_0t(target("dx.TypedBuffer", <4 x float>, 0, 0, 0) %in, i32 %idx)
  %value = extractvalue {<4 x float>, i1} %loaded, 0
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %out, i32 %idx, <4 x float> %value)
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="64,1,1" }

declare target("dx.TypedBuffer", <4 x float>, 0, 0, 0) @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_0_0_0t(i32, i32, i32, i32, ptr)
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0) @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32, i32, i32, i32, ptr)
declare {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_0_0_0t(target("dx.TypedBuffer", <4 x float>, 0, 0, 0), i32)
declare void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32, <4 x float>)
