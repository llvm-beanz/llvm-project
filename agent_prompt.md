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

Can you close out L35 from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real `Vk.SampledTexture2D.SampleBias.test.yaml` case (combining `Bias`, a
> nonzero texel `Offset`, and a `MinLodClamp` on the *same* `SampleBias` call)
> still fails `vkCreateGraphicsPipelines`, `VkResult = -3`**, on the same
> `"unsupported raised operation: ...handlefrombinding... is a register-bound
> resource handle..."` diagnostic L26 closed for the simpler (`Sample`-only, or
> one-modifier-at-a-time) shapes -- confirmed distinct from L26's own now-fixed
> scope by re-running the real case after L26's fix landed. Reduction is
> unusually hard: `feme-translate --import-spirv` (this project's own standard
> real-IR-reduction tool for cases like this) crashes outright on *any* SPIR-V
> binary using the `ConstOffset`/`MinLod` image operands at all -- confirmed
> identically against `Feature/Textures/Sample.test`'s own already-fixed,
> already-passing `.o`, which uses the same operands -- via
> `mlir/lib/Dialect/SPIRV/IR/ImageOps.cpp`'s `verifyImageOperands`, an upstream
> MLIR SPIR-V dialect op verifier with a literal `// TODO: Add the validation
> rules for the following Image Operands` followed by an
> `assert(!bitEnumContainsAny(...))` unconditionally rejecting
> `ConstOffset`/`Offset`/`ConstOffsets`/`MinLod`/etc. The real Vulkan runtime
> path (`feme::SPIRVImporter`, `Pipeline.cpp`) never hits this assert at all
> (confirmed: it built and ran L26's own already-fixed cases just fine), meaning
> the runtime's own `mlir::spirv::deserialize` call path does not invoke full op
> verification the way `feme-translate`'s own translate-registration does -- so
> this is a real, narrow, pre-existing *tooling* gap in `feme-translate
> --import-spirv` itself (not a new regression, and not blocking any real
> pipeline), but it means this row's own real reduction needs a different
> technique (e.g. temporarily disabling/loosening this specific assert for
> investigation purposes only, or adding a `feme-translate` flag to skip strict
> op verification on import) before the *actual* live gap -- some deeper
> interaction between combining three image-operand bits on one call and the CPU
> target's handle-normalization pass -- can be isolated and fixed
