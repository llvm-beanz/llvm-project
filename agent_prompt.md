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

1. **(~5 min)** Check `part1` (PID 7228, 377,991+ cases, no crash) again
   next session. It is the longest-running unresolved lead in this
   investigation — either it finally crashes with a real stack trace, or
   it keeps running and becomes less and less likely to ever reproduce.
2. **(~1 day, dedicated session)** Since memory leaks are now ruled out,
   pivot the stability investigation to **resource-table/descriptor-count
   exhaustion**: look for any fixed-size handle table, descriptor pool, or
   similar bounded structure in FeMe's object-tracking code
   (`Objects.h`/`Device.h`/descriptor-pool allocation path) that could run
   out or wrap around after hundreds of thousands of distinct
   create/destroy cycles, producing `DEVICE_LOST` or a null-deref
   `SIGSEGV` instead of a clean `VK_ERROR_*` return. This is the next-most-
   likely mechanism now that the obvious memory-growth hypothesis is shut.
3. **(~30 min)** `part2`'s own caselist run (`pipeline.{fast_linked_library,
   monolithic}.{stencil,blend.dual_source}.*` `DeviceLost`) — re-check its
   current progress/case count; if it has finished, grab the final
   DeviceLost percentage for comparison against earlier mid-run snapshots.
4. **(~1 day)** If resource-table exhaustion doesn't pan out either:
   consider that `DeviceLost`/`SIGSEGV` might be a genuine logic bug in a
   specific high-combinatorial-state test family (stencil/dual-source
   blend state permutations) rather than a resource-accounting problem at
   all — would need case-by-case narrowing within `part2`'s caselist,
   similar to the narrowing approach that worked for `L402`/`L406`.
