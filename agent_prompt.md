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

Can you continue working on H18 or any prerequisite work required to complete
the H-series milestones?

> **`b10g11r11_ufloat` texture filtering fails independently of any mip/filter
> combination**, discovered via roadmap H16's own real re-run of
> `dEQP-VK.texture.filtering.2d.*`: the remaining fails not explained by H17's
> own trilinear gap are all `formats.b10g11r11_ufloat.*` -- no format-neutral
> pattern involved, so this is a distinct, format-specific gap, not
> trilinear-related. H17's own closure re-run corrects the count from 4 to the
> real 6 (all six
> `formats.b10g11r11_ufloat.{linear,linear_mipmap_linear,linear_mipmap_nearest,nearest,nearest_mipmap_linear,nearest_mipmap_nearest}`
> cases fail, not just the 4 non-`_mipmap_linear`-suffixed ones H16's own
> narrower sample happened to catch). Not yet root-caused: needs a real
> pixel-level reduction of one of these cases (e.g.
> `formats.b10g11r11_ufloat.nearest`, the simplest -- no mip, no blend) to
> determine whether the bug is in this packed-float format's own encode/decode
> round-trip, or something else specific to this one format
