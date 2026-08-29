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
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you complete H6c-a?

> **Wire `MeshOutputBuilder`/`TaskPayloadBuilder` into real `feme.stage.*`
> mesh-output-store/task-payload-store operations** reaching the reused
> `EntryWrapperPass` path, once those ops exist (H6d) and, for task payload
> specifically, once it can be imported from SPIR-V `TaskPayloadWorkgroupEXT` at
> all (H6h) and canonicalized (H6i) (investigated, not landed: direct inspection
> of `feme/include/feme/Core/StageOps.h`'s `StageOpKind` enum,
> `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`, and
> `CanonicalizeStagePass::run`'s stage filter
> (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`) confirms all three
> named prerequisites -- H6d, H6h, H6i -- are still completely unimplemented: no
> mesh-output-store/task-payload-store `feme.stage.*` op exists,
> `TaskPayloadWorkgroupEXT` still has no address-space convention or import
> pattern, and the canonicalization stage filter still does not accept
> `ShaderStage::Mesh`/`Amplification`. Unlike H6b's own investigation, which
> found a real, independently-landable fix inside its own stated scope, this row
> has zero such content: there is nothing yet producing a canonicalized
> mesh-output-store/task-payload-store operation for
> `MeshOutputBuilder`/`TaskPayloadBuilder` to be wired to. Split below into two
> independently-trackable rows, since mesh output and task payload do not
> actually share the same blocker set -- mesh output's own canonicalization
> already exists (H6b's `feme.stage.output.store` `Vertex` operand), so wiring
> it only needs H6d (meshlet assembly to consume it) and H6i (lifting the stage
> filter so a mesh entry is canonicalized at all, not H6h, which is
> task-payload-import-specific); task payload additionally needs H6h before H6i
> can canonicalize a payload write into anything. See "Roadmap H6c-a: why this
> row could not land" in VulkanCTSReport.md)
