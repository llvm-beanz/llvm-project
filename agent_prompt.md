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

Can you work on L89a from the roadmap or other prerequisites blocking the
L-series milestones?

> **`SIMDizePass` needs a bounded-basic-block (e.g. lane-chunked or loop-based)
> codegen strategy for wide required subgroup sizes**, split out of L89's own
> closing session: L89's own live-process `gdb` progress-sampling plus a
> run-to-completion confirmed the
> `PostMachineSchedulerLegacy`/`ScheduleDAGInstrs` behavior once filed as an
> "infinite hang" is actually a real, severe, but finite (~210 seconds for one
> pipeline, confirmed via letting a real `deqp-vk` re-run of
> `dEQP-VK.subgroups.shuffle.compute.subgroupclusteredrotate_float_dynamically_uniform_requiredsubgroupsize`
> complete with no timeout: it passes) compile-time blowup, root-caused to
> `SIMDizePass`'s intentional, documented per-lane scalarization strategy (`W`
> unrolled scalar clones of each divergent op, all inline in one basic block)
> producing a single ~2200-instruction basic block at `WaveSize=64` (vs. ~140 at
> the host-derived default `WaveSize=4`, a matching ~16x scaling) that hits
> LLVM's legacy `ScheduleDAGRRList`/`BURRSort`/`ComputeHeight`-based list
> scheduler's well-known poor scaling on basic blocks with thousands of
> `SUnit`s. Not yet fixed: needs a `feme`-side redesign of how `SIMDizePass`
> emits a wide wave's per-lane scalar work (e.g. processing lanes in
> native-host-width chunks inside a real loop, or otherwise splitting the
> unrolled work across multiple basic blocks) so that a scheduling region's own
> size stays roughly constant regardless of the shader's declared/required
> subgroup size, rather than scaling linearly with it -- a materially larger,
> riskier change than a typical `feme`-side legalization-pattern fix, touching
> the core lane-processing shape `SIMDize.cpp`'s entire 3714-line file is built
> around, needing careful design (a wrong chunking strategy could silently break
> wave-uniform control-flow/reconvergence assumptions `LinearizePass` depends on
> downstream) plus its own dedicated unit/lit test coverage across multiple
> `WaveSize`s before any CTS re-verification
