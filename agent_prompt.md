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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L43 or other prerequisites blocking the L-series milestones?

> **L41's own fix correctly surfaces a real CTS mesh shader's atomic "allocate a
> unique output slot" branch as divergent for the first time, but
> `vkCreateGraphicsPipelines` still fails at `feme-cpu-simdize` instead of
> `feme-cpu-linearize` ever converting it**:
> `dEQP-VK.mesh_shader.ext.query.no_queries.*.mesh_only.*` (both of the 2 real,
> feature-supported cases in the 12,340-case `query.*.mesh_only.*` sweep L41
> ran) now fail cleanly (no more JIT-link crash) with `"error: feme-cpu-simdize:
> function 'main' has a divergent branch; the divergence transform
> (feme::cpu::LinearizePass) did not remove it, or produced a shape this pass
> cannot widen"` at `vkCreateGraphicsPipelines`. The branch in question guards
> this shader's own per-lane use of a `feme.cpu.masked.atomicrmw.*` result
> (`icmp ult i32 %old, 32` deciding whether *this* lane is one of the first `N`
> to get an output slot, per L41's own root-cause) -- before L41's fix,
> `feme::cpu::WaveTTIImpl` wrongly classified this branch uniform, so
> `feme::cpu::LinearizePass`'s own `DiamondFlattener`/`LoopLinearizer` never
> even attempted to linearize it (nothing to do for a "uniform" branch); now
> that it is correctly seen as divergent by both passes, `LinearizePass` still
> does not turn it into real masked, straight-line code the way it already does
> for an ordinary divergent `if`, since a branch depending on a
> masked-atomicrmw's own per-lane result is a shape neither `DiamondFlattener`
> nor `LoopLinearizer` has an existing case for (this branch's own two arms do
> not themselves contain another masked atomicrmw, a stage-IO store or any other
> op `DiamondFlattener`'s existing masking rules were written against). Needs
> its own real IR reduction of this exact case's pre-`LinearizePass` IR
> (captured once already via the env-gated `FEME_DEBUG_DUMP_PIPELINE_STAGE_IR`
> technique documented in L41 and L42's own rows,
> `feme/lib/Target/CPU/Pipeline.cpp`, but not committed anywhere) to design a
> fix: most likely a new `DiamondFlattener` case recognizing a branch whose
> condition depends on (transitively) a `feme.cpu.masked.atomicrmw.*` call as an
> ordinary divergent `if` to mask-flatten, mirroring how it already handles
> other divergent conditions, rather than something specific to the atomicrmw
> call itself
