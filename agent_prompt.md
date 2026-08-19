---
model: claude-sonnet-5
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
after each change and update the VulkanCTSReport.md.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement the C4 milestone for Vulkan conformance?

> **Graphics pipeline state breadth.** `mapTopology` beyond triangles (point,
> line, line-strip, fan), `mapDynamicState` beyond its six states,
> `FRONT_AND_BACK` culling, dual-source blend factors, and the sample counts
> `isSupportedAttachmentSampleCount` declines. Every one of these is a
> rasterizer/executor feature, not a translation gap, so this is really G-track
> work surfaced by the Vulkan track. **Sub-step C4a, do first and separately:
> make every silent rejection diagnose itself.** A state-side rejection
> currently emits nothing at all, so triaging this bucket means reading
> `GraphicsPipeline.cpp` instead of the ICD's output
