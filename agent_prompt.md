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

Can you continue working on H16 or any prerequisite work required to complete
the H-series milestones?

> **A `magFilter=LINEAR` magnification sample still produces slightly-wrong
> (roughly +/-1 of 255 per channel) pixel values**, discovered via H15's own
> real re-run of `dEQP-VK.texture.filtering.2d.*`: 156 remaining `Fail`s, all
> real re-runs isolated to a `magFilter=LINEAR` case
> (`combinations.nearest.linear.*`, `combinations.linear.linear.*`, plus the
> `_mipmap_*` groups' own magnification cases) -- unlike H15's own gross
> wrong-mip-level symptom, this is a small, consistent rounding-scale
> discrepancy (e.g. `(50, 84, 171, 50)` rendered vs. `(49, 85, 170, 49)`
> reference), suggesting a bilinear-weight or format-conversion rounding-mode
> mismatch rather than a wrong texel or wrong level being read. Not yet
> root-caused: needs a real, careful comparison of `femeRTSampleLinear2D`'s own
> float-domain blend arithmetic (`FeMeRuntimeCPU.c`) and the UNORM8
> encode/decode round-trip against the exact rounding convention
> `tcu::TexLookupVerifier`'s own tolerance expects, to determine whether the gap
> is in the bilinear weight computation itself, the final float-to-UNORM8
> quantization, or a genuine (if small) tolerance mismatch in the verifier's own
> assumptions
