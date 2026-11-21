// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#if LEGACY_ROOT
#define ROOT_DESCRIPTOR_FLAGS ""
#define RANGE_FLAGS ""
#else
#define ROOT_DESCRIPTOR_FLAGS ", flags=DATA_STATIC"
#define RANGE_FLAGS ", flags=DATA_STATIC_WHILE_SET_AT_EXECUTE"
#endif

#define ROOT \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
  "RootConstants(num32BitConstants=4, b3, space=7)," \
  "CBV(b4, space=7" ROOT_DESCRIPTOR_FLAGS ")," \
  "SRV(t3, space=7" ROOT_DESCRIPTOR_FLAGS ")," \
  "UAV(u3, space=7" ROOT_DESCRIPTOR_FLAGS ")," \
  "DescriptorTable(CBV(b5, numDescriptors=2, space=7, offset=4" RANGE_FLAGS ")," \
  "SRV(t9, space=7" RANGE_FLAGS "))," \
  "DescriptorTable(Sampler(s5, space=7))," \
  "StaticSampler(s3, space=7, filter=FILTER_MIN_MAG_MIP_POINT," \
  "addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_MIRROR," \
  "addressW=TEXTURE_ADDRESS_CLAMP, mipLODBias=1.0, minLOD=2.0, maxLOD=4.0)"

RWByteAddressBuffer Output : register(u3, space7);

[RootSignature(ROOT)]
[numthreads(1, 1, 1)]
void main() {
  Output.Store(0, 42);
}
