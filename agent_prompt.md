---
model: claude-opus-5
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

Can you work on L89c from the roadmap or other prerequisites blocking the
L-series milestones?

> **Advertise `VK_SUBGROUP_FEATURE_SHUFFLE_BIT`, now that its last known blocker
> (L89d) is closed**, split out of L89b's own closing session: the bit has been
> gated behind a moving list of blockers since roadmap L7-series work, most
> recently L89 (compile-time blowup, fixed by L89b) and L89d (`SIMDizePass`'s
> missing vector-typed `wave.readlane` decomposition, fixed in the same
> session). `dEQP-VK.subgroups.shuffle.compute.*` now runs 1,680 cases with 128
> passed and **0 failed**, so the direct evidence for the flip exists -- but it
> was deliberately not made, because advertising a subgroup feature bit changes
> what CTS asks of the device across the *whole* `dEQP-VK.subgroups.*` tree (the
> 1,552 currently-unsupported cases in that one group alone become live, plus
> every `shuffle`-gated case in the `graphics`/`framebuffer`/`ray_tracing`
> shader-stage variants of the same tests), and this milestone chain has already
> produced three separate rows whose claims were later corrected for exactly
> this kind of under-verified extrapolation. Needs the bit added to
> `PhysicalDeviceInfo.cpp`'s `supportedOperations`, then a real full-tree
> `dEQP-VK.subgroups.*` sweep before and after the flip, with
> `Vulkan14FeatureInventory.md` updated for the first `SHUFFLE_BIT` change in
> the chain
