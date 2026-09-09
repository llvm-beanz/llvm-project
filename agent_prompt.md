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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L7 from the roadmap or other prerequisites blocking the
L-series milestones?

> **A long tail of genuinely unimplemented SPIR-V/MLIR legalization patterns
> this suite's own HLSL-derived shapes reach that no `deqp-vk` case in this
> project's own recorded runs has ever reached**: matrix
> `spirv.CompositeConstruct`/`spirv.AccessChain`/`spirv.Transpose`,
> combined-image-sampler `spirv.Image`, several `spirv.GL.*` builtins
> (`SmoothStep`/`Length`/`Distance`/`Atan2`/`Step`/`Normalize`/`UnpackHalf2x16`/`ImageDrefGather`),
> several `spirv.GroupNonUniform*` wave-op variants
> (`IMul`/`IAdd`/`AllEqual`/`Shuffle`/`Elect`/`BitwiseAnd`/`BitwiseOr` across
> multiple int widths -- the `BitwiseAnd`/`BitwiseOr` pair already has a
> narrower, tracked `feme-cpu-simdize`-side gap at H6g-b-a-i-a-i-b's own
> citation, but the *legalization* gap for the other variants is new and
> untracked anywhere), an `unhandled Decoration : 'NonUniform'`, an ~~`unknown
> extension: SPV_KHR_compute_shader_derivatives`~~ (fixed by roadmap L60's own
> closing session: `mlir/include/mlir/Dialect/SPIRV/IR/SPIRVBase.td`'s
> `Extension` enum had no case for this extension name at all, rejecting
> deserialization of any module declaring it outright; added as case 34, needing
> no companion capability/execution-mode change since the numerically-identical
> NV-named enum cases already cover those. See roadmap L60's own closure text
> for the full fix and CTS verification), and a couple of raw `unhandled
> opcode`/`unhandled deserializations ... from extension set GLSL.std.450`
> errors. None of this is mesh/ray-tracing/version-floor work already covered by
> an H/J/K row above (confirmed by grepping this document for every one of these
> names before adding this row); each needs its own scoping pass once assigned,
> split per coherent cluster rather than landed as one row, following H7/H19's
> own precedent
