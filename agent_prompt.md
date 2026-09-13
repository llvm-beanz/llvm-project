---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work on H101c or other blocking work to make progress on the H-series
milestones?

> **`spirv_assembly.instruction.compute.compute_shader_derivatives.compute.verify_ndx.linear.128_1_1`'s
> GEP-operand-type legalization failure** (newly exposed by H101's own
> `AccessChainPattern` zero-index fix, which converted this case from a hard
> crash into this narrower, non-crashing pipeline-creation failure):
> `vkCreateComputePipelines` now fails cleanly with `'llvm.getelementptr' op
> operand #0 must be LLVM pointer type or LLVM dialect-compatible vector of LLVM
> pointer type, but got 'i32'` instead of crashing -- some other conversion
> pattern in this same shader (likely a `spirv.PtrAccessChain`,
> `spirv.InBoundsAccessChain`, or `spirv.InBoundsPtrAccessChain` op, none of
> which have a registered lowering pattern per H101's own investigation) is
> producing a GEP whose base-pointer operand ends up as a plain integer rather
> than an LLVM pointer, or a type-conversion step upstream of the GEP-emitting
> pattern is not converting a pointer-typed value correctly for this shader's
> specific derivative-related type shape. Not yet triaged -- needs a
> `feme-translate --import-spirv` dump of this shader's SPIR-V to identify the
> exact op producing the ill-typed GEP
