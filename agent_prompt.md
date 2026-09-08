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

Can you close out L81 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`DomainSystemValues.test` (L77/L78/L79's own named repro) now reaches
> `vk.queueSubmit` after L79's vertex-attribute-fetch fix, but that submit fails
> with `VkResult = -3`**, a new failure mode not previously reached (before
> L79's fix, this repro failed earlier, during
> `vkCreateGraphicsPipelines`/pipeline validation, per L78's own filing).
> Confirmed via a real `offloader` re-run of this exact repro after L79's fix:
> `"Graphics Pipeline created."` now logs successfully, but `"Failed to submit
> to queue. (VkResult = -3)"` follows immediately, with no further diagnostic
> text captured yet. Unrelated to L79's own
> vertex-attribute-format-channel-count scope (that fix only changes which real
> data reaches this stage, not the pipeline/command-buffer validation path that
> now fails). Needs its own investigation to capture the real underlying
> validation error (likely via `FEME_VULKAN_LOG_CREATION_ERRORS=1` or an
> equivalent verbose-diagnostic path, not yet attempted for a `vkQueueSubmit`
> failure specifically) before a real IR reduction can even be scoped. Not yet
> started.
