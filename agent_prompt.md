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

Can you work on H21f or other prerequisites blocking the H-series milestones?

> **`VK_EXT_graphics_pipeline_library`-gated
> `simple_fast_gpl`/`simple_optimized_gpl` CTS groups** (15,782 of 133,719
> cases, 12%, per H21a's own scoping): blocked on a separate, unrelated
> `VK_EXT_graphics_pipeline_library` implementation existing at all -- no
> transform-feedback work alone can close these regardless of how complete
> H21c/H21d/H21e become. This dependency is now tracked as its own dedicated
> top-level milestone, H29, since a real `deqp-vk` case-list re-run (done while
> investigating this row) found the extension's own footprint is far larger than
> transform-feedback's own 15,782-case slice
> (`dEQP-VK.pipeline.pipeline_library.*` alone is 120,483 cases) -- see H29 for
> the real scope and why full implementation is out of reach of this row alone.
> This row stays open, blocked on H29, until that milestone lands
