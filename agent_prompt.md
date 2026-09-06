---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled. Also build and test the `check-feme` target
ensuring that all the target dependencies are correctly setup so that the test
dependencies will build before running the tests.

When you deviate from the design document please update the design document.

Also please run the Vulkan CTS from the checkout under /home/dev/dev/VK-GL-CTS/
after each change and update the VulkanCTSReport.md. Please keep the
Vulkan14FeatureInventory and VulkanExtensionInventory up to date with each
change as well.

If the request is to complete a roadmap stage, if you complete it please strike
it through on the roadmap document, if you do not, please add entries to the
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L52 or other prerequisites blocking the L-series milestones?

> **L50's own (a)-(f) breakdown leaves 4 sub-items open after this session's (d)
> fix and L51's own further split-out of (f)**: (a) **`Plain1D`/`Array1D` shadow
> sampling** (`sampler1d{,array}shadow_*`) has no ordinary (non-comparison)
> sampled-image path on the CPU target at all yet for either shape -- needs its
> own new 1D-sampling infrastructure
> (`ImageCallKind::Sample1D`/`Array1D`-equivalents,
> `createSample1D`/`createSample1DArray`, matching runtime entry points)
> *before* a `SampleCmp1D`/`SampleCmpArray1D` counterpart is even possible, a
> materially bigger prerequisite than any other item in this row; (b) **a `Bias`
> image operand** (`sampler{2d,cube}shadow_bias_fragment` and siblings, 5 real
> confirmed-failing CTS cases per this session's own re-run) fails at
> `ConvertSPIRVToLLVMPass` legalization itself, before
> `SPIRVResourceLowering.cpp` ever sees it --
> `ImageSampleDrefImplicitLodPattern`'s own `SupportedMask`
> (`SPIRVToLLVMPatterns.cpp`) only allows `ConstOffset`/`MinLod`, not `Bias`,
> and no `llvm.spv.resource.samplecmp*` intrinsic form threads an explicit bias
> through today, unlike the ordinary
> `spv_resource_samplebias`/`.samplebias_clamp` family already handled for a
> non-dref sample -- needs its own new intrinsic-lowering design (a genuinely
> bigger, cross-cutting scope touching real LLVM SPIR-V backend intrinsic
> definitions, not just feme-internal code, per this session's own investigation
> into why (b) was deferred in favor of (d)); (c) **`samplecmp_clamp`'s own
> trailing `MinLod` clamp operand** (not yet confirmed present in any real
> failing CTS case measured so far) needs each `createSampleCmp*` builder
> extended with a `MinLodClamp` parameter, mirroring `createSample2D`'s own
> roadmap L26 precedent; (e) **the entirely separate LOD-query intrinsics**
> (`spv_resource_calculate_lod`/`.calculate_lod_unclamped`, the 190-case
> `texturequerylod` group, confirmed unaffected at 0/190 by every L48/L50 re-run
> so far) have no CPU-lowering consumer at all on either the SPIR-V or DXIL
> frontend and need a genuinely new runtime design: a real screen-space
> coordinate derivative (`dFdx`/`dFdy`) threaded through to a query call,
> needing its own design pass to confirm where a per-invocation derivative is
> (or could be made) available in this target's per-lane execution model.
> (`Cube`/`CubeArray`'s own real, nonzero `ConstOffset` sub-item (d) named in
> L50's own original report does not exist -- SPIR-V forbids `ConstOffset`
> against `Dim::Cube` outright, confirmed again by this session's own
> `isSupportedOffset` review, so there is no remaining `(d)`-shaped gap to file
> here; `(f)`'s `CubeArray`-shadow rendering bug is already its own row,
> **L51**, not re-listed here.) Each of (a), (b), (c), (e) should be scoped and
> fixed as its own small, independently-committed, independently-CTS-measured
> row rather than attempted together, per this project's own established
> L26->L33/L45->L47/L46->L48/L48->L50 precedent
