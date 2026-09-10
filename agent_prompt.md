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

Can you work on L89 from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real `PostMachineSchedulerLegacy`/`ScheduleDAGInstrs::buildSchedGraph`
> compile-time hang (not a crash -- the process spins indefinitely, confirmed
> via `gdb -p <pid> -batch -ex bt` sampled mid-hang, consuming 100% CPU with no
> forward progress inside `SUnit::addPred`/`addChainDependencies`)**, split out
> of L88's own closing session: discovered via a real `deqp-vk` re-run of
> `dEQP-VK.subgroups.shuffle.compute.*` (initially run as a speculative
> `SHUFFLE_BIT` re-verification after L88's own fix, but confirmed to reproduce
> identically against the real, currently-committed, un-flipped feature set too
> -- `subgroupclusteredrotate_*` cases exercise `subgroupClusteredRotate`
> regardless of whether `VK_SUBGROUP_FEATURE_SHUFFLE_BIT` is advertised, so this
> is a live, currently-reachable bug, not one hidden behind an unadvertised
> feature bit). Every `*_requiredsubgroupsize` variant of
> `subgroupclusteredrotate_float_dynamically_uniform`  (and, going by the shared
> shape, presumably every other `_requiredsubgroupsize` variant in the group)
> hangs indefinitely at pipeline-creation time (`vkCreateComputePipelines` ->
> `feme::cpu::CompiledStage::create` -> ORC JIT compile ->
> `llvm::legacy::PassManagerImpl::run` -> post-RA machine scheduling), confirmed
> via a live backtrace showing the hang is *inside* the LLVM AArch64 host
> backend's own post-regalloc instruction scheduler, not anywhere in `feme`'s
> own IR-level passes -- consistent with a real quadratic-or-worse blowup in
> `ScheduleDAGInstrs`'s memory-dependence-chain construction once a scheduling
> region's own basic block grows large enough, plausibly because "required
> subgroup size" forces this ICD's own wave-width resolution to a much wider
> lane count than the plain (un-suffixed) variant of the same case (which passes
> quickly), producing a proportionally larger unrolled/masked basic block for
> the scheduler to chew through. Not yet reduced to a minimal repro or profiled
> to confirm the exact quadratic mechanism (this session's own investigation
> stopped at "confirmed real, confirmed backend-side, confirmed size-sensitive"
> via a live-process backtrace and an A/B compare against the passing
> non-`requiredsubgroupsize` sibling case) -- needs its own IR-size profiling
> pass (e.g. dumping the actual scheduling-region instruction count for both the
> passing and hanging variants) to confirm the size-blowup theory, then either a
> `feme`-side fix (if `SIMDize.cpp`/`Linearize.cpp` produces needlessly large
> code for a wide required subgroup size that a real GPU driver would not) or an
> upstream LLVM performance investigation (if the scheduler's own complexity is
> inherently unfit for a code shape this ICD legitimately needs to produce for
> wide subgroups) before this CTS group can be swept in full or `SHUFFLE_BIT`
> considered further
