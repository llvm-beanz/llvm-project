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

Can you work on L89a from the roadmap or other prerequisites blocking the
L-series milestones?

> **`SIMDizePass` needs a bounded-basic-block (e.g. lane-chunked or loop-based)
> codegen strategy for wide required subgroup sizes**, split out of L89's own
> closing session -- CORRECTED and re-scoped this session: implemented and
> empirically tested the originally-proposed "naive" mitigation (a new, late
> `feme::cpu::BoundBlockSizePass` module pass, run after `OptimizerPipeline` to
> survive `SimplifyCFG`, mechanically chunking any oversized basic block into
> fixed-size pieces joined by unconditional branches via `llvm::SplitBlock` --
> semantically a no-op, preserving execution order/dominance) against the exact
> motivating case. Confirmed via `llc -O2 -time-passes` on captured `.ll` dumps
> of the *same* shader before/after chunking that this approach is **actively
> counterproductive**: the un-chunked single ~2200-instruction block compiles in
> ~2.7s total (~1.55s "Instruction Scheduling"), while the identical shader
> chunked into ~9 blocks of 256 instructions each takes ~40s total (~32.3s
> "Instruction Scheduling" -- roughly a **21x slowdown** in that specific
> sub-phase), and the full `deqp-vk` case did not even complete within a 300s
> timeout (worse than the ~210s unfixed baseline). Root cause of the regression
> (not yet fully profiled at the `MachineInstr` level, but consistent with the
> data): naive equal-sized chunking multiplies the number of
> block-boundary-crossing SSA values (via `CopyToReg`/`CopyFromReg`
> register-class plumbing at each new synthetic edge) without reducing the
> underlying live-range/critical-path footprint at all, since split points were
> chosen purely by instruction count, not by minimizing cross-block liveness --
> so it adds scheduling/regalloc bookkeeping overhead on top of the original
> cost rather than replacing it. This **falsifies** "just split the block" as a
> viable fix; the `BoundBlockSizePass` prototype and its
> `feme-opt`/`CompiledStage.cpp` wiring were reverted, not committed.
> Separately, instrumented `createStage` with per-call IR-content hashing and
> confirmed the ~210-300s wall-clock cost for this one CTS case is *also* driven
> by ~42 separate `vkCreateComputePipelines`-triggered JIT compiles for what CTS
> treats as parameter-swept sub-cases (not one pathological compile) -- of which
> roughly half (21 of 42, in 4 distinct hash buckets recurring 5-6x each) are
> byte-for-byte-identical IR, while the rest are genuinely distinct per-sub-case
> compiles. Both findings are recorded and broken out below rather than
> re-attempted in this session's remaining budget: L89b (the real fix -- an
> actual loop-based lane-chunking redesign of `SIMDizePass`'s emission strategy
> that reduces live-value count per region, not just block size) and L89c (an
> implicit, always-on, device-level compiled-artifact cache reusing
> `PipelineCache.h`'s existing content-hash scheme, independent of the
> app-supplied `VkPipelineCache` handle, to eliminate the confirmed ~50%
> genuinely-redundant-compile fraction as a complementary, lower-risk
> mitigation). `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md`: no
> change this session (no bit flips; `SHUFFLE_BIT` remains blocked, now on
> L89b/L89c rather than L89a directly). `ninja check-feme` (all experimental
> source changes reverted before finishing; tree matches pre-session baseline):
> 2,902 discovered, 2,843 passed, 59 unsupported, 0 failed
