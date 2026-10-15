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

**Next steps, ranked:**
1. **(~1-2 days)** Fix `K6z`: generalize `validate`/`flatten`'s
   nested-cycle-boundary handling to tolerate an already-linearized
   child cycle anywhere in a uniform loop's body, not just at the exit
   check. Highest-value pick available: closes 24 of 26 current
   `check-hlsl-feme-vk` failures in one shot. Start from
   `matrix_single_subscript_load.test`'s `FEME_DUMP_IR_PRELINEARIZE`
   dump (already captured this session, see this session's own
   `K6z` write-up for the exact block numbers) as the unit-test seed
   shape.
2. **(~half day)** Root-cause `InterlockedAdd.resources.32.test`'s
   numeric mismatch (expected all `0x1`, actual all `0x0`) -- not yet
   looked at past the raw buffer diff.
3. **(~1-2 days)** `K6n`-proper: the largest remaining `variablePointers`
   bucket, still not touched for many sessions.
4. **(standing, unresolved many sessions)**
   `backup-before-rebase-20261014` tag decision,
   `CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decision.
5. **(standing)** `L419(j)` remaining untriaged items, deferred full
   `Vulkan14FeatureInventory.md`/extension-manifest regeneration.

**Immediate next action for next session:** pick `K6z` (item 1) --
highest leverage available, already root-caused down to the exact code
path and a concrete IR repro shape.
