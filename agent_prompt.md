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

Can you close out L35(a) from the roadmap or other prerequisites blocking the
L-series milestones?

> **`feme-translate --import-spirv` crashes outright on any SPIR-V binary using
> the `ConstOffset`/`Offset`/`ConstOffsets`/`MinLod`/etc. image operands**, via
> an upstream MLIR SPIR-V dialect op verifier assert in
> `mlir/lib/Dialect/SPIRV/IR/ImageOps.cpp`'s `verifyImageOperands` (a literal
> `// TODO: Add the validation rules for the following Image Operands` followed
> by an unconditional `assert(!bitEnumContainsAny(...))` rejecting these operand
> bits outright) -- confirmed by L35's own original investigation against
> `Feature/Textures/Sample.test`'s own already-fixed, already-passing `.o`,
> which uses the same operands. Not blocking any real pipeline (the real Vulkan
> runtime path, `feme::SPIRVImporter`/`Pipeline.cpp`, uses
> `mlir::spirv::deserialize` directly without invoking full op verification, so
> it never hits this assert), but it is a real, narrow, pre-existing gap in this
> project's own standard real-IR-reduction tooling for any future row whose
> repro uses these operands -- needs either loosening/removing the specific
> assert (if MLIR upstream agrees the TODO can be resolved by accepting these
> operands without full validation today) or a new `feme-translate` flag to skip
> strict op verification on import. Not yet started
