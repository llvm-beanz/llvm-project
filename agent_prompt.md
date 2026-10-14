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

1. **(~1 day+, bigger than previously estimated)** Scope and implement
   general nested-control-flow support in `EntryWrapper.cpp`'s barrier
   region splitter, OR find a narrower special case that covers this
   specific loop-with-nested-conditional-barriers shape without a full
   generalization. Repro is pinned: `-n="dEQP-VK.rasterization.line_continuity.*"`.
   Read `EntryWrapper.cpp`'s file-header comment and `isLinearChain` first.
2. **(~1-2 hours each)** `L419(j)`'s last 2 untouched singletons:
   `primitive_id.matching` and `device_mandatory_features` — still
   completely untriaged, no repro run yet this session.
3. **(standing)** Doc sync — done this session for both wins above.
4. **(standing)** offload-test-suite `VkBool32`/`WaveActiveMax` pattern
   check — not exercised, no `check-hlsl-feme-vk` work done.
5. **(unresolved, carried many sessions)** `CMAKE_BUILD_TYPE`
   `RelWithDebInfo` vs `Release` — still not revisited.

**Immediate next action for next session:** run
`-n="dEQP-VK.rasterization.geometry.*primitive_id*matching*"` to get the
exact failing case name and error for the `primitive_id` singleton —
it's untriaged and smaller than the line_continuity generalization.
