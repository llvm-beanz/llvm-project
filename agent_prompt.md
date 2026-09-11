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

Can you work on H93b or other blocking work to make progress on the H-series
milestones?

> **`properties.mesh_payload_and_shared_memory_size`/`mesh_shared_memory_size`/`task_shared_memory_size`/`task_payload_and_shared_memory_size`'s
> `feme-cpu-linearize: ... has more than one divergent exit check ...`** (4
> cases: the original 2 (newly exposed by H90's own
> `spirv.SpecConstantOperation` fix) plus 2 more
> (`task_shared_memory_size`/`task_payload_and_shared_memory_size`) newly
> exposed by H91's own signature-metadata fix, hitting the identical
> diagnostic): `LoopLinearizer`'s own `OtherCondBrBlocks` classification finds
> two separately-divergent exit-check candidates in the same loop (this case's
> own shared-memory read/write verification loop has two separate per-invocation
> bounds checks -- a write-phase one and a read-phase one, per the CTS shader's
> own two `for` loops each guarded by `if (elemIdx < sharedMemoryElements)`) and
> conservatively refuses to linearize rather than attempting to fuse or
> otherwise support genuinely more than one divergent check per loop -- a
> documented, intentional "roadmap milestone 6 deviation" limitation, not a
> crash or miscompile. Not yet triaged -- needs its own real IR reduction of one
> of these four cases (mirroring L40/L42/H89a/H89b's own established
> `feme-cpu-linearize`-focused reduction technique) to determine whether the two
> checks can be fused/handled as a genuinely new `LoopLinearizer` capability, or
> whether this specific shape has some narrower, cheaper-to-support structure
> (e.g. one check strictly subsuming the other) that does not need full general
> multi-divergent-check support
