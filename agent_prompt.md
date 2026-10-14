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

1. **(~half day) Root-cause the remaining 12 `L419(e)` cases**: the
   `astc_{8x8,10x5,12x12}_unorm_block` -> `a2b10g10r10_snorm_pack32`
   group. Known repro:
   `astc_10x5_unorm_block.a2b10g10r10_snorm_pack32.general_general_linear`,
   shows a full-scale (`1.0`) alpha diff with RGB within threshold -- a
   completely different signature from the `inf`/minifloat bug just
   fixed (different destination format, no `inf` involved, only the
   alpha channel affected). First step: check `packClearColor`'s/
   `unpackColor`'s `R10G10B10A2_SNORM` alpha-component handling
   specifically (4th component is only 2 bits wide, a much coarser
   field than RGB's 10 bits each -- a likely place for a sign/clamp
   edge case to live), and whether ASTC's own alpha-channel decode path
   (`decodeASTCBlock`) is a plausible second suspect.
2. **(~2-4 hours) `L419(j)`**: small long tail, 7 cases -- still
   untriaged, deferred again this session in favor of finishing
   `L419(e)`'s in-progress investigation instead of starting fresh work.
3. **(~2-4 hours) `L418(a)` sanity check**: still flagged "done" by
   several sessions' handoffs now; the standing self-correction reminder
   (`git log --oneline --all | grep -i L418`) was not run again this
   session since no `L418` work was touched -- still worth doing before
   ever touching `L418(a)` again, but not urgent while it stays untouched.
4. **(standing)** Keep `VulkanCTSReport.md` + both inventories +
   `L418`-`L419` roadmap counts synced after every fix -- done this
   session for `L419(e)`'s partial close.
5. **(standing)** Keep checking the `VkBool32`/`WaveActiveMax`
   `offload-test-suite` pattern before calling any `check-hlsl-feme-vk`
   discrepancy a new regression -- not exercised this session.
6. **(unresolved, carried for several sessions)** Decide whether to
   revert `CMAKE_BUILD_TYPE` from `RelWithDebInfo` back to `Release` --
   still not revisited.

**Next concrete action**: open `feme/lib/Graphics/ImageFixture.cpp` and
grep for `R10G10B10A2_SNORM` to find the alpha-component pack/unpack
case, then rerun the standalone `astc_10x5_unorm_block.
a2b10g10r10_snorm_pack32.general_general_linear` case with a targeted
debug print on the alpha channel specifically.
