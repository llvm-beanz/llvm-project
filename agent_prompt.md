---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Please investigate and fix the issues tracked by milestone L10:

> **A handful of small, distinct residual cases from L2's own triage, each
> needing its own individual scoping rather than sharing a root cause**:
> `Feature/PushConstant/types.test`'s own "shader's root-constant span is not
> fully covered by a VkPushConstantRange" failure (the test's own
> `PushConstants:` YAML block only ever supplies one `float` value at offset 0,
> while the shader's own `S` struct spans several more members past it -- likely
> an `offload-test-suite`-side test-authoring gap in how its own harness sizes
> the generated `VkPushConstantRange`, not necessarily a `feme` bug, so needs
> checking against the harness's own range-sizing logic before assuming which
> side to fix); a couple of `feme-cpu-simdize` "groupshared global ... feeds an
> unrecognized broadcast"/"feeds a nested getelementptr" diagnostics
> (`WaveOps/GroupMemoryBarrierWithGroupSync.test`,
> `WaveOps/GroupSharedMatrixElementExprComponentDataRace.test`,
> `WaveOps/GroupSharedMatrixRowComponentDataRace.test`); an
> `'llvm.getelementptr'`/`'llvm.call'` "must be LLVM dialect-compatible type,
> but got 'si32'" MLIR-dialect-conversion error
> (`Feature/ResourceArrays/overflow-unbounded-array.test`,
> `WaveOps/WaveActiveSum.convergence.test`)
