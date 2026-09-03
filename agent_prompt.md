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

Can you work on H8h or other prerequisites blocking the H-series milestones?

> **`A2B10G10R10_UNORM_PACK32` (`R10G10B10A2_UNORM`) as a vertex attribute.**
> H8b deliberately deferred this one remaining mandatory `VERTEX_BUFFER_BIT`
> format: it is a single packed 32-bit word (2 bits A, 10 bits each of B/G/R,
> MSB-down), not a "N bytes per component" layout `decodeAttribute`'s existing
> convention fits mechanically -- needs its own dedicated decode case mirroring
> `femeRTUnpackR10G10B10A2Unorm`'s (`FeMeRuntimeCPU.c`) existing bit-unpacking
> convention, plus a `attributeComponentByteSize`-adjacent way to describe "one
> 4-byte fetch produces all 4 components" to the caller's own bounds-check
> arithmetic (`Executor.cpp`'s draw loop), which currently assumes one fetch per
> component | H8b | `feme/lib/Graphics/Executor.cpp`,
> `feme/lib/Vulkan/Format.cpp` | P2 | (`R10G10B10A2_UNORM`) as a vertex
> attribute.** H8b deliberately deferred this one remaining mandatory
> `VERTEX_BUFFER_BIT` format: it is a single packed 32-bit word (2 bits A, 10
> bits each of B/G/R, MSB-down), not a "N bytes per component" layout
> `decodeAttribute`'s existing convention fits mechanically -- needs its own
> dedicated decode case mirroring `femeRTUnpackR10G10B10A2Unorm`'s
> (`FeMeRuntimeCPU.c`) existing bit-unpacking convention, plus a
> `attributeComponentByteSize`-adjacent way to describe "one 4-byte fetch
> produces all 4 components" to the caller's own bounds-check arithmetic
> (`Executor.cpp`'s draw loop), which currently assumes one fetch per component
