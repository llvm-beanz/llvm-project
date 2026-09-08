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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you close out L70 from the roadmap or other prerequisites blocking the
L-series milestones?

> **All compute-stage image sampling fails outright at
> `vkCreateComputePipelines`, entirely independent of screen-space derivatives**
> -- discovered by roadmap L69's own real CTS re-run, which found its 295-case
> caselist's 153 `Fail` cases were unaffected in aggregate by either of L69's
> own two sub-bug fixes; confirmed unrelated to derivatives specifically because
> a trivial `dEQP-VK.glsl.texture_functions.texturelod.sampler2d_float_compute`
> case (explicit-LOD, needs no derivative-group mode at all) fails identically,
> with `FEME_VULKAN_LOG_CREATION_ERRORS=1` reporting the same generic
> `UnsupportedOps.cpp` diagnostic every other compute-stage sampling failure
> already shows: `"...is a register-bound resource handle the FeMe CPU target
> cannot normalize into a heap access..."`. A real SPIR-V disassembly of this
> exact failing case (`--deqp-log-decompiled-spirv=enable`) shows the flagged
> handle (`target("spirv.Image", f32, 1, 0, 0, 0, 2, 4)`, i.e. a plain 2D,
> `Rgba8`-format storage image at binding 4) is the compute shader's own output
> image -- unlike a fragment shader, a compute shader has no color-attachment
> framebuffer to write its result into, so this CTS group always binds a plain
> storage image for that purpose instead -- but the diagnostic's own caveat
> ("this handle may be an unrelated bystander") is confirmed live here too:
> `classifyStorageImage2DHandle` in `SPIRVResourceLowering.cpp` already accepts
> this exact shape (`Dim2D`, non-arrayed, non-multisampled,
> `SPIRVSampledWithoutSampler`, float channel type) with no image-format-based
> rejection at all, so the flagged handle is very unlikely to be the real
> failing operation -- some *other* resource use in the same entry function
> (most plausibly the function's own *input* sampled image, at binding 0, or an
> as-yet-unidentified operation specific to how a compute-stage entry point's
> resources get imported/lowered) is the true cause, per this project's own
> established "an unsupported use of any other resource in the same function
> prevents every handle in that function from being normalized" precedent. Not
> yet root-caused; needs its own real IR reduction of this exact failing case
> (mirroring this project's own H6-series/H8-series/H9-series/L-series reduction
> precedent) to isolate the true failing operation before any fix can be scoped,
> since grepping
> `SPIRVResourceLowering.cpp`/`BoundResourceNormalization.cpp`/`ResourceInfo.cpp`
> for stage-based branching found none, so the bug is not a simple compute-stage
> exclusion anywhere in the resource-lowering passes themselves -- it must be
> either in how the SPIR-V-to-LLVM import path threads a compute entry point's
> resource/binding metadata differently than a graphics one, or in an
> image-sampling-shaped operation this pass's existing pattern set does not yet
> recognize for a compute-stage caller specifically. This is a large,
> cross-cutting, and currently the sole blocker of any CTS-visible payoff for
> every compute-stage sampling fix this project's own history has already landed
> (L69 included) -- likely needs breaking down further once root-caused, per
> this project's own established splitting precedent, rather than attempted in
> one pass.
