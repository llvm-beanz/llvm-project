---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
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

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(5 min)** Check whether `input_output_float_32_to_16`'s `_rtz` cases
   are still failing at all, now that the full 48,307-case list shows
   only 2 failures -- if they're gone, cross this off permanently instead
   of carrying it forward again.
2. **(15-30 min)** Triage the `check-hlsl-feme-vk` findings above: run
   each of the 3 failing tests individually with full output, and check
   whether `array_of_matrices.test`'s `XFAIL` for `Clang`/`DXC` was ever
   meant to include FeMe, or if this is a genuine new pass worth
   registering as expected (removing the false-XFAIL) or worth double-
   checking as a coincidental symptom of something else.
3. Given the 48,307-case list is now down to 2 (both permanent, by
   design), **the next full/broad CTS re-run has no urgent trigger
   anymore** -- the remaining work is entirely in the groups this
   baseline never covered in the first place (L201(b)/(c)/(d), and
   whatever `check-hlsl-feme-vk` turns up). Consider whether a *fresh*
   full-suite sweep (not just the old verified-failure list) is now
   worth doing from scratch, since the old list's own scope was fixed
   as of 2026-09-26 and may not reflect cases that have started
   NotSupported->Fail or similar drift.
4. L201(d) (mesh/tessellation f16 I/O, 120 cases) is still the largest
   untouched group with a real, distinct symptom already known
   ("Result does not match reference," fail location
   `vktMeshShaderInOutTestsEXT.cpp:1590` from 2 sessions ago) -- highest
   remaining case count of any carried-over item.
5. `stash@{0}`/`stash@{1}`: inspect (`git stash show -p stash@{N}`) and
   either finish or `git stash drop` explicitly, rather than carrying
   forward a 5th/6th time.
