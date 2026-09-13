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

Can you work on H101n or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.random_geometry.nested_structs_instance_arrays.45`'s
> `corrupted double-linked list` heap-corruption crash when run immediately
> after case `.44`'s own (pre-existing, unrelated) `Mismatch` fail** (discovered
> during H101l's own closing regression sweep; confirmed, via `git stash`
> bisection against the pre-H101l binary, to be entirely pre-existing and
> unaffected by that row's `D.XfbOffset` fold-in fix): reproducibly crashes
> glibc's malloc consistency check (`SIGABRT`) when cases `.44` and `.45` (both
> `random_geometry` and `random_vertex` variants of
> `nested_structs_instance_arrays`) run back-to-back in the same `deqp-vk`
> process, but case `.45` does **not** crash when run in isolation -- it instead
> fails cleanly with the pre-existing, already-cataloged `si32`/`i32`
> legalization error (part of H101m's own 28-case bucket), suggesting the
> corruption is a heap-metadata side effect of some earlier case's (likely
> `.44`'s own) allocation/deallocation pattern that a later case's own
> allocation then trips over, rather than a bug in case `.45` itself.
> Non-deterministic across repeated identical runs at different points in this
> project's history (did not manifest during H101k's own closing full-790-case
> sweep, despite an identical code path), consistent with a classic
> heap-layout-randomization-sensitive use-after-free or double-free rather than
> a deterministic logic bug. Not yet triaged -- needs a smaller, faster repro
> than the full sweep (the `.44`+`.45` pair alone already reproduces it
> deterministically in this session, a useful starting point) bisected further
> with a memory-error detector (e.g. ASan or valgrind, if available in this
> environment) to identify the actual out-of-bounds write or double-free, likely
> in `Executor.cpp`'s per-case JIT/pipeline teardown or
> `CanonicalizeStage.cpp`'s own per-module allocation of scratch state, given
> neither case's own shape is otherwise anything unusual for this test family
