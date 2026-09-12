---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work on H95a or other blocking work to make progress on the H-series
milestones?

> **The 4 remaining `*_shared_memory_size` cases' "Unexpected shared memory
> result: 0"**: confirmed (via direct instrumentation of `GroupShared` contents
> immediately after `invokeMesh()` returns) that the final
> workgroup-shared-memory state is **100% correct** by the time the shader
> finishes -- this is not a data-corruption/addressing bug. The shader's own
> internal verification -- `if (gl_LocalInvocationIndex == 0u) { for (...) if
> (mismatch) allOK = false; result.sharedOK = allOK; }` -- is computed once per
> **wave** (not once per **workgroup**) by the wrapped-entry region function
> (confirmed via a cached IR dump, `main`'s body in
> `/tmp/h94dump/module0-wrapped3.ll`, reproduced for `mesh_shared_memory_size`):
> `feme-cpu-linearize`'s divergent-loop lowering folds the invocation-0 gate
> entirely into the verify-loop's own per-lane read mask (`masked.mask =
> wave_entry_mask & live.t13.wide`), so for any wave that does *not* contain
> invocation 0, every lane's masked gather returns its passthru value
> (`zeroinitializer`), which never matches the expected nonzero constant, so
> `allOK` comes out false for that wave too -- and the final `result.sharedOK`
> store is emitted with a hard-coded `i1 true` mask (`call void
> @feme.cpu.resource.store.raw.i32(..., i32 %17, i1 true)`), i.e. **every wave
> in the workgroup's wave-loop unconditionally overwrites `result.sharedOK`**,
> not just the wave containing invocation 0. Since waves execute sequentially in
> wrapped-entry order, the *last* wave (which never contains invocation 0, and
> so always computes `allOK=false` from garbage passthru data) always wins,
> unconditionally clobbering whatever the wave containing invocation 0
> legitimately computed. Root cause is narrow and specific: whichever pass
> lowers a `for`-loop-with-early-exit nested inside a single-invocation-gated
> `if` (this is `feme-cpu-linearize`'s `loop.exit.guard` scalar-merge shape,
> `Linearize.cpp`) must gate that shape's own *final scalar side-effect store*
> by whether the enclosing `if`'s condition (`live.t13.wide`, reduced across the
> wave) held for this wave at all, not execute it unconditionally. Not yet fixed
> -- needs: (1) a `Linearize.cpp` change threading the outer gate's own reduced
> mask through to the final scalar store rather than defaulting to `i1 true`;
> (2) unit-test coverage in `LinearizeTest.cpp` with a hand-built "if (single
> lane) { loop-with-break; scalar store }" `.ll` case; (3) re-verification of
> all 4 target CTS cases; (4) a broader `dEQP-VK.mesh_shader.ext.*` sweep to
> check for regressions, since this store-masking shape is likely shared by
> other single-invocation-gated verification loops elsewhere in the CTS
> mesh/task suite.
