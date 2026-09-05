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

Can you work on L31 or other prerequisites blocking the L-series milestones?

> **L25's own two cases
> (`Feature/Textures/{SampleCmp,CalculateLevelOfDetail}.test`) now clear MLIR
> SPIR-V import but still fail `vkCreateGraphicsPipelines`, on the expected
> next-stage legalization gap**: `"failed to legalize operation
> 'spirv.ImageSampleDrefImplicitLod'/'spirv.ImageSampleDrefExplicitLod'/'spirv.ImageQueryLod'
> that was explicitly marked illegal"` -- `feme`'s own `SPIRVToLLVMPatterns.cpp`
> has no conversion pattern for any of the three ops L25 just taught MLIR's
> SPIR-V dialect to deserialize, confirmed both by a minimal `feme-opt
> --feme-convert-spirv-to-llvm` repro and at real CTS scale (every one of
> `dEQP-VK.glsl.texture_functions.{query.texturequerylod.*,texture.*shadow*}`'s
> 190 + 32 cases fails identically once run against the real `feme` ICD, not
> just this row's 2 named cases). LLVM's own SPIRV backend intrinsics to target
> are already known from
> `llvm/test/CodeGen/SPIRV/hlsl-resources/{SampleCmp,SampleCmpLevelZero,CalculateLevelOfDetail}.ll`:
> `llvm.spv.resource.samplecmp`/`.samplecmp.clamp` (implicit-LOD dref, mirroring
> `ImageSampleImplicitLodPattern`'s own `None`/`ConstOffset`/`MinLod`
> operand-combination handling), `llvm.spv.resource.samplecmplevelzero`
> (explicit-LOD dref, but only for a literal `Lod = 0.0` constant -- the
> intrinsic itself has no LOD operand at all, unlike
> `ImageSampleExplicitLodPattern`'s `samplelevel`, so a nonzero explicit LOD
> dref sample has no known mapping and should `notifyMatchFailure`), and
> `llvm.spv.resource.calculate_lod`/`.calculate_lod_unclamped`
> (`ImageQueryLod`'s clamped/unclamped mip level, needing **two** intrinsic
> calls combined into one `vector<2xf32>` result via two `llvm.insertelement`s,
> the reverse direction of the `.ll` file's own
> single-`OpImageQueryLod`-from-two-calls-lowering, since `feme` converts SPIR-V
> *into* LLVM IR rather than the other way around). Once these three
> legalization patterns land, they will very likely expose a *further* blocker:
> `feme/lib/Transforms/CPU/SPIRVResourceLowering.cpp` (the pass that actually
> implements CPU-side resource-intrinsic semantics) only recognizes
> `spv_resource_sample`/`spv_resource_samplelevel` today, not any of
> `spv_resource_samplecmp{,_clamp}`/`spv_resource_samplecmplevelzero`/`spv_resource_calculate_lod{,_unclamped}`
> -- so real depth-comparison sampling and LOD-query CPU emulation semantics
> (not just legalization plumbing) remain a substantial follow-on scope of their
> own, likely needing its own further breakdown once the legalization patterns
> above are in place and this next blocker is confirmed for real
