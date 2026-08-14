// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for `WaveActiveBallot`: real HLSL, compiled to a DXIL
// DXContainer by Clang, imported/raised/JIT-dispatched by `feme-run` (see
// loop.hlsl's own comment for the pipeline this exercises in full). This is
// `ballot.hlsl` from roadmap step R3 (feme/docs/Roadmap.md's §2.3):
// "WaveActiveBallot + countbits; gated on §1.3's aggregate-returning
// mechanism" -- the mechanism `feme::dxil::OpRaisingPass::raiseAggregateCall`
// implements, and `feme::cpu::WaveCallKind::Ballot`/`lowerBallot`
// (WaveLowering.cpp) lower for the CPU target. Every even lane's `tid.x`
// satisfies the ballotted condition, so lanes 0 and 2 of this 4-wide wave
// set bits 0 and 2 of the ballot's first 32-bit word (`0b0101 == 5`);
// `countbits` of that word is 2, the same uniform answer every lane writes
// to its own `Out` slot.

// CHECK: binding[0:0][0]: 2 2 2 2

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint4 ballot = WaveActiveBallot(tid.x % 2 == 0);
  Out[tid.x] = countbits(ballot.x);
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
