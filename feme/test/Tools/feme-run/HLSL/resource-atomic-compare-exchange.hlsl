// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/raw.hlsl -o %t/raw.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/raw-heap.yaml %t/raw.dxcontainer | FileCheck %s --check-prefix=RAW
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/typed.hlsl -o %t/typed.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/typed-heap.yaml %t/typed.dxcontainer | FileCheck %s --check-prefix=TYPED

// End-to-end coverage for a *resource* (not groupshared)
// `InterlockedCompareExchange`, the `cmpxchg` counterpart to
// resource-atomic.hlsl's `InterlockedAdd` coverage: real HLSL, compiled to
// a DXIL DXContainer by Clang, imported/raised/JIT-dispatched by
// `feme-run`. `feme::dxil::OpRaisingPass::raiseAtomicCompareExchange`
// (OpRaising.cpp) raises `dx.op.atomicCompareExchange` (opcode 79) back
// into `llvm.dx.resource.getpointer`+`cmpxchg`, and
// `feme::cpu::ResourceLoweringPass::lowerAtomicAccess`
// (ResourceLowering.cpp) lowers that into the pre-existing,
// previously-DXIL-unreachable `feme.cpu.resource.atomic.compare_exchange.
// {raw,typed}.i32` runtime calls (roadmap H8w/H8x).
//
// Every lane runs `InterlockedCompareExchange(Counter[0], 0, 100, orig)`:
// only the first lane to observe `Counter == 0` actually swaps it to
// `100`, returning the pre-swap value (`0`) as `orig`; every later lane
// observes the already-swapped `100`, so the compare fails, `Counter` is
// left unchanged, and `orig` reads back the current (unswapped) `100`.
// Dispatch is sequential per lane (see histogram.hlsl's identical
// reasoning), so lane 0 always wins this race: `Counter` ends at `100`,
// and `Out`'s four slots read `0 100 100 100`.

// RAW: binding[0:0][0]: 100
// RAW: binding[0:1][0]: 0 100 100 100
// TYPED: binding[0:0][0]: 100
// TYPED: binding[0:1][0]: 0 100 100 100

//--- raw.hlsl
RWStructuredBuffer<uint> Counter : register(u0);
RWStructuredBuffer<uint> Out : register(u1);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint orig;
  InterlockedCompareExchange(Counter[0], 0, 100, orig);
  Out[tid.x] = orig;
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
  uint orig;
  InterlockedCompareExchange(Counter[0], 0, 100, orig);
  Out[tid.x] = orig;
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
