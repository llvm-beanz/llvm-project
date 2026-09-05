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

Can you work on L41 or other prerequisites blocking the L-series milestones?

> **A real `dEQP-VK.mesh_shader.ext.*` sweep, run while validating L39's own
> fix, incidentally found an unrelated, pre-existing JIT-link crash**:
> `dEQP-VK.mesh_shader.ext.query.no_queries.lines.no_reset.copy.no_wait.draw.32bit.no_availability.multiple_blocks.mesh_only.inside_rp.single_view.only_primary`
> (and likely other `mesh_only` cases in the same `query.*` group) aborts the
> whole `deqp-vk` process with `"deqp-vk:
> llvm/include/llvm/ExecutionEngine/JITLink/JITLink.h:285: void
> llvm::jitlink::Block::setMutableContent(MutableArrayRef<char>): Assertion
> `MutableContent.data() && \"Setting null content\"' failed."` -- confirmed
> reproducible in isolation (a single-case run hits it deterministically, not
> merely a byproduct of the broader sweep) and confirmed unrelated to L39's own
> task-payload fixes by reverting
> `CanonicalizeStage.cpp`/`TaskPayloadWrapper.cpp` to their pre-L39 state and
> reproducing the identical assertion (unsurprising, since this case's own
> shader has no task/amplification stage or payload access at all -- `mesh_only`
> in its own case name). A JIT-link crash rather than a diagnosed failure or
> ordinary rendering mismatch suggests a genuinely different class of bug than
> this project's usual legalization gaps -- likely a real object emitted with a
> null/empty section JITLink chokes on, possibly specific to this query-tests
> group's own pipeline statistics/timestamp query instrumentation around a
> mesh-only dispatch, or an interaction between mesh shading and
> `VK_EXT_mesh_shader`'s own draw-count/query machinery -- needs its own scoping
> pass (confirm how many `query.*.mesh_only.*` cases are affected, and whether a
> real IR/JIT reduction of this exact case narrows it to a specific
> instrumentation feature) before a fix can be designed
