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

Can you work on L3 or other prerequisites blocking the H-series milestones?

> **A `gpu-exec: error: Failed to create render pass. (VkResult = -11)`
> (`VK_ERROR_FORMAT_NOT_SUPPORTED`'s numeric value) bucket** -- re-counted
> alongside L2: 35 distinct failing cases (34 originally estimated, effectively
> unchanged by L1/L4, as expected since neither fix touches render-pass/format
> code at all). Likely a real, currently-unadvertised format gap in this suite's
> own render-target format choices (distinct from any §1.9 `deqp-vk` coverage,
> which may simply never probe the same formats this HLSL suite's own YAML
> pipeline descriptions request); needs a per-case format survey to confirm
> before deciding between "extend format support" and "the format genuinely is
> out of scope, document why"

Ideally we should support all formats comprehensively unless there is a reason
why some format is not supportable.
