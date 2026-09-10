---
model: claude-opus-5
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

Can you work on L89e from the roadmap or other prerequisites blocking the
L-series milestones?

> **`spirv.GroupNonUniformBroadcast`/`GroupNonUniformBroadcastFirst` are
> unimplemented in the SPIR-V -> LLVM conversion, failing all 336
> `dEQP-VK.subgroups.ballot_broadcast.compute.*` cases**, found by L89b's own
> regression sweep of `dEQP-VK.subgroups.ballot*` (6,284 cases, 16 passed / 336
> failed / 5,932 unsupported). Every failure is the same
> `VK_ERROR_INITIALIZATION_FAILED` from `vkCreateComputePipelines`, and the
> driver-side diagnostic is a conversion-legalization error, not a `feme`-IR
> one: `error: failed to legalize operation 'spirv.GroupNonUniformBroadcast'
> that was explicitly marked illegal: %148 =
> "spirv.GroupNonUniformBroadcast"(%145, %147) <{execution_scope =
> #spirv.scope<Subgroup>}> : (i1, i32) -> i1`. The failures cover the group
> exhaustively -- all 48 distinct
> `subgroupbroadcast`/`subgroupbroadcast_nonconst`/`subgroupbroadcastfirst` x
> scalar/`vec*`/`ivec*`/`uvec*`/`bvec*` shapes, each x 7 subgroup-size variants
> -- so this is a wholly missing op pair rather than a type-specific gap; the
> sibling `ballot`/`ballot_mask`/`ballot_other` groups in the same sweep have no
> failures. Confirmed pre-existing and unrelated to L89b, which changed only
> `lowerReadLane`'s body in `WaveLowering.cpp`, a pass that runs long after
> SPIR-V conversion. Needs conversion patterns mapping both ops onto the
> existing `feme.cpu.wave.readlane` / first-active-lane machinery
> (`GroupNonUniformBroadcast` is `readlane` with a subgroup-uniform index;
> `GroupNonUniformBroadcastFirst` is a read from the first active lane, which
> `getClampedFirstActiveLaneIndex` already computes), including the `i1` and
> vector operand shapes the group exercises, plus lit coverage in the
> SPIR-V-to-LLVM conversion tests
