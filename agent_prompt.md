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

**When you find an issue with environment or process**: Codify it in Feme's
.instructions.md file so future sessions don't lose it.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(cheap, first thing)** Check on `/tmp/l444_spirvasm_full_rerun.qpa`/`.log`
   (still running, not restarted across two sessions now) — once done, record
   the refreshed baseline in `VulkanCTSReport.md` and `Roadmap.md`'s `L444`
   entry (expect ~249 remaining failures, pending the real measurement).
2. **(standing, needs a full dedicated session, not a half-day slice)**
   `L419(ad)` (`mixed_break_continue`, 4 cases) — root cause and both candidate
   fix designs are fully written up in `Roadmap.md`'s own entry; this needs a
   session that can give it full, undivided attention given its prior-regression
   history in this exact code area.
3. **(standing)** `L419(y)` (`conditional_break`, 6 cases) and `L419(g)` (30
   tessellation residuals) — both still completely untouched.
4. **(half day)** `check-hlsl-clang-feme-vk`'s 39-case residual triage
   (`L440(c)`) — still deferred every session.
5. **(worth filing, low priority, carried forward)** `feme-run` cannot currently
   execute a DXIL shader using a resource (not groupshared)
   `InterlockedAdd`/atomic. Not investigated further.
6. **(L444's own remaining clusters)** With `opquantize` and `opptraccesschain`
   both now fixed, the next-largest remaining clusters in the original 275-case
   baseline (per this session's own cluster breakdown) are `frexpstruct` (12
   cases), and several ~8-case struct/array composite-decomposition clusters
   (`f16arr3`, `struct16arr3`, `v3f16`, `v3f16arr5`, `v4f16arr3`, `struct`,
   `switch`) — worth re-clustering against the fresh completed sweep (item 1)
   before picking the next target, since some of these may already be fixed as
   side effects of the two fixes landed this session (unlikely, but cheap to
   confirm once the real data is in).
