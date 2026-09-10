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

Can you work on L89b from the roadmap or other prerequisites blocking the
L-series milestones?

> **`SIMDizePass` needs a real loop-based (not post-hoc-block-split)
> lane-chunking redesign that reduces live-SSA-value count per scheduling
> region, not just instruction count per block**, split out of L89a's own
> closing session: L89a's own `llc -time-passes` A/B measurement proved that
> merely relocating the same fully-unrolled, all-lanes-simultaneously-live
> instruction sequence into more, smaller basic blocks (via post-hoc splitting)
> makes compilation dramatically slower (~21x on "Instruction Scheduling"
> alone), not faster, because it does not reduce the number of values live
> across region boundaries -- it only adds `CopyToReg`/`CopyFromReg` bookkeeping
> on top of the original cost. A viable fix therefore cannot be a mechanical,
> pass-order-only transform bolted on after `SIMDizePass`; it needs
> `SIMDizePass` itself (`feme/lib/Transforms/CPU/SIMDize.cpp`, 3714 lines) to
> emit a genuine loop over lane-chunks (e.g. 4 or 8 lanes per iteration,
> matching the host's native vector width) with a small, fixed number of
> loop-carried values per iteration (accumulator/mask state only), rather than
> unrolling all `WaveSize` lanes' worth of scalar ops inline -- the only
> strategy that reduces both SelectionDAG node count *and*
> register-pressure/live-range footprint together. A materially large, invasive
> redesign (not yet started): needs careful design to avoid silently breaking
> wave-uniform control-flow/reconvergence invariants `LinearizePass` depends on
> downstream, a decision on chunk width (fixed vs. host-CPU-feature-derived),
> and its own dedicated unit/lit test coverage across multiple
> `WaveSize`/chunk-width combinations before any CTS re-verification of
> `_requiredsubgroupsize` cases or further `SHUFFLE_BIT` consideration
