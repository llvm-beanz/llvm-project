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

Can you close out L36 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`Texture2DArray` implicit-LOD sampling also does not select the correct mip
> from screen-space derivatives in every case**, mirroring L34's own
> `TextureCube` finding: a real re-run of
> `Feature/Textures/Array.Sample.test`/`Array.SampleBias.test` (neither of which
> uses a nonzero texel offset or clamp at all, ruling out L26/L33's own scope
> entirely) now clears pipeline creation and submission cleanly after L26's fix,
> but produces 3 wrong-mip output mismatches (elements 5/13/14) -- a
> pre-existing, unrelated implicit-LOD-selection gap uncovered only because
> these cases could not reach this far before L26's fix. Needs its own real IR
> reduction (once L35's own `feme-translate` tooling blocker is worked around,
> or via a reduced case avoiding `ConstOffset`/`MinLod` entirely) to confirm
> whether `femeCpuImageSample2DArrayV4F32`'s own derivative/LOD-planning path
> (mirroring `femeRTPlanImplicitLod`) has a real bug, or whether (like L34) it
> never computes one at all for the array shape
