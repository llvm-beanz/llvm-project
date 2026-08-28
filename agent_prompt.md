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

Can you complete H6g-b-a-i-a-i-b?

> **A divergent vector value used as a vector comparison (`fcmp`/`icmp`) operand
> is rejected by `feme-cpu-simdize` in the same
> `vkCreateGraphicsPipelines`/`vkRefUtil.cpp:37` bucket ("function 'main' has a
> divergent vector value ... used outside a supported
> insertelement-chain/resource-store/extractelement/select/shufflevector/phi/elementwise
> pattern; component decomposition is not yet supported for this use")**, now
> the new dominant first-emitted FeMe/MLIR diagnostic in that bucket once
> H6g-b-a-i-a-i-a's own masked-store fix lets those shaders progress further (80
> of 218 cases in a combined stdout/stderr diagnostic rerun land here, e.g.
> `dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_0.mesh_only`'s `%8 =
> insertelement <4 x float> %6, float %7, i64 3` used by `%16 = fcmp ole <4 x
> float> %8, %15`, confirmed via the same one-off
> diagnostic-dump-and-single-case-rerun technique H6g-b-a-i-a-i-a used). Root
> cause not yet isolated: a vector comparison producing a `<N x i1>` result is
> not among `checkVectorDecompositionSupported`'s accepted consumer shapes at
> all today (unlike a matched resource-store's or masked-store's stored-value
> operand), and the design's own existing "a `select` with a per-lane `<N x i1>`
> condition remains diagnosed" deviation (`checkVectorDecompositionSupported`'s
> file comment) suggests decomposing a per-lane vector *condition* end-to-end --
> not just a per-lane vector *value* -- may need broader work than a single
> consumer-acceptance addition; needs a real failing shader/IR reduction to
> confirm the exact GLSL/SPIR-V source shape (a component-wise
> `lessThanEqual`/`greaterThan`-style comparison feeding a `select`, most
> likely) and whether the right fix is a narrow `fcmp`/`icmp`
> consumer-and-producer addition mirroring this row's own sibling, or the
> broader per-lane-condition decomposition the file comment already anticipates
