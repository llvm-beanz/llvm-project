# Draft upstream issue: VK-GL-CTS mesh-shader `in_out.32_bits_only` vec3/ivec3 std430 mismatch

**Status**: drafted, not yet filed against `KhronosGroup/VK-GL-CTS` (no push
access to that repo from this environment). Recorded here so a human can
file it, and so future FeMe sessions do not re-investigate the same 90-case
cluster as a FeMe regression. See roadmap `L149` and
`feme/docs/VulkanCTSReport.md` for the original root-cause writeup this
draft is based on.

## Affected file

`external/vulkancts/modules/vulkan/mesh_shader/vktMeshShaderInOutTestsEXT.cpp`

## Summary

`InterfaceVariablesCase::PerVertexData` and `::PerPrimitiveData` declare
`tcu::Vec3`/`tcu::IVec3` array fields (e.g. `tcu::Vec3
prim_f32d3_flat_0[kNumPrimitives]`), fill them via ordinary array-index
assignment, and upload the whole struct to a Vulkan storage buffer with one
raw `deMemcpy`. The generated GLSL declares the matching buffer block as
`std430`, which mandates a 16-byte array stride for `vec3`/`ivec3` members
(each element is padded to 16 bytes, per the GLSL spec's block-layout
rules), even though `tcu::Vec3` itself is a tightly-packed 12-byte
`float[3]` with no such padding.

Every `vec3`/`ivec3` array field in these two structs is therefore laid out
one way in the C++ struct the test uploads, and a different way in the
`std430`-decorated SPIR-V the same test hands to the shader compiler. Any
Vulkan-conformant driver -- which must honor the SPIR-V module's own
`OpMemberDecorate ... Offset` values -- reads back the wrong 16-byte window
for every such field and everything declared after it in each struct.

## Evidence (see `feme/docs/VulkanCTSReport.md`'s `L149` entry for the full narrative)

1. `sizeof`/`offsetof` of a C++ struct mirroring `PerPrimitiveData`'s real
   field list confirms `prim_f32d3_flat_0` sits at a tight offset of 224,
   with total struct size 960 bytes -- both computed straight from the
   struct's own field list, not estimated.
2. The compiled SPIR-V embedded in the test's own QPA log decorates this
   same field `Offset 240` (present identically in both the mesh and
   fragment shader modules) -- the `std430`-correct offset given the
   preceding fields, independently confirmed against the GLSL spec's
   `vec3`-array-stride rule.
3. A live dump of the real bound host buffer at descriptor-write time shows
   bytes tightly packed exactly as `sizeof()`/`offsetof` predict (no gap),
   confirming the host side truly is unpadded, not just in theory.

## Suggested fix direction (not fully validated -- see caveat below)

Give every `vec3`/`ivec3` array field in `PerVertexData`/`PerPrimitiveData`
the same 16-byte-per-element layout the GLSL `std430` block already
requires, e.g. a wrapper type:

```cpp
struct alignas(16) Std430Vec3 {
    tcu::Vec3 v;
    float pad;
    Std430Vec3() = default;
    Std430Vec3(const tcu::Vec3 &v_) : v(v_), pad(0.0f) {}
    operator tcu::Vec3() const { return v; }
};
// ... Std430IVec3 analogously ...
```

...then change only the field *declarations* (not the ~80 call sites that
assign/read them, which keep working unchanged via the implicit
constructor/conversion operator) from `tcu::Vec3 name[N]` to
`Std430Vec3 name[N]` (and `tcu::IVec3`/`Std430IVec3` likewise).

**Caveat -- this repo's own attempt at exactly this wrapper, this session,
did not fully resolve the cluster locally** (2/80 passing, down from the
baseline, with a different failing line number after the change). Since
both structs declare *many* consecutive `vec3`/`vec4`/`vec2`/scalar fields
of varying bit-width groups, forcing 16-byte alignment on the `vec3`
wrapper changes every subsequent field's offset too, and getting every one
of those cascading offsets to agree with the `std430`-decorated SPIR-V
likely needs the fix applied consistently to *all* three-and-larger-vector
fields across both structs (not just `vec3`), plus care around structs
whose members are not multiples of 16 bytes in total (e.g. a lone
`float`/`int32_t` field, or an odd-sized `vec2` block) still not ending on
a 16-byte boundary before the next `vec3` group begins. A full fix needs
someone with time to iterate on this cross-cutting layout question field
group by field group (most likely padding `vec2` and scalar groups too,
not just `vec3`/`ivec3`) and confirm the *entire* `in_out.32_bits_only`
cluster (not just one permutation) passes end to end -- out of scope for
one FeMe session to complete to a mergeable state, but the direction above
is a reasonable starting point for whoever picks this up upstream.

## Where to file

`https://github.com/KhronosGroup/VK-GL-CTS/issues`, referencing
`external/vulkancts/modules/vulkan/mesh_shader/vktMeshShaderInOutTestsEXT.cpp`
and the `dEQP-VK.mesh_shader.ext.in_out.32_bits_only.*` test group.
