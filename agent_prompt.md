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

Please work on H7m or any prerequisite work to continue making progress on the
H-series milestones.

> **A vertex-entry `vkCreateGraphicsPipelines` fails with "vertex stage wrapper
> requires attached feme.signature metadata"**, the vertex-side twin of roadmap
> H3a's own fragment-side finding (already flagged, in `VulkanCTSReport.md`'s
> own H3a write-up, as "not yet independently tracked for the vertex side").
> Found via H7e's own real `deqp-vk` reproduction, newly reachable only because
> H7e's own feature-bit flip lets
> `dEQP-VK.draw.renderpass.point_size_clamp.point_size_clamp_max` and 8 of
> `dEQP-VK.dynamic_state.monolithic.line_width.{dyna_static,static_dyna}.*`
> clear their own `wideLines`/`largePoints` gate for the first time; nothing
> point-size/line-width-specific about the gap itself. H3a's own fragment-side
> root cause (`SPIRVResourceLowering.cpp`/`ResourceLowering.cpp`'s
> `addResourceEnvParams` rebuilding the stage entry function via
> `Function::Create`+`copyAttributesFrom`, which silently drops the
> `!feme.signature` metadata `CanonicalizeStagePass` already attached) was fixed
> with an explicit `NewF->copyMetadata(&F, 0)` for both files at the time, which
> should already cover the vertex stage too (the fix was not stage-specific) --
> so this needs its own investigation into why a vertex-entry case still hits
> the bare "no metadata attached" error rather than H3a's fix simply working,
> not an assumption that H3a's fix missed the vertex side entirely
