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

Can you continue working on H19n or any prerequisite work required to complete
the H-series milestones?

> **The rest of the mandatory `shaderStorageImageExtendedFormats` list**, split
> out of H19j once it narrowed to the single-channel
> `R8_{UNORM,SNORM,UINT,SINT}` slice only. Needs entirely new `ResourceFormat`
> enum entries for `R8G8_{UNORM,SNORM,UINT,SINT}`,
> `R16_{UNORM,SNORM,UINT,SINT,SFLOAT}`, `R16G16_{UNORM,SNORM,UINT,SINT,SFLOAT}`,
> and `R32G32_{UINT,SINT}`'s own storage-mandatory partial-component siblings,
> plus the packed 32-bit formats
> `A2B10G10R10_{UNORM,UINT}_PACK32`/`B10G11R11_UFLOAT_PACK32` (still needing
> their own component-order verification against the existing sampled-image
> decode, per H19j's own unresolved note). Only once this list is materially
> complete should `shaderStorageImageExtendedFormats` itself flip to `VK_TRUE`,
> and only then can H19i's own `without_format.*` work proceed, since that row's
> 828 real CTS cases exercise this same full list (partially closed: added the
> two-channel `R8G8_{UNORM,SNORM,UINT,SINT}` slice only (4 new `ResourceFormat`
> entries appended at the enum's own tail,
> `mapVkFormat`/`formatElementSize`/sampled- and storage-feature-bit wiring in
> `Format.cpp`, and 8 new always-inline pack/unpack helpers in
> `FeMeRuntimeCPU.c` -- each a straightforward two-lane widening of the existing
> single-channel `R8` helpers H19j added, packed/unpacked little-endian via
> `__builtin_memcpy` into/out of a `uint16_t` the same way the existing
> 4-component 16-bit-per-lane helpers already do -- wired into all five existing
> dispatch tables). `ImageFixture.cpp`'s
> `formatFixtureName`/`parseFixtureFormat` and `feme-run.cpp`'s
> `imageFormatElementSize` needed new cases too, same as H19j's own R8 slice.
> New unit tests: 8 in `ImageSamplingTest.cpp` (read float/int and write
> float/int paths), 1 new (`MapsTwoChannelR8G8Formats`) plus 2 widened in
> `FormatTest.cpp`. `ninja check-feme` passes in full, 2166/2225 (59
> pre-existing `Unsupported`, 0 `Failed`). A real re-run of
> `dEQP-VK.image.load_store.with_format.*.r8g8_*` (112 cases) confirms the fix:
> 88 Pass (up from 0), 0 Fail, 24 honestly `NotSupported` (the same shape
> variants outside existing coverage as H19j's own R8 re-run, unrelated to
> format). The `load-store.txt` mustpass regression caselist (3446 cases) shows
> 436 Pass (up from the H19j-era 348 baseline by exactly the +88 new R8G8
> passes), 0 Fail, 3010 `NotSupported` -- 0 regressions. `R16_*`, `R16G16_*`,
> `R32G32_{UINT,SINT}`, and the packed 32-bit formats all remain entirely
> unimplemented; `shaderStorageImageExtendedFormats` itself stays `VK_FALSE`.
> See "Roadmap H19n: measured impact" in `VulkanCTSReport.md`)
