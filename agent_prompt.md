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

1. **~1 day, needs a reproducer preserving full-sweep ordering, carried over
   untouched**: 8 `robustness.index_access.*` cases — wait, these were already
   fixed by `L396` last session. Re-checking the actual remaining open items:
   there is currently **no known open bug** in `feme/`. All previously-tracked
   crash/DeviceLost/Fail items (`L375`, `L383(a)`, `L384`, `L385`, `L394`,
   `L395`, `L396`, `L397`) are closed.
2. **The one real standing task**: a fresh full-CTS sweep has not been run since
   `feme-20261005-full-1170553-complete` (this session only re-ran the 53
   previously-flagged cases, not the full ~1.17-1.64M case space). Running a new
   full sweep is the only way to surface the next batch of untriaged failures —
   budget several hours, run in background, and bisect only the sub-families
   with new Fails/Crashes.
3. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache item: already resolved, no action
   needed.
4. **Process note for future sessions**: treat handoff lists as a *hypothesis*,
   not fact — this is now the 2nd-3rd session in a row that found a
   stale/incorrect claim in its own handoff (or in the immediately preceding
   session's own edit). Always re-verify via `git log --oneline --all | grep
   <L-number>` and a direct CTS rerun before starting "design session" work.
