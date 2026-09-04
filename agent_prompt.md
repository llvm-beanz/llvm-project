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

Can you work on H10h or other prerequisites blocking the H-series milestones?

> **`dEQP-VK.wsi.xcb.incremental_present.scale_none.fifo.identity.opaque.reference`
> fails `vkCreateGraphicsPipelines`**: `"'llvm.shl' op operand #1 must be
> signless integer or LLVM dialect-compatible vector of signless integer, but
> got 'si32'"`, discovered by H10b's own real `dEQP-VK.wsi.xcb.*` re-run (one of
> its original 8 real failures, mistakenly omitted from that row's own closure
> tally by a counting error -- corrected in H10b's own entry above). An MLIR
> verifier-level type-legality gap: `llvm.shl`'s second (shift-amount) operand
> must be a signless integer, but whatever SPIR-V-to-LLVM lowering step produced
> this particular shift left its operand as a *signed* (`si32`,
> SPIR-V-dialect-tagged) integer instead of first converting it to LLVM
> dialect's own signless convention -- entirely unrelated to
> WSI/CompositeConstruct/matrix-arithmetic (H10d/H10f) or to
> swapchain/device-group. Needs a real IR reduction of this exact case to find
> which lowering pattern emits an `llvm.shl` without first stripping/converting
> its shift-amount operand's SPIR-V signedness, and whether the same gap affects
> `llvm.lshr`/`llvm.ashr` identically
