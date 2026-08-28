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

Can you complete H6g-b-a-i-a-i?

> **`feme::cpu::UnsupportedOps` rejects a register-bound resource handle that
> survives to the FeMe CPU target's normalization pass ("unsupported raised
> operation: ... is a register-bound resource handle the FeMe CPU target cannot
> normalize into a heap access or the root-constant block ...")**, now the
> single dominant cause found within H6g-b-a-i-a's own 218-case
> `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket (82 of 218 cases hit
> exactly this, per a diagnostic-logged re-run of that bucket alone, up from a
> pre-existing, already out-of-scope 1 -- the 81 cases H6g-b-a-i-a's own fix
> newly unblocked progress into this same failure), and newly reachable only now
> that H6g-b-a-i-a's own fix lets the `spirv.All`/`spirv.Any`-bearing SPIR-V
> this bucket's cases share actually legalize far enough to reach `feme-cpu`'s
> own resource-handle normalization pass. Root cause not yet isolated (which
> resource-declaration shape in this content the pass's existing "finite,
> unambiguous traditional binding, bindless access, or root-constant"
> recognition doesn't cover, and whether the fix belongs in
> `UnsupportedOps`/`RootConstantLowering` itself or an earlier canonicalization
> step feeding it)
