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

Can you continue working on H19f or any prerequisite work required to complete
the H-series milestones?

> **The format/configuration breadth
> `shaderStorageImageExtendedFormats`/`shaderStorageImageReadWithoutFormat`/`shaderStorageImageWriteWithoutFormat`
> actually need**, split out of H19d's own original bundled scope once H19d
> itself narrowed to cube/cube-array shape support only.
> `shaderStorageImageExtendedFormats` needs every `VkFormat` beyond the current
> 6-format mandatory floor (`Format.cpp`'s `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`
> advertisement) to actually round-trip through `FeMeRuntimeCPU.c`'s fetch/store
> helpers, not just the floor H19a hard-coded.
> `shaderStorageImageReadWithoutFormat`/`WriteWithoutFormat` need a shader to
> declare a storage image with no `layout(format)` qualifier at all (SPIR-V's
> `Format == Unknown`) and still read/write correctly against whatever format
> the bound view actually has at runtime, rather than assuming the compiled
> shader's own declared format always matches -- a real re-run of
> `dEQP-VK.image.format_reinterpret.*`/an unqualified-format subset of
> `load_store.*` needed to scope the actual gap size once attempted (partially
> closed: confirmed storage-image *reads* already worked for any format
> `femeRTImageFormatElementSize` recognizes -- `femeRTFetchTexel2D` et al. reuse
> the same `femeRTUnpackImageTexel(I32)` sampled-image reads use -- so the real
> gap was write-side only. Widened
> `femeRTPackImageTexel`/`femeRTPackImageTexelI32` (`FeMeRuntimeCPU.c`) and
> `Format.cpp`'s `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` switch to also cover
> `R16G16B16A16_{SFLOAT,UINT,SINT}` (a new `femeRTFloatToHalf` helper, the
> inverse of the existing `femeRTHalfToFloat`, for the float case; 16-bit
> truncation for the integer cases) -- three new unit tests in
> `ImageSamplingTest.cpp`, one widened in `FormatTest.cpp`. `ninja check-feme`
> passes in full, 2122/2181 (59 pre-existing `Unsupported`, 0 `Failed`). A real
> re-run of
> `dEQP-VK.image.load_store.with_format.*.r16g16b16a16_{sfloat,uint,sint}*` (140
> cases) confirms the fix: 66 Pass (up from 0), 0 Fail, 74 honestly
> `NotSupported` (the `_unorm`/`_snorm` variants of the same 4-channel-16-bit
> format, still outside this row's own pack support).
> `shaderStorageImageExtendedFormats` itself stays `VK_FALSE`: the real Vulkan
> mandatory extended-format list is far larger than the one format family this
> change closes (single- and two-channel `R8`/`R16`/`R32` formats,
> `10:10:10:2`/`11:11:10` packed formats, etc.), most without any
> `ResourceFormat`/pack-table entry yet.
> `shaderStorageImageReadWithoutFormat`/`WriteWithoutFormat` also stay
> `VK_FALSE`: `dEQP-VK.image.load_store.without_format.*`'s own `checkSupport`
> (`vktImageLoadStoreTests.cpp`) gates on the *format's own*
> `VK_FORMAT_FEATURE_2_STORAGE_{READ,WRITE}_WITHOUT_FORMAT_BIT` tiling features,
> which `Format.cpp` does not compute at all today (a distinct, larger gap than
> this row's own pack/unpack breadth work, and dependent on the full
> extended-format list existing first since `without_format.*`'s own 828 real
> CTS cases exercise exactly that list). Split into two new sibling rows below:
> H19h (the rest of the mandatory extended-format list) and H19i (the
> without-format format-feature-bit work))~~
