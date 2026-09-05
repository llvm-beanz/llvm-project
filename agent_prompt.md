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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L28 or other prerequisites blocking the L-series milestones?

> **1 of L22's own 13 cases (`Feature/Semantics/InterpolationModifiers.test`)
> now clears pipeline creation (via L22's own `Centroid`-decoration fix) but
> fails at `vkQueueSubmit` instead, `VkResult = -3`**, with no diagnostic text
> emitted even with `-debug-layer` (the failure originates inside
> `offload-test-suite`'s own `Device.cpp` `"Failed to submit to queue."` wrapper
> around a real submission-time error feme's own runtime is not yet surfacing a
> cause for). This case exercises multiple interpolation-modifier combinations
> (`nointerpolation`, `sample`, `noperspective`, `centroid`) in one pixel
> shader; feme already has substantial `Centroid`-aware infrastructure
> (`Executor.cpp`, `FragmentWrapper.cpp`, `ValidateStage.cpp`,
> `CanonicalizeStage.cpp`), so the newly-reached failure is likely a genuine
> execution-time gap in one specific modifier combination rather than `Centroid`
> support being entirely absent; needs its own real reduction, isolating one
> interpolation-modifier combination at a time, to identify which one crashes
> submission and why
