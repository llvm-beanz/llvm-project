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

Can you work on L46 or other prerequisites blocking the L-series milestones?

> **L31's own fix clears the SPIR-V-to-LLVM legalization gap for
> `spirv.ImageSampleDrefImplicitLod`/`ImageSampleDrefExplicitLod`/`ImageQueryLod`,
> but `vkCreateGraphicsPipelines` still fails on a real
> `Feature/Textures/{SampleCmp,CalculateLevelOfDetail}.test` re-run**:
> `"unsupported raised operation: 'llvm.spv.resource.handlefrombinding...' is a
> register-bound resource handle the FeMe CPU target cannot normalize..."`
> (`UnsupportedOps.cpp`'s end-of-pipeline catch-all) --
> `feme/lib/Transforms/CPU/SPIRVResourceLowering.cpp`'s own
> `isSampleIntrinsic`/`hasOnlySupportedImageUses` only recognize
> `spv_resource_sample`/`spv_resource_sample_clamp`/`spv_resource_samplelevel`
> today, so a sampled-image handle whose only uses are one of the five
> newly-legalized intrinsics
> (`spv_resource_samplecmp`/`.samplecmp_clamp`/`samplecmplevelzero`/`calculate_lod`/`calculate_lod_unclamped`)
> is rejected as "not fully supported" and left entirely unlowered, so its
> `handlefrombinding` call survives, unconsumed, all the way to this pass. This
> is a substantial, cross-cutting scope of its own (real CPU emulation
> semantics, not legalization plumbing): needs (1) extending
> `isSampleIntrinsic`/`hasOnlySupportedImageUses`/`getSampleOffsetIdx`-style
> helpers to recognize all five new intrinsics and validate their own
> coordinate/offset/clamp/dref operand shapes the same way the existing three
> are validated; (2) new `feme.cpu.image.*` runtime entry points in
> `feme/runtime/CPU/FeMeRuntimeCPU.c` alongside the existing
> `femeRTSamplePoint2D`/`femeRTComputeBilinearSupport` -- a depth-comparison
> sample (`samplecmp`/`samplecmplevelzero`) needs each of the (up to 4,
> bilinear) sampled texels compared against the reference value and blended per
> the sampler's own `ComparisonOp`, rather than just averaged, so the existing
> bilinear helpers cannot be reused as-is; a LOD query
> (`calculate_lod`/`_unclamped`) needs a real screen-space coordinate derivative
> (`dFdx`/`dFdy`) to compute a mip level from, which no existing CPU-target code
> path currently threads through to a sample/query call at all -- needs its own
> design pass to confirm where a per-invocation derivative is (or could be made)
> available in this CPU target's per-lane execution model; (3) new lit coverage
> in `feme/test/Transforms/CPU/` (mirroring the existing
> `resource-lowering-image-sample.ll`) plus new `FeMeRuntimeCPUTests` unit tests
> for each new runtime entry point; (4) a real `check-hlsl-feme-vk`/`deqp-vk`
> re-run of `Feature/Textures/{SampleCmp,CalculateLevelOfDetail}.test` and the
> `dEQP-VK.glsl.texture_functions.{query.texturequerylod.*,texture.*shadow*}`
> CTS groups (190 + 32 cases) to confirm the fix at scale, since this row's own
> scope was discovered by, but not yet measured against, that real CTS surface
