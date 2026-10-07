---
model: claude-sonnet-5
resume: db99d377-bdef-4dd8-a2b5-787bcf66bc23
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

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **~1 day, dedicated design session, still the right scope**: `L383(a)`
   (`graphicsfuzz.nested-for-loops-switch-fallthrough`). Confirmed this session
   (again) to be a genuinely different mechanism from `L375`'s now-fixed
   cross-instance collision — an edge is silently stolen via a `RedirectTo`
   rewrite from a block that is never itself revisited as `Cur`, so no amount of
   `MergeVisited`-style visited-tracking at the `Cur`-visitation level can catch
   it. Needs its own root-cause trace starting from the `RedirectTo` call sites
   in `DiamondFlattener::flatten`/`validate`, looking for where a predecessor
   edge gets reassigned without any accompanying visited-set update.
2. **~1 day, needs a reproducer that preserves full-sweep ordering, not a single
   isolated case** (carried over, untouched this session): the 8
   `robustness.index_access.draw_indexed*pipeline_robustness_1*` `DeviceLost`
   cases. Case names are recorded (item 25 in `VulkanCTSReport.md`). Isolated
   re-runs don't reproduce — start by running the full `index_access.*`
   sub-family (not all of `robustness.*`) to narrow how many preceding cases are
   needed to trigger it, the bisection approach that worked for `L392`.
3. **~30 min, carried over, untouched this session**: triage the 2
   `non_robust_buffer_access.unexecuted_oob_{overflow,underflow}` Fails — never
   investigated, no hypothesis yet.
4. **Still infeasible, don't attempt a full sweep, carried over**: `pipeline`'s
   own `Crash=46` figure remains unconfirmed (~1.17-1.64M cases). Item 9's own
   53-case targeted re-run (3 crashes, all `L375`, now fixed) remains the best
   evidence it's stale. If a future session wants to move this forward, extend
   item 9's targeted-case-name technique rather than a full sweep.
5. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache-persistence item: already
   resolved/documented, no action needed.
