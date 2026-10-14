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

1. **(~1 day) Start `L419(e)`**: all-format color blits, 20 cases
   (`api.copy_and_blit.core.blit_image.all_formats.color`, 70,140 total cases
   upstream -- no cached failure log exists, so first step is a targeted
   format-pair sweep or filter to isolate the exact 20, not a blind full rerun).
2. **(~2-4 hours) `L419(j)`**: small long tail, 7 cases -- not yet triaged,
   good second pick after `L419(e)` since both were deferred pending `L419(k)`'s
   close, which is now done.
3. **(~2-4 hours) `L418(a)`**: CPU rasterizer `Draw`-path MSAA optimization --
   still flagged "done" by the last two sessions' handoffs; worth one quick
   `git log --oneline --all | grep -i L418` sanity check before trusting that,
   per a prior session's own caught-in-the-act warning about memory drift on
   this exact item, but not re-opening work without that check first.
4. **(standing)** Keep `VulkanCTSReport.md` + both inventories + `L418`-`L419`
   roadmap counts synced after every fix -- done this session for both halves
   of `L419(k)`.
5. **(standing)** Keep checking the `VkBool32`/`WaveActiveMax`
   `offload-test-suite` pattern before calling any `check-hlsl-feme-vk`
   discrepancy a new regression -- not exercised this session, no
   `check-hlsl-feme-vk` work was done.
6. **(unresolved, carried for several sessions)** Decide whether to revert
   `CMAKE_BUILD_TYPE` from `RelWithDebInfo` back to `Release` -- still not
   revisited, no new information this session either way.
