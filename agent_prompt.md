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

Can you work on L7i from the roadmap or other prerequisites blocking the
L-series milestones?

> **Advertise the rest of the Vulkan 1.1
> `VkPhysicalDeviceSubgroupProperties.supportedOperations` feature bits
> `Vote`/`Shuffle`/`ShuffleRelative` now that a real legalization pattern exists
> for at least one op in each** (`GroupNonUniformElect`/`AllEqual` for
> `Basic`/`Vote`, `GroupNonUniformShuffle` for `Shuffle`; `ShuffleRelative`'s
> own `ShuffleUp`/`ShuffleDown` still have no pattern at all and would need one
> first), split out of L7e's own closing session: `PhysicalDeviceInfo.cpp`'s
> `Info.SubgroupSupportedOperations` is hardcoded to
> `VK_SUBGROUP_FEATURE_BASIC_BIT` only, causing
> `dEQP-VK.subgroups.vote.*`/`shuffle.*`'s real CTS cases (confirmed via L7e's
> own re-run, 1,804 cases, 1,676 declined `NotSupported` purely on this
> capability gate) to never even execute against this ICD's now-real
> `Elect`/`AllEqual`/`Shuffle` conversion patterns. Needs: (1) advertising
> `VK_SUBGROUP_FEATURE_VOTE_BIT`/`VK_SUBGROUP_FEATURE_SHUFFLE_BIT` in
> `PhysicalDeviceInfo.cpp` once each op family backing it is confirmed complete
> for every operand type/width the relevant CTS group exercises
> (`dEQP-VK.subgroups.vote.*` alone covers `bool`/`bvec2-4`/`int8_t`-`int64_t`
> and unsigned/float variants --
> `ElectConversionPattern`/`AllEqualConversionPattern` today only handle the
> plain scalar 32-bit-class shapes any known HLSL source reaches, not this full
> type matrix), and (2) a real `deqp-vk` re-run of the newly-unlocked groups to
> confirm they now pass rather than merely stop being skipped. UPDATE this
> session: the actual missing op-family coverage the
> `GroupNonUniformVote`/`GroupNonUniformShuffle` capabilities need is now real
> and verified -- added `VoteConversionPattern<GroupNonUniformAllOp/AnyOp>`
> (both always scalar-`i1`-only per the dialect's own type constraints, an exact
> match for `dEQP-VK.subgroups.vote.*`'s own always-scalar-bool
> `subgroupAll`/`subgroupAny` test shapes, confirmed via a direct read of
> `vktSubgroupsVoteTests.cpp`), extended `AllEqualConversionPattern` to accept a
> vector `Value` operand (AND-reducing `llvm.spv.wave.all_equal`'s own
> per-component `<Nxi1>` result down to the single scalar `i1`
> `spirv.GroupNonUniformAllEqualOp` always requires, via
> `llvm.intr.vector.reduce.and`, the same collapse upstream's own unrelated
> `spirv::AllOp` conversion already uses), and added
> `ShuffleXorConversionPattern` (mirroring `ShuffleConversionPattern`, computing
> `SubgroupInvocationID XOR Mask` first). Also found and fixed a real,
> previously-undetected bug this same investigation surfaced:
> `ElectConversionPattern`/`AllEqualConversionPattern` (from L7e) were emitting
> `llvm.call_intrinsic` with the *tablegen-def-spelled* names
> `"llvm.spv.wave.is_first_lane"`/`"llvm.spv.wave.all_equal"` (underscores)
> rather than the *real* LLVM intrinsic names TableGen actually mangles
> multi-word segments into
> (`"llvm.spv.wave.is.first.lane"`/`"llvm.spv.wave.all.equal"`, dots) --
> invisible to L7e's own MLIR-dialect-level lit tests (which only check the
> textual IR, never resolve the string against LLVM's real intrinsic table), but
> fatal at real `offloader` pipeline-creation time (`could not find LLVM
> intrinsic: llvm.spv.wave.is_first_lane`), meaning `Elect`/`AllEqual`'s
> glslang/SPIR-V-origin path (as opposed to their DXIL-origin path, which
> reaches the same intrinsics via a real `Intrinsic::spv_wave_*` C++ enum ID
> rather than a hand-written string, and so was unaffected) has *never actually
> worked* against a real ICD since L7e landed -- fixed alongside this row's own
> new patterns. Real, end-to-end verification: `check-feme` (2,814 tests, 0
> failures) plus a real `check-hlsl-vk-feature-waveops` (`offloader`) re-run
> confirms `WaveActiveAllTrue`/`WaveActiveAnyTrue`/`WaveActiveAllEqual` (32-bit
> scalar) now produce byte-exact `BufferExact` matches via the real glslang/dxc
> `-spirv` path (previously impossible to reach at all, given the intrinsic-name
> bug above). A real `deqp-vk` re-run of `dEQP-VK.subgroups.vote.*`/`shuffle.*`
> with `Info.SubgroupSupportedOperations` *temporarily* patched to include
> `VOTE_BIT`/`SHUFFLE_BIT` was then attempted to determine the real pass rate
> needed to honestly justify the flip -- but every single compute-stage case
> (the only stage this ICD supports subgroup ops on at all) fails outright with
> `failed to legalize operation 'spirv.SpecConstantComposite'`, a **new,
> unrelated, and far more fundamental** blocking gap: every
> `dEQP-VK.subgroups.*` compute-stage shader declares its `gl_WorkGroupSize` via
> a `LocalSizeId`-style specialization-constant composite that the shader body
> itself then reads back (not merely an execution-mode operand
> `ExecutionModeIdPattern` already erases, unlike `SpecConstantErasurePattern`'s
> own scalar case), and `spirv.SpecConstantComposite` (plus its necessary
> companion, `spirv.mlir.referenceof`) has no legalization pattern anywhere in
> this project or upstream MLIR at all -- meaning **zero** `dEQP-VK.subgroups.*`
> compute-stage cases (of any op, not just `Vote`/`Shuffle`) can reach pipeline
> creation against this ICD today, an entirely separate and pre-existing gap
> this row's own patterns did not create and are not equipped to fix. Given
> this, `Info.SubgroupSupportedOperations`'s real capability-bit flip is
> honestly **not yet justified** (0/805 confirmed passing in the real sweep, all
> 36 non-`NotSupported` cases failing on this unrelated gap rather than on
> anything this row's own patterns touch) and is **not** made this session --
> `VOTE_BIT`/`SHUFFLE_BIT` remain unadvertised pending the new blocking row
> below. Split out: L7j, tracking the
> `spirv.SpecConstantComposite`/`spirv.mlir.referenceof` gap this session's own
> real `deqp-vk` attempt discovered as the actual, more-fundamental prerequisite
> now blocking not just this row but effectively every `dEQP-VK.subgroups.*`
> (and likely much broader) compute-stage CTS coverage
