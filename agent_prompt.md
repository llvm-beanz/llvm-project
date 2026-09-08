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

Can you close out L78 from the roadmap or other prerequisites blocking the
L-series milestones?

> **Both of L77's own named repros
> (`Feature/Semantics/{HullSystemValues,DomainSystemValues}.test`) now clear
> `vkCreateGraphicsPipelines` and command submission cleanly, but still fail
> their own `SystemValues` result check**: the real ICD's `ResultBuffer` comes
> back entirely zero (`[0x0, 0x0, 0x0, 0x0, ...]`) against a non-zero expected
> buffer (`[0x0, 0x0, 0x0, 0x1, 0x2, 0x3F800000, 0x3F800000, ...]`), confirmed
> via a real `offloader -debug-layer` re-run of both repros (`"Graphics Pipeline
> created."` with no further error, then a clean `Test failed: SystemValues` /
> `BufferExact` mismatch, not a crash or a `VkResult` failure) once L77's fix
> let both cases reach real execution for the first time. Entirely unrelated to
> L77's own execution-mode-merging scope -- a distinct, further-downstream gap
> in either the tessellator's real per-patch execution, the
> hull/domain/patch-constant stage-wrapping chain's real storage addressing, or
> the pixel shader's own read-back of the forwarded per-vertex/per-patch
> attributes, now reachable for the first time. Needs its own real IR reduction
> of one of these exact cases (the same technique this project's
> H6-series/H8-series/H9-series/L-series chains have used throughout) to isolate
> which stage's real output is actually going unwritten -- e.g. a temporary
> pre-rasterization buffer dump (mirroring this row's own quick unsuccessful
> `RenderTarget`-comparison probe, which hit an unrelated `Data:`-key
> YAML-schema restriction on an `OutputProps`-tagged buffer resource and was not
> pursued further) to confirm whether the tessellator ever emits primitives at
> all for these two real cases, before narrowing further. Not yet started.
