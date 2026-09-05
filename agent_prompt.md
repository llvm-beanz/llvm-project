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

Can you work on L30 or other prerequisites blocking the L-series milestones?

> **1 of L22's own 13 cases (`Graphics/MeshShaders/SimpleAmplification.test`) is
> unaffected by any of L22's own fixes and still fails identically**: `"JIT
> session error: Symbols not found: [ in.var.payload ]"`, `gpu-exec: error:
> Failed to create mesh shader pipeline. (VkResult = -3)` -- a pre-existing gap
> in mesh-shader amplification-stage payload plumbing (the payload symbol an
> amplification shader's task-to-mesh handoff relies on is never defined),
> unrelated to texture sampling, decorations, matrix constants, or composite
> construction. Needs its own scoping pass to determine how much of
> amplification-shader payload support is missing before a real fix can be
> designed -- likely a substantial, multi-part gap (payload ABI,
> task-shader-to-mesh-shader data handoff, JIT symbol registration) rather than
> a small legalization fix
