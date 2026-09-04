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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H12 or other prerequisites blocking the H-series milestones?

> **Decide, once, which large optional extension groups stay out of scope**, and
> record the decision here rather than rediscovering it per run:
> `transform_feedback` (133,719 cases), `shader_object` (243,853),
> `fragment_shading_rate`, `fragment_shader_interlock`,
> `fragment_shading_barycentric`, `conditional_rendering`,
> `descriptor_indexing`, `sparse_resources`, `protected_memory`, `video`. None
> is required for a 1.4 submission; `descriptor_indexing` is the one with a real
> conformance consequence, since J-series ray tracing needs a large part of it
> (see §1.9.8's J2). Everything else defaults to out of scope, per Part 4

Of the listed optional groups, video is the only one that should be out of scope
for this effort.
