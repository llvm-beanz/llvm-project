---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you continue working on H7f or other milestones that block completing H7f?

> **`sampleRateShading`/`alphaToOne`**: the executor always invokes the fragment
> shader once per covered fragment/pixel, never once per covered sample, so
> `VkPipelineMultisampleStateCreateInfo::sampleShadingEnable`/`minSampleShading`
> have no per-sample invocation loop to honor yet; `alphaToOneEnable` has no
> handling at all in the blend path (distinct from the already-implemented
> alpha-to-coverage handling). Grouped since both are
> `VkPipelineMultisampleStateCreateInfo` fields reached by the same
> pipeline-state translation code. **Update: `alphaToOne` done,
> `sampleRateShading` blocked on a new gap.** `alphaToOneEnable` translation
> (`GraphicsPipeline.cpp`) and executor handling (`Executor.cpp`'s `processTile`
> forcing every color attachment's alpha to `1.0`) are both real and confirmed
> conformant against a real
> `dEQP-VK.pipeline.monolithic.multisample.alpha_to_one.*` re-run (4/4
> feme-supported-sample-count cases passing) -- `alphaToOne` flips to `VK_TRUE`.
> `sampleRateShading`'s own translation and executor plumbing (a per-sample
> shade/dispatch/merge loop, narrowing each pass's invocation copy to one
> sample) are likewise implemented and unit-tested, but real
> `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading*`/`multisample_shader_builtin.sample_id.*`
> re-run (once the feature bit was flipped to let `checkSupport` allow these
> cases to attempt real pipeline creation for the first time) fails every case
> at shader-compilation time with `feme-cpu-simdize: ... has a divergent value
> '' of vector type ...`; the identical shader shape's `gl_SampleID`-derived
> storage-buffer index also fails with sample shading *disabled*, confirming the
> gap is `SIMDize.cpp`'s own pre-existing, generic lack of support for a
> divergent (per-invocation-computed) buffer store address, not anything
> sample-shading-specific -- so `sampleRateShading`'s feature bit stays
> `VK_FALSE` (advertising it before a real conformance case exercising it can
> pass would be a conformance violation), tracked as a new follow-on, H7o.
> `alphaToCoverageEnable` remains rejected and untracked-with-a-feature-bit,
> tracked as H7n. `ninja check-feme` (assertions-enabled, ccache build) passes
> in full, 2020/2079 (59 pre-existing, unrelated `Unsupported`, 0 `Failed`), up
> from H7e's own 2016/2075 baseline by exactly the 4 new tests this row adds
> (`ExecutorTest`'s
> `SampleShadingEnableInvokesFragmentOncePerSample`/`AlphaToOneEnableForcesOutputAlphaToOne`,
> `GraphicsPipelineTest`'s
> `TranslatesSampleShadingAndAlphaToOneState`/`RejectsAlphaToCoverageEnable`);
> the later feature-bit revision (`alphaToOne` on, `sampleRateShading` left off)
> only edits assertions inside the pre-existing `PhysicalDeviceInfoTest` case,
> adding no further test. `Vulkan14FeatureInventory.md` updated (`alphaToOne`
> now advertised, `sampleRateShading` explicitly noted as blocked on H7o);
> `VulkanExtensionInventory.md` confirmed no change needed (a core feature-bit
> row, not an extension). `FeMeVulkanDesign.md`'s H7 status paragraph updated.
> See "Roadmap H7f: measured impact" in VulkanCTSReport.md for the full
> reproduction
