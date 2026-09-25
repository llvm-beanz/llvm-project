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

1. **(large, the actual next step for L188 itself, unchanged from the
   last two sessions)** Attempt `run()`'s own traversal-order change:
   make it a genuine post-order traversal (all descendants fully
   processed before their parent is attempted) instead of permanently
   skipping any cycle with children. Before writing that change,
   separately investigate whether a child cycle's own block deletions
   (`foldRedundantFlowBlocksInCycle`/`mergeTrivialRelayBlocksInCycle`)
   can invalidate a not-yet-processed *parent* cycle's own
   `CI.getHeader`/`getExitBlocks`/`contains` results -- still an
   entirely open safety question no session has investigated yet.
2. **Scan `Roadmap.md` fresh** if not picking up 1 above -- the
   long-stale candidate list (`L116(b)`/`L116(f)`, `L126(a)`, `L147`,
   `L98(b)`, assorted `R`/`V`/`W`-prefixed rows) is still individually
   unvetted after many sessions of deferral. A future session should do
   a real full-table pass rather than keep punting on this same list.
3. **(~5 min)** No `/tmp` scratch remains from this session -- all
   `/tmp/l196b_*`/`/tmp/ctsrun_l196b/`/`/tmp/Linearize_*.cpp` scratch
   (extracted SPIR-V, intermediate `.ll` dumps, both temporary `feme-opt`
   binaries, `llvm-reduce` interestingness scripts, one CTS `.qpa` log)
   removed.

Next step if resuming: item 1 (`run()`'s traversal-order change) is the
real payoff the last three sessions' work (L188's root cause, L196's
mechanism, this session's test coverage) has been building toward --
pick it up next.
