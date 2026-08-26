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

Can you work on milestone H2i?

> **`dEQP-VK.multiview.readback_implicit_clear`'s multi-subpass view-mask
> combinations still fail** (18 of H2g's own triage, exactly the cases whose
> numeric suffix names more than one subpass -- e.g. `1_2_4_8`, `5_10_5_10`,
> `8_1_1_8` -- while every single-subpass combination, e.g. `15`, now passes
> after H2g's `vkCmdClearAttachments` fix): a per-subpass
> `VK_ATTACHMENT_LOAD_OP_CLEAR` interacting with multiple multiview subpasses
> (each its own view mask) inside one render pass instance is not yet correctly
> modeled -- root cause otherwise undetermined

