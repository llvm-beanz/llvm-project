---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file, and the environment-wide
agent skills at /home/dev/.agents/skills.

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

**Before doing anything else**: `vulkaninfo --summary | grep deviceName` and
confirm `FeMe CPU Vulkan Device`. Every session from now on, every time, not
just once at the start.

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you please work on the FeMe ICD implementation? The previous session gave
the next steps:

1. **(~1-2 days, not scoped in detail yet)** The residual pixel-value
   `Fail` `complex-nested-loops-and-call` now hits (expected red, got
   black) instead of crashing. Likely a genuinely separate,
   pre-existing correctness bug in the same shader -- worth its own
   fresh QPA-log/`feme-translate` trace from scratch, not an extension
   of this session's fix.
2. **(~1-2 hrs each)** Finish the partial `graphicsfuzz.*` sweep this
   session started (426/757 done, in `/tmp` no longer -- rerun from
   scratch with a crash-tolerant per-batch driver, e.g. re-derive from
   this session's own throwaway Python driver if useful as a starting
   point) to get a full before/after comparison, then investigate the 2
   pre-existing process-crashing cases found
   (`cov-function-multiple-loops-compare-integer-return`'s "Uses remain
   when a value is destroyed!", `cov-function-loops-vector-mul-matrix-
   never-executed`'s divergent-branch `feme-cpu-simdize` gap) as their
   own rows if picked up.
3. **(large, not yet re-scoped in detail, deferred from the L188/L189
   session)** Design "provably uniform by construction" value tracking
   for `LoopLinearizer` -- the real prerequisite for nested-cycle
   support (`L188`'s own still-open root cause). Do this design in
   isolation before touching `LoopLinearizer::run()`'s traversal order.
4. **(2-4 hrs, one-time setup, deferred many sessions now)**
   `offload-test-suite`'s `check-hlsl-feme-vk` still has no build
   directory at `/home/dev/dev/offload-test-suite/build`.
5. **Scan `Roadmap.md` fresh** if not picking up 1-4 above -- the long-
   stale candidate list (`L90`-`L95` [now closed, drop from this list],
   `L116(b)`/`L116(f)`, `L126(a)`, `L147`, `L98(b)`, assorted
   `R`/`V`/`W`-prefixed rows) is still individually unvetted; a future
   session should do a real full-table pass rather than keep deferring
   to this same list.
6. **(~5 min)** No `/tmp` scratch left from this session -- all `l190_*`
   QPA logs, IR dumps, and caselist files already removed.
