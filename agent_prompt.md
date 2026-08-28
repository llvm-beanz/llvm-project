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

Can you complete H6g-b-a-i-a-i-a?

> **`feme-cpu-simdize` rejects a newly-unblocked divergent vector value in the
> same 218-case `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket ("function
> 'main' has a divergent vector value ... used outside a supported
> insertelement-chain/resource-store/extractelement/select/shufflevector/phi/elementwise
> pattern; component decomposition is not yet supported for this use")**, now
> the new dominant first-emitted FeMe/MLIR diagnostic in that bucket once
> H6g-b-a-i-a-i's own direct-storage-buffer-handle fix lets those shaders
> progress further (148 of 218 cases in a combined stdout/stderr diagnostic
> rerun land here first; H6g-b-a-i-a-i reduced its own named `UnsupportedOps`
> bucket from 82 cases to a lone, out-of-scope sampled-image/sampler remainder).
> Root cause not yet isolated: needs a real failing shader/IR reduction to
> identify which divergent-vector use shape in the newly-unblocked mesh/fragment
> content is still outside `SIMDizePass`'s supported decomposition patterns, and
> whether the right fix belongs in `SIMDize.cpp` itself or in an earlier
> canonicalization/legalization pass feeding it
