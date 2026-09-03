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

Can you work on H8q or other prerequisites blocking the H-series milestones?

> **`e5b9g9r9_ufloat_pack32` (`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32`) is an entirely
> unimplemented format, split off from H8e.** A real `deqp-vk` run found this
> format missing *every* required feature bit
> (`BLIT_SRC_BIT`/`SAMPLED_IMAGE_BIT`/`FILTER_LINEAR_BIT`/`TRANSFER_DST_BIT`/`TRANSFER_SRC_BIT`),
> the tell-tale sign `mapVkFormat` has no case for it at all (unlike H8p's own
> formats, which are each missing only one specific bit) -- there is no
> `ResourceFormat` enumerator, no pack/unpack support, nothing. A real fix needs
> a brand-new shared-exponent RGB9E5 packed format: a new
> `ResourceFormat::E5B9G9R9_UFLOAT` enumerator (appended at `RuntimeABI.h`'s own
> tail, per that file's append-only hard-coded-switch-case constraint), a
> `mapVkFormat` case, `packClearColor`/`unpackColor` shared-exponent
> encode/decode logic (distinct from every existing packed-format case in
> `ImageFixture.cpp`: a shared 5-bit exponent plus three independent 9-bit
> mantissas, needing real floating-point range-reduction math, not just bitfield
> extraction), and (if sampling is also wanted) a
> `femeRTImageFormatElementSize`/`femeRTUnpackImageTexel` runtime case mirroring
> `R11G11B10_FLOAT`'s own precedent for the closest existing packed-float format
