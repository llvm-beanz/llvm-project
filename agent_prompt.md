---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work the the H-series milestones?

The last session suggested the next steps:

> **Next session: H7x**, via the real-CTS-image channel reduction this
> session's predecessor already recommended (pull the actual failing
> `dEQP-VK.clipping.user_defined.*_fragmentshader_read` case's own
> rendered image and expected image, diff channel-by-channel, mirroring
> H88's own closing technique for `local_size_id_mesh`/`local_size_id_
> task`). This is the highest-leverage open item in the whole H-series:
> one fix closes both H32 and H53. Do not repeat this and the last two
> sessions' synthetic-unit-test bisection approach -- it has now twice
> produced a false positive (see the two "H7x debunked" entries above)
> without finding the real bug; a real captured CTS image is the
> untried lever.
>
> **If H7x stalls again**, pivot to **H52** instead (SIGSEGV crash,
> isolated scope, unrelated area, blocks accurate measurement of the
> whole `tessellation.*` group) rather than re-attempting H7x a third
> time with the same technique.
