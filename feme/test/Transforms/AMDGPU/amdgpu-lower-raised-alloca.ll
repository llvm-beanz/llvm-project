; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-raised -S %s | FileCheck %s

; feme::amdgpu::RaisedLoweringPass moves a raised module's `alloca`s into
; AMDGPU's private address space (5), the only one its frame-index selection
; covers, per the "Raised LLVM IR -> AMDGPU" section of feme/docs/Design.md.
; Unlike amdgpu-lower-raised.ll's checks, this module deliberately keeps its
; original (non-`amdgcn-*`) target triple: feme::Driver runs this pass before
; retargeting the module's own `llvm.target_triple` to the requested AMDGPU
; one (see feme::Driver::run), so a generic-address-space `alloca` -- invalid
; input for an `amdgcn-*`-triple module, per LLVM's IR verifier -- is exactly
; what this pass actually sees in practice.

; A generic-address-space `alloca` -- e.g. from a `const static` HLSL array a
; SPIR-V/DXIL input keeps as a per-invocation local rather than folding into
; a single constant -- moves to AMDGPU's private address space; a
; `getelementptr` dynamically indexing into it is rebuilt to match, and the
; `load`/`store` through it are simply repointed.
; CHECK-LABEL: define <3 x float> @read_local_array(
define <3 x float> @read_local_array(i32 %idx) {
  ; CHECK: [[ALLOCA:%.*]] = alloca [2 x <3 x float>], align 16, addrspace(5)
  ; CHECK: store <6 x float> {{.*}}, ptr addrspace(5) [[ALLOCA]]
  ; CHECK: [[GEP:%.*]] = getelementptr [2 x <3 x float>], ptr addrspace(5) [[ALLOCA]], i32 0, i32 %idx
  ; CHECK: load <3 x float>, ptr addrspace(5) [[GEP]]
  %1 = alloca [2 x <3 x float>], align 16
  store <6 x float> <float 0.0, float 0.0, float 0.0, float 1.0, float 0.5, float 0.25>, ptr %1
  %2 = getelementptr [2 x <3 x float>], ptr %1, i32 0, i32 %idx
  %3 = load <3 x float>, ptr %2
  ret <3 x float> %3
}

; An `alloca` already in AMDGPU's private address space is left as-is.
; CHECK-LABEL: define float @read_already_private(
define float @read_already_private() {
  ; CHECK: alloca float, align 4, addrspace(5)
  %1 = alloca float, align 4, addrspace(5)
  store float 1.0, ptr addrspace(5) %1
  %2 = load float, ptr addrspace(5) %1
  ret float %2
}

; An `alloca` whose address escapes through anything other than a
; `getelementptr`/`load`/`store` (here, returned directly) is left
; unmodified rather than partially rewritten: this pass only knows the
; shapes FeMe's own conversions produce.
; CHECK-LABEL: define ptr @escaping_alloca(
define ptr @escaping_alloca() {
  ; CHECK: alloca i32, align 4{{$}}
  %1 = alloca i32, align 4
  ret ptr %1
}
