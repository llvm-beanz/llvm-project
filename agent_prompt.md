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

Can you work on H96 or other blocking work to make progress on the H-series
milestones?

> **A long-lived `deqp-vk` process crashes (bare `SIGSEGV`) after processing
> roughly 2,000-2,500 test cases, regardless of which case is next in the
> caselist**, discovered during this same re-triage session while diagnosing
> what first looked like a dense, shader-cache-related crash family (see the
> "Critical correction" note in `VulkanCTSReport.md`'s "Reproducing this report"
> section for the shader-cache half of that story). After ruling out `deqp-vk`'s
> own `shadercache.bin` (disabling it with `--deqp-shadercache=disable` did
> **not** stop the crash), a `dEQP-VK.pipeline.*` re-run still crashed after
> exactly 2,401 cases in one process, and -- critically -- **every
> freshly-restarted process then crashed again on its own very first case**,
> even though that exact case (confirmed via a standalone, single-case re-run in
> a byte-fresh directory) passes cleanly in isolation. This rules out both "one
> specific case is buggy" and "corrupted shared on-disk state" (no feme-specific
> on-disk cache path was found in `feme/lib/Vulkan/`), leaving **some form of
> unbounded, in-process resource growth carried across test cases within a
> single long-lived `VkInstance`/`VkDevice`** (JIT-compiled-code arena,
> pipeline-cache growth, or similar) as the most likely cause -- serious not
> only for CTS-run accuracy but for any real, long-running Vulkan application on
> this driver. Not yet triaged -- needs a memory-growth profile (e.g. `valgrind
> --tool=massif` or periodic `/proc/<pid>/status` `VmRSS` sampling) across a few
> thousand sequential `deqp-vk` cases in one process to identify which
> allocation grows unboundedly, and whether the fix belongs in the JIT engine's
> code-cache eviction, the pipeline-cache/shader-cache implementation, or
> elsewhere in `feme/lib/Vulkan/`. Until fixed, any full-suite CTS run should
> chunk each group's own caselist into fixed-size batches (roughly 1,500-2,000
> cases per `deqp-vk` invocation) from the start, rather than relying on
> crash-triggered resume loops, which degrade to one process-spawn per case once
> this threshold is crossed
