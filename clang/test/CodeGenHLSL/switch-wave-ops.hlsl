// REQUIRES: spirv-registered-target
// RUN: %clang --target=spirv1.6-unknown-vulkan1.3-compute -Xclang -finclude-default-header -DTEST=0 -O0 -c %s -o %t.0.od.spv
// RUN: %clang --target=spirv1.6-unknown-vulkan1.3-compute -Xclang -finclude-default-header -DTEST=0 -O3 -c %s -o %t.0.opt.spv
// RUN: %clang --target=spirv1.6-unknown-vulkan1.3-compute -Xclang -finclude-default-header -DTEST=1 -O0 -c %s -o %t.1.od.spv
// RUN: %clang --target=spirv1.6-unknown-vulkan1.3-compute -Xclang -finclude-default-header -DTEST=1 -O3 -c %s -o %t.1.opt.spv
// RUN: %clang --target=spirv1.6-unknown-vulkan1.3-compute -Xclang -finclude-default-header -DTEST=2 -O0 -c %s -o %t.2.od.spv
// RUN: %clang --target=spirv1.6-unknown-vulkan1.3-compute -Xclang -finclude-default-header -DTEST=2 -O3 -c %s -o %t.2.opt.spv
// RUN: %if spirv-tools %{ spirv-val %t.0.od.spv && spirv-val %t.0.opt.spv %}
// RUN: %if spirv-tools %{ spirv-val %t.1.od.spv && spirv-val %t.1.opt.spv %}
// RUN: %if spirv-tools %{ spirv-val %t.2.od.spv && spirv-val %t.2.opt.spv %}

// Exercise resource handles at -O0, exit dispatch selections, and merge/branch
// adjacency after machine CSE at -O3.
Buffer<uint> In : register(t0);
RWBuffer<uint> Out : register(u1);

uint checkMask(uint id, uint expected) {
  return WaveActiveBitOr(1u << id) == expected;
}

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_GroupThreadID) {
  uint id = tid.x;
  uint passed = 0;
#if TEST == 0
  switch (In[id]) {
  case 0:
    passed |= checkMask(id, 0x5);
    if (id == 2)
      break;
    passed |= checkMask(id, 0x1) << 1;
  case 1:
    passed |= checkMask(id, 0x3) << 2;
    if (id & 1)
      passed |= checkMask(id, 0x2) << 3;
    else
      passed |= checkMask(id, 0x1) << 3;
    passed |= checkMask(id, 0x3) << 4;
    if (id == 1)
      break;
  case 2:
    passed |= checkMask(id, 0x9) << 5;
    break;
  }
  passed |= checkMask(id, 0xf) << 6;
#elif TEST == 1
  [loop]
  for (uint i = 0; i < 2; ++i) {
    uint shift = 6 * i;
    passed |= checkMask(id, i == 0 ? 0xf : 0xb) << shift;
    switch (In[id]) {
    case 0:
      passed |= checkMask(id, 0x1) << (shift + 1);
      if (i == 0)
        continue;
    case 1:
      passed |= checkMask(id, i == 0 ? 0x2 : 0x3) << (shift + 2);
      if (id == 1 && i == 1)
        break;
    case 2:
      passed |= checkMask(id, i == 0 ? 0x6 : 0x1) << (shift + 3);
      break;
    default:
      passed |= checkMask(id, 0x8) << (shift + 4);
      continue;
    }
    passed |= checkMask(id, i == 0 ? 0x6 : 0x3) << (shift + 5);
    if (id == 2)
      break;
  }
  passed |= checkMask(id, 0xf) << 12;
#else
  switch (In[id]) {
  case 0:
    passed |= checkMask(id, 0x7);
    [loop]
    for (uint i = 0; i < 3; ++i) {
      if (i == id)
        break;
      passed |= checkMask(id, i == 0 ? 0x6 : 0x4) << (1 + i);
      if (id == 1)
        continue;
      passed |= checkMask(id, 0x4) << (3 + i);
    }
    passed |= checkMask(id, 0x7) << 5;
  case 1:
    passed |= checkMask(id, 0xf) << 6;
    break;
  }
  passed |= checkMask(id, 0xf) << 7;
#endif
  Out[id] = passed;
}
