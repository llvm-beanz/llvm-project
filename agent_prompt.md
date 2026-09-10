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

Can you work on L87 from the roadmap or other prerequisites blocking the
L-series milestones?

> **The leftover "GLSL.std.450" half of L7g's own original filing text**
> (`unhandled deserializations ... from extension set GLSL.std.450`), split out
> of L7g's own closing session: never confirmed to a concrete repro across any
> of L7a-L7g's own investigations (all of which found and closed real, concrete
> `unhandled opcode`-shaped gaps instead -- L7c/L7d/L7f/L7g). Needs its own real
> IR reduction of whichever HLSL/GLSL shape emits a currently-unhandled
> `GLSL.std.450` extended-instruction-set builtin (dxc's own
> intrinsic-to-`GLSL.std.450`-builtin mapping covers dozens of
> math/bit-manipulation builtins --
> `FindUMsb`/`FindSMsb`/`InterlockedCompareStore` families and similar
> less-common HLSL intrinsics are plausible candidates, none yet confirmed)
> before scoping a fix, mirroring this row's own siblings' "reduce first, then
> fix" methodology throughout the L7-series
