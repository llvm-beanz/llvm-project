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

Can you work on milestone H4c?

> **`splitTessellationControlEntry` (H4a) explicitly rejects a
> tessellation-control shader whose patch-constant phase reads back an SSA value
> computed before the barrier** (24 of H4b's own measured 227
> `dEQP-VK.tessellation.*` `Fail`s, diagnosed rather than mis-compiled:
> `"tessellation-control SPIR-V entry point's patch-constant region cannot yet
> capture SSA values defined before the barrier"`), yet this is the common, real
> GLSL-compiled shape -- computing a per-patch tessellation factor from data
> derived from the control-point body (e.g. control-point output positions) and
> referencing it after `OpControlBarrier`. Needs the split to either
> re-materialize/clone the relevant pre-barrier computation into the
> patch-constant phase (correct only when that computation reads no other
> invocation's per-vertex output, i.e. is not itself a cross-invocation
> reduction) or thread the captured value through a synthetic patch-scoped
> storage location both phases can address (likely the right shape in general,
> since it mirrors how the control-point phase's own per-vertex outputs already
> cross the barrier through patch-shared storage)
