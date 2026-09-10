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

Can you work on L86 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`OpCopyObject` (SPIR-V opcode 83) has zero support anywhere in upstream
> MLIR's SPIR-V dialect**, split out of L7f's own closing session: confirmed via
> `grep -rln "CopyObject" mlir/include/mlir/Dialect/SPIRV/
> mlir/lib/Dialect/SPIRV/ mlir/lib/Target/SPIRV/` returning zero matches -- no
> op definition, no deserialization case, no serialization case, anywhere.
> Discovered as the second of two coupled gaps in a real, concrete
> `NonUniformResourceIndex()` HLSL repro (dxc compiles this to a `Texture2D`
> array index wrapped in `OpCopyObject %type %idx`, with the `NonUniform`
> decoration -- L7f's own now-fixed gap -- attached to the copy's own result id
> rather than the original value): even with L7f's decoration fix in place,
> deserialization of this real repro still fails, now on `"unhandled opcode 83"`
> one instruction later. In practice, essentially any real dxc-compiled shader
> using `NonUniformResourceIndex()` needs this op, since dxc's own codegen
> convention always pairs the two together, even though they are architecturally
> independent SPIR-V features. A materially larger gap than an ordinary
> `feme`-side legalization-pattern fix -- mirrors L7g's own `OpImageGather`
> precedent exactly: needs new upstream-style MLIR dialect work (a new
> `spirv.CopyObject` op definition, verifier, printer/parser, and both a
> deserialization and serialization case) before any `feme`-side legalization
> pattern converting it to LLVM IR can even be written, and before the real
> `NonUniformResourceIndex()` end-to-end HLSL repro can pass deserialization at
> all
