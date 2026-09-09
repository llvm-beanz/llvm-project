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

Can you work on L85 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`GroupNonUniformBallot`
> (`OpGroupNonUniformBallot`/`OpGroupNonUniformBallotBitExtract`/etc., SPIR-V
> opcode 341 for the `BitExtract` variant, `VK_SUBGROUP_FEATURE_BALLOT_BIT`) is
> entirely unimplemented anywhere in this project**, split out of L7t's own
> closing session: not a new regression -- confirmed via `grep -rln
> "GroupNonUniformBallot" feme/lib/ feme/include/` returning zero matches,
> entirely pre-existing and previously undiscovered by name (only implicitly,
> indirectly surfaced before now as "why does `dEQP-VK.subgroups.ballot.*` show
> `NotSupported`" rather than root-caused). Two real, distinct consequences
> discovered this session: (1) the entire, direct `dEQP-VK.subgroups.ballot.*`
> CTS group (correctly hidden today behind its own unadvertised `BALLOT_BIT`, so
> currently harmless); and (2) surprisingly, essentially the **entire**
> `dEQP-VK.subgroups.shuffle.*` CTS group too (a real flag-flip verification
> run: 256/9562 cases newly failing with `error: unhandled opcode 341` once
> `SHUFFLE_BIT` was speculatively advertised), because every non-rotate CTS
> shuffle test shader's own verification harness (not the shuffle operation
> itself) calls `subgroupBallot()`/`subgroupBallotBitExtract()` to check whether
> the lane it read from was active -- meaning `SHUFFLE_BIT` cannot be safely
> advertised until this gap closes, even though shuffle's own core
> `OpGroupNonUniformShuffle`/`OpGroupNonUniformShuffleXor` legalization is
> presumably already fine on its own (a distinct, already-implemented
> `WaveCallKind::ReadLane`-adjacent pattern, not itself blocked by this row). A
> materially larger prerequisite than a single-pattern fix: needs (1) new
> SPIR-V-to-LLVM legalization pattern(s) for each `OpGroupNonUniformBallot*`
> variant this ICD's own frontend reaches (mirroring
> `AllEqualConversionPattern`/`ShuffleConversionPattern`'s own precedent in
> `SPIRVToLLVMPatterns.cpp`), (2) a new `WaveCallKind::Ballot`-family
> CPU-runtime intrinsic/lowering path (`WaveCalls.cpp`/`feme::cpu::SIMDizePass`)
> producing the correct `<4 x uint32>`-shaped active-lane bitmask this ICD's own
> wave width actually needs, and (3) its own real `deqp-vk` verification pass
> across both the direct `ballot.*` group and a re-verification that flipping
> `SHUFFLE_BIT` afterward introduces zero regressions, before either bit is
> advertised in `PhysicalDeviceInfo.cpp`
