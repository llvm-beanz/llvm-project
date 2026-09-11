---
model: claude-sonnet-5
resume: 0a6535de-11d2-4cb7-8770-7e69bf31da83
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
agent_thoughts.md file.

# Request

Can you work on H89b or other blocking work to make progress on the H-series
milestones?

> **Fix the latent `feme::cpu::LoopLinearizer` runtime hang H89a's own
> poison-operand fix unmasks**: once H89a stops misclassifying the outer loop's
> trip counter as divergent, `LoopLinearizer` still (correctly) finds a
> genuinely divergent exit signal threaded through the *same* reconvergence
> block (the inner per-lane bounds check's own early-exit arm, merged with the
> outer loop's own "done after N iterations" arm at one shared block) and
> applies its masked-loop transform -- but the compiled shader then spins
> forever at runtime (confirmed via `gdb -p <pid> -batch -ex bt` stuck inside
> the compiled shader body, not the compiler) rather than terminating, for a
> case (`max_mesh_output_vertices_256`) whose real per-lane bounds check is
> always true at runtime (the loop's two sources of "should I stop" -- the outer
> uniform trip count and the inner per-lane index bound -- coincide exactly for
> every tested output-array size, so this is not merely a rare edge case). Needs
> its own dedicated IR-level investigation (a fresh pre-/post-`LoopLinearizer`
> IR diff on the reduced repro, independent of H89a's own poison-operand fix, to
> isolate whether the masked "active" phi threading double-counts/never-clears a
> lane once both the outer and inner exit signals can fire from the same merge
> point, or whether `closeLatch`'s own backedge-condition construction has a
> similar unhandled shape) before H89a's own fix can safely land; until then,
> `feme-cpu-simdize`'s current compile-time rejection is the correct, safer
> behavior to keep and this row should stay open
