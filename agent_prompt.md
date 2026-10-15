---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

1. **(~2-4 hours)** The original "profile one L418 1024x1024x2
   image-to-image MSAA case" next step -- **this item is now stale**:
   `L418`/`L418(a)`/`L418(b)` are all already marked "done" in
   `Roadmap.md` from an earlier session (the profiling, the rasterizer
   fix, and the `QueueExecutor`/`DeviceMemory` race it uncovered are all
   closed, see `L421`). Drop this from future next-steps lists -- it was
   carried forward stale for several sessions.
2. **(~half day)** Root-cause the single remaining undiagnosed
   `opptrdiff.*` failure past its current signature-level bucketing:
   `wg_comparisons_diff`/`variable_pointers_vars_wg_diff` both now hit
   the same `rewriteGroupSharedGlobals` "groupshared global ... is used
   in a way this milestone does not yet canonicalize" diagnostic
   (milestone 9 scope) -- a pre-existing, correctly-functioning
   limitation, not a bug, but still worth individually root-causing (the
   `ptrtoint`-of-groupshared-global shape specifically) if milestone 9
   scope is ever revisited.
3. **(standing, unresolved many sessions)**
   `backup-before-rebase-20261014` tag decision,
   `CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decision -- neither
   revisited this session.
4. **(standing)** `L419(j)` remaining untriaged items (rasterization line
   continuity needs barrier-region splitting generalized to nested
   control flow; mandatory-feature reporting needs 14 missing feature
   bits, cross-referenced to `D1`), deferred full
   `Vulkan14FeatureInventory.md`/extension-manifest regeneration.
5. **(standing)** `K6n`-proper: the largest remaining `variablePointers`
   bucket, CPU-backend resource-handle-normalization architectural
   limitation -- still not root-caused beyond signature-level bucketing,
   unchanged from every prior session's framing. Not touched this
   session either (scope stayed on the `opptrdiff.*` CFG/SIMDize bugs).

**Immediate next action for next session:** with the diamond-into-loop-
header CFG gap and the `widenGroupSharedGEP` crash both fixed, the only
remaining `opptrdiff.*` residual is the single, already-tracked,
*correctly-functioning* `rewriteGroupSharedGlobals` diagnostic shared by
the last 2 cases (item 2 above) -- a narrow, optional root-causing task,
not a bug fix. The much bigger, still-fully-open item is `K6n`-proper
(item 5 above), unattempted for many sessions now and the largest
remaining `variablePointers` bucket; that or `check-hlsl-feme-vk`
exercise against offload-test-suite (never run yet, despite the standing
requirement) are the two strongest candidates for a change of pace.
