---
model: claude-sonnet-5
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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on milestone H2h?

> **`dEQP-VK.multiview.input_attachments` renders a totally blank (`Fail
> (Fail)`, every pixel black/transparent) image in every one of its 16 remaining
> cases** (H2g's own triage): unlike H2g's other two fixes (a wrong-but-present
> value), nothing is drawn at all, suggesting either the subpass-input read
> itself always returns zero/fails silently under multiview, or the
> pipeline/subpass wiring for a multiview input-attachment subpass never reaches
> the draw in the first place. Not yet root-caused; needs its own investigation
> (dump the subpass-input descriptor heap contents and confirm whether the
> fragment shader's `subpassLoad` executes at all, or short-circuits)

