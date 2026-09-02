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

Can you work on H8e or other prerequisites blocking the H-series milestones?

> **`COLOR_ATTACHMENT_BIT`/`SAMPLED_IMAGE_BIT`/`SAMPLED_IMAGE_FILTER_LINEAR_BIT`
> gaps for packed and 16-bit integer formats.** The same `format_properties`
> re-run shows several formats (`r16_{sint,uint}`, `r16g16_{sint,uint}`,
> `a2b10g10r10_uint_pack32`, `a8b8g8r8_{uint,sint}_pack32`, `d16_unorm`'s own
> depth-sampling case, and the packed sub-byte families
> `a1r5g5b5_unorm_pack16`/`b4g4r4a4_unorm_pack16`/`e5b9g9r9_ufloat_pack32`)
> still missing one or more of these three bits -- not yet triaged for whether
> each is a genuine rendering-capability gap or a reporting-only one (i.e. the
> underlying sample/attachment path may already work for a format that just is
> not yet advertised)
