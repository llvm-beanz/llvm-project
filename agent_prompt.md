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

Can you work on H8s or other prerequisites blocking the H-series milestones?

> **Re-audit every prior H8 row's CTS-verified claim now that the CTS-runner
> environment-variable bug (H8g) is fixed.** `VK_ICD_FILENAME` (singular, used
> by every prior session's `deqp-vk` invocation) is silently ignored by the
> Vulkan loader, which only reads `VK_ICD_FILENAMES` (plural); this environment
> has that plural variable globally exported to Mesa's own lavapipe ICD, so
> every `deqp-vk` run in this project's history before this row validated
> lavapipe's own conformant driver, not feme, and every "N/N Pass" CTS number
> this doc records for H8a-H8q (and likely many other rows outside H8) is
> unverified for feme specifically. A real re-run of
> `dEQP-VK.api.info.format_properties.*` against feme's own ICD
> (`VK_ICD_FILENAMES=.../feme_icd.json`, loader var spelled correctly) found 24
> remaining real failures this session did not have time to fix, several
> contradicting rows already struck through as done:
> `a2b10g10r10_unorm_pack32`/`b8g8r8a8_unorm` missing `VERTEX_BUFFER_BIT`
> (reopens H8a/H8b's own scope), `r16g16_{sint,uint}` missing
> `UNIFORM_TEXEL_BUFFER_BIT` (reopens H8d's own scope), a much broader
> `COLOR_ATTACHMENT_BIT`/`_BLEND_BIT` gap than H8e/H8p scoped -- including
> *non-integer* formats H8p's own integer-only framing did not cover
> (`r8_unorm`, `r8g8_unorm`, `r16_sfloat`, `r16g16_sfloat`) alongside more
> integer formats than H8p's 7 (`r16g16b16a16_{sint,uint}`,
> `r32g32b32a32_{sint,uint}`, `r8_{sint,uint}`, `r8g8_{sint,uint}`) -- plus new
> `SAMPLED_IMAGE_BIT`/`STORAGE_IMAGE_ATOMIC_BIT` gaps for
> `r32_{sint,uint}`/`r32g32_{sint,uint}` and `STORAGE_IMAGE_BIT` gaps for
> `r32g32_sfloat`/`r8g8b8a8_{uint,unorm}`. Each cluster needs its own scoping
> pass (mirroring H8e's own split into H8p/H8q) before a fix -- this row exists
> to make sure that re-scoping actually happens rather than the gaps staying
> invisible behind a stale "done" strikethrough
