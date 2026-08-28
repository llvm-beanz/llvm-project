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

Can you complete H6g-b-a-i?

> **`ConvertSPIRVToLLVMPass` fails to legalize `spirv.AccessChain` outright
> ("failed to legalize operation 'spirv.AccessChain' that was explicitly marked
> illegal")**, now the single dominant cause found within H6g-b-a's own
> newly-shrunk 218-case `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket
> (80 of 218 cases still failing there hit exactly this, per a diagnostic-logged
> re-run of that bucket alone), and newly reachable only now that H6g-b-a's own
> fix lets `PerPrimitiveEXT`-decorated per-primitive-block SPIR-V actually
> deserialize far enough to reach legalization at all. Root cause not yet
> isolated (which `spirv.AccessChain` shape the existing conversion patterns
> don't cover -- a per-primitive-block member access through a
> `PerPrimitiveEXT`-decorated pointer specifically, given this row's own
> prerequisite, or something broader -- and whether the fix belongs in a
> new/extended conversion pattern or a legalization-target adjustment)
