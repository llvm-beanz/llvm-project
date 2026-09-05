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

The last session got stuck.

Can you work on L48 or other prerequisites blocking the L-series milestones?

> **L46's own Plain2D/zero-offset/no-clamp depth-comparison sample slice
> measurably fixed 2/32 real `dEQP-VK.glsl.texture_functions.texture.*shadow*`
> cases, but the remaining 13 real failures (plus the entirely-separate 190-case
> `texturequerylod` group) are still open, split across several distinct,
> independently-sized gaps** this row exists to break down rather than
> re-attempt in one pass: (a) **non-`Plain2D` depth-comparison shapes**
> (`Array2D`/`Cube`/`CubeArray` --
> `sampler1d{,array}shadow`/`sampler2darrayshadow`/`samplercube{,array}shadow`,
> 8 of the 13 real failures) need a new
> `ImageCallKind::SampleCmpArray2D`/`SampleCmpCube`/`SampleCmpCubeArray` plus
> matching `createSampleCmp*`/`femeCpuImageSampleCmp*F32` runtime entry points,
> mirroring `createSample2DArray`/`createSampleCube`/`createSampleCubeArray`'s
> own non-`Plain2D` filtered-sample precedent; (b) **a `Bias` image operand**
> (`sampler2dshadow_bias_fragment` and siblings, 1 of the 13) fails even
> earlier, at `ConvertSPIRVToLLVMPass` legalization itself --
> `ImageSampleDrefImplicitLodPattern`'s own `SupportedMask`
> (`SPIRVToLLVMPatterns.cpp`) only allows `ConstOffset`/`MinLod`, not `Bias`, so
> needs its own new intrinsic-lowering design (no `llvm.spv.resource.samplecmp*`
> intrinsic form threads an explicit bias through at all today, unlike the
> ordinary `spv_resource_samplebias`/`.samplebias_clamp` family already handled
> for a non-dref sample) before `SPIRVResourceLowering.cpp` could even see it;
> (c) **`samplecmp_clamp`'s own trailing `MinLod` clamp operand** (not yet
> confirmed present in any real failing CTS case this session measured, but
> named in L46's own original report) needs
> `createSampleCmp2D`/`femeCpuImageSampleCmp2DF32` extended with a `MinLodClamp`
> parameter, mirroring `createSample2D`'s own roadmap L26 precedent; (d) **a
> real, nonzero depth-comparison `ConstOffset`** (also not yet confirmed present
> in a real failing case, named in L46's own original report) needs
> `createSampleCmp2D` extended with `OffsetX`/`OffsetY` parameters, mirroring
> the same L26 precedent on the ordinary-sample side; (e) **the entirely
> separate LOD-query intrinsics**
> (`spv_resource_calculate_lod`/`.calculate_lod_unclamped`, the 190-case
> `texturequerylod` group, confirmed via this session's own unaffected 0/190
> re-run) have no CPU-lowering consumer at all on either the SPIR-V or DXIL
> frontend (confirmed by L46's own investigation into
> `DXSAToLLVMIRTranslator.cpp`'s `CalculateLOD` path) and need a genuinely new
> runtime design: a real screen-space coordinate derivative (`dFdx`/`dFdy`)
> threaded through to a query call, which no existing CPU-target code path does
> today, needing its own design pass to confirm where a per-invocation
> derivative is (or could be made) available in this target's per-lane execution
> model before any lowering pattern can be written. Each of (a)-(e) should be
> scoped and fixed as its own small, independently-committed,
> independently-CTS-measured row rather than attempted together, per this
> project's own established precedent (L26->L33, L45->L47)
