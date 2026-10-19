// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/raw.hlsl -o %t/raw.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/raw-heap.yaml %t/raw.dxcontainer | FileCheck %s --check-prefix=RAW
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/typed.hlsl -o %t/typed.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/typed-heap.yaml %t/typed.dxcontainer | FileCheck %s --check-prefix=TYPED

// End-to-end coverage for a *resource* (not groupshared) buffer's own
// `InterlockedAdd`: real HLSL, compiled to a DXIL DXContainer by Clang,
// imported/raised/JIT-dispatched by `feme-run` (see loop.hlsl's own comment
// for the pipeline this exercises in full). Unlike histogram.hlsl's
// groupshared `Counter` -- whose `atomicrmw addrspace(3)` was already
// idiomatic LLVM IR straight out of Clang -- a resource atomic instead
// round-trips through DXIL's own `dx.op.atomicBinOp` (opcode 78) once a
// real DXIL backend compiles it into a DXContainer, which
// `feme::dxil::OpRaisingPass::raiseAtomicBinOp` (OpRaising.cpp) must raise
// back into the same `llvm.dx.resource.getpointer`+`atomicrmw` pair Clang's
// own frontend emits before that backend conversion ever happens, and
// `feme::cpu::ResourceLoweringPass::lowerAtomicAccess` (ResourceLowering.cpp)
// must then lower into the pre-existing, previously-DXIL-unreachable
// `feme.cpu.resource.atomic.*.{raw,typed}.i32` runtime calls (roadmap
// H8w/H8x) -- this is their first real-DXIL-derived end-to-end coverage
// (both were already exercised through SPIR-V's own, differently-shaped
// atomic path).
//
// Lane 0 and 2 (the even `tid.x` values) each increment the shared
// `Counter`, capturing the running count *before* their own increment as
// `slot`; lane 1 and 3 keep the sentinel `999`. `Out[0]` ends at `2` (two
// increments); `Out[1]`'s four slots read `0 999 1 999`, in `tid.x` order
// (dispatch is sequential per lane, so `Counter`'s increments happen in
// that order regardless of wave size -- see histogram.hlsl's identical
// reasoning).

// RAW: binding[0:0][0]: 2
// RAW: binding[0:1][0]: 0 999 1 999
// TYPED: binding[0:0][0]: 2
// TYPED: binding[0:1][0]: 0 999 1 999

//--- raw.hlsl
RWStructuredBuffer<uint> Counter : register(u0);
RWStructuredBuffer<uint> Out : register(u1);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint slot = 999;
  if (tid.x % 2 == 0) {
    InterlockedAdd(Counter[0], 1, slot);
  }
  Out[tid.x] = slot;
}

//--- raw-heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 4
  - space: 0
    register: 1
    entries:
      - index: 0
        size: 16

//--- typed.hlsl
RWBuffer<uint> Counter : register(u0);
RWStructuredBuffer<uint> Out : register(u1);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint slot = 999;
  if (tid.x % 2 == 0) {
    InterlockedAdd(Counter[0], 1, slot);
  }
  Out[tid.x] = slot;
}

//--- typed-heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        kind: typed-buffer
        format: r32_uint
        size: 4
  - space: 0
    register: 1
    entries:
      - index: 0
        size: 16
