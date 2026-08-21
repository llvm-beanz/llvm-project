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

Can you implement milestone E26 in the roadmap document?

> **Integer-format image sampling/loading.** No `feme.cpu.image.*` entry point
> returns an integer vector (every sample/load call is `<4 x float>`), so a
> mandatory-sampled `_UINT`/`_SINT` format (`R32G32B32A32_UINT`/`_SINT`,
> `R16G16B16A16_UINT`/`_SINT`, `R8G8B8A8_UINT`/`_SINT`, `R10G10B10A2_UINT`, per
> the spec's own "Mandatory Format Support" tables, roadmap E25's own
> investigation) cannot be sampled or `OpImageFetch`-loaded at all today, even
> though `SPIRVResourceLowering.cpp` already raises an integer
> `OpImageFetch`/`OpImageRead` to a canonical call. Needs a
> `feme.cpu.image.load.2d.v4i32` (and, if a filtered integer sample is ever
> legal in SPIR-V, a decision on what "filtering" even means for one) entry
> point in `FeMeRuntimeCPU.c`, the matching `ImageCalls.{h,cpp}` builder, and an
> integer decode table (`femeRTUnpackImageTexelI32`-shaped) analogous to E25's
> own `femeRTUnpackImageTexel`, before `formatFeatureFlags` can honestly set
> `SAMPLED_IMAGE_BIT` for any integer format
