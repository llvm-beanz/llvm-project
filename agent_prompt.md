---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you close out L82 from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real `offloader` re-run of `DomainSystemValues.test` after roadmap L81's
> fix now runs the pipeline to completion (no `VkResult` failure, no
> `vkCreateGraphicsPipelines` failure), but the result buffer still fails an
> exact-match comparison against `ResultBuffer_Expected` by exactly 1 ULP on a
> handful of interpolated position/`uv` elements** (e.g. `0x3e800000` expected
> vs. `0x3e7fffff` observed -- `0.25` vs. `0.24999997`; `0x3f400000` vs.
> `0x3f400001` -- `0.75` vs. `0.75000006`), confirmed via a real `offloader`
> re-run of this exact repro after L81's fix. Entirely unrelated to L81's own
> `SV_PrimitiveID`-classification scope: every `SV_PrimitiveID`-forwarded value
> in the same buffer now matches exactly, isolating the remaining mismatch to
> the domain shader's own bilinear bounding-quad interpolation
> (`lerp(patch[0].position, patch[1].position, uv.x)` etc. in this repro's real
> `domain.hlsl`) or the tessellator's own domain-coordinate generation feeding
> it -- a genuine, if narrow, floating-point-rounding discrepancy against the
> reference values, not a logic/addressing bug like L77-L81's chain. Needs its
> own real IR reduction (the same technique this project's
> H6-series/H8-series/H9-series/L-series chains have used throughout) to isolate
> whether `feme::graphics::tessellate`'s own domain-coordinate generation
> (`Tessellator.cpp`) or the compiled domain shader's own `lerp`-to-IR lowering
> (e.g. fused-multiply-add contraction differing from the reference
> implementation's own arithmetic order) is the source of the 1-ULP drift,
> before a real fix can be scoped. Not yet started.
