// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run -O0 --wave-size=4 --groups=2,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s
// RUN: feme-run -O1 --wave-size=4 --groups=2,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s
// RUN: feme-run -O2 --wave-size=4 --groups=2,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s
// RUN: feme-run -O3 --wave-size=4 --groups=2,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// Roadmap step R14 (see feme/docs/Roadmap.md's §2.2.5 "Optimization
// level"): every other end-to-end HLSL test in this directory JITs its
// shader through `feme::cpu::JITEngine`'s `-O2`-equivalent default (see
// `feme-run`'s own file comment on `-O`, and `JITEngine.cpp`'s
// `toOptimizationLevel`), but nothing ever asked it to run at a
// *different* level and checked the answer stayed the same, so an
// optimization bug that only shows up at, say, `-O0` (nothing folded or
// reordered) or `-O3` (the most aggressive vectorization/reassociation
// `feme::OptimizerPipeline` will attempt) had no differential to catch it
// against. This shader has real reassociation/vectorization opportunities
// for the optimizer to exploit -- a small unrolled loop of independent
// per-lane multiply-adds, each of the four loop bodies over a divergent
// (`tid.x`-derived) base value -- unlike thread-id-store.ll's single
// `mul`, so the four `-O<N>` runs below are not simply re-running
// identical IR through the optimizer, they are genuinely exercising its
// reordering/folding passes differently at each level while still
// dispatching against the same widened (`--wave-size=4`), two-group JIT
// path every other execution test uses.

// CHECK: binding[0:0][0]: 10 20 30 40 50 60 70 80

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint sum = 0;
  for (uint i = 0; i < 4; i++) {
    sum += (tid.x + 1) * (i + 1);
  }
  Out[tid.x] = sum;
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 32
