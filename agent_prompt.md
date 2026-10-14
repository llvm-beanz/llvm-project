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

1. **(~2-4 hours) `L419(j)`**: small long tail, 7 cases -- still
   untriaged across many sessions now. Mixed bag per `Roadmap.md`: own
   conformance-version reporting (won't-fix per `L412(g)`), draw output
   location (2), rasterization line continuity (2), geometry primitive ID
   (1), mandatory-feature reporting (1). Don't lump these together --
   triage each signature separately, starting with whichever has the most
   cases (draw output location or rasterization line continuity, 2 each).
2. **(~2-4 hours) `L418(a)` sanity check**: still flagged "done" by
   several sessions' handoffs now, several sessions in a row without
   re-verifying. Run `git log --oneline --all | grep -i L418` once before
   ever touching it again -- still not run this session either (not
   touched, no reason to).
3. **(standing)** Keep `VulkanCTSReport.md` + both inventories +
   `L418`-`L419` roadmap counts synced after every fix -- done this
   session (`L419(e)`/`L265`/`L424`/`L424(a)`/`L424(b)` all struck
   through; inventories confirmed via grep to need no change).
4. **(standing)** Keep checking the `VkBool32`/`WaveActiveMax`
   `offload-test-suite` pattern before calling any `check-hlsl-feme-vk`
   discrepancy a new regression -- not exercised this session, no
   `check-hlsl-feme-vk` work done.
5. **(unresolved, carried for many sessions)** Decide whether to revert
   `CMAKE_BUILD_TYPE` from `RelWithDebInfo` back to `Release` -- still not
   revisited, no new information this session either way.
