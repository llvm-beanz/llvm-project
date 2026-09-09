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

Can you work on L7g from the roadmap or other prerequisites blocking the
L-series milestones?

> **A couple of raw `unhandled opcode`/`unhandled deserializations ... from
> extension set GLSL.std.450` errors**, the leftover tail of L7's own original
> filing text not otherwise claimed by L7a-L7f above. UPDATE (L7b's own closing
> investigation this session): a real, concrete repro for the `unhandled opcode`
> half is now confirmed -- offload-test-suite's own
> `Vk.SampledTexture2D.Gather.test.yaml` (a real dxc-compiled HLSL
> `Texture2D::Gather()` call, confirmed via a real `check-hlsl-feme-vk` re-run)
> fails with exactly `unhandled opcode 96`. SPIR-V opcode 96 is `OpImageGather`
> (the non-depth-comparison gather variant), confirmed via direct inspection to
> have **zero support anywhere in upstream MLIR's SPIRV dialect** --
> `SPIRVBase.td` jumps directly from opcode 95 (`OpImageFetch`) to opcode 97
> (`OpImageDrefGather`), skipping 96 entirely (no enum case, no op definition,
> no deserialization case). A materially larger gap than an ordinary `feme`-side
> legalization-pattern gap (c.f. L7d's own `spirv.ImageDrefGather` case, whose
> op already exists upstream): needs new upstream-style MLIR dialect work (a new
> `spirv.ImageGather` op definition, deserialization case, verifier, and
> printer/parser, mirroring the existing `spirv.ImageDrefGather`'s own shape)
> before any feme-side legalization pattern can even be written against it. This
> row now stays open scoped specifically to this one confirmed opcode-96 case
> plus whatever remains of the original "GLSL.std.450" deserialization half
> (still unconfirmed)
