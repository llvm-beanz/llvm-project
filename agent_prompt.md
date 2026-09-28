---
model: claude-sonnet-5
resume: 1ebfe39e-5f48-4c14-810b-08a0301888a7
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

1. **(a few hours, new roadmap item, ready to pick up)** `L249`:
   `dEQP-VK.pipeline.fast_linked_library.depth.depth_only.
   subpasses_postpass` genuine `DeviceLost`. Start with
   `FEME_VULKAN_LOG_CREATION_ERRORS=1` or a debug build to find where
   execution actually stalls/faults before the fence-wait timeout --
   this is a hang/crash, not a wrong-value bug, so standard qpa-level
   diffing won't help; needs a runtime-level trace.
2. **(a few hours, still carried over, unchanged for many sessions)**
   Pick one of `L227(d)` (3 `Graphics/MeshShaders/*`
   image-comparison failures) or `L228(b)`/`(c)` (compressed-format
   blits, MSAA multi-layer clears).
3. **(a few hours, partially addressed this session, still open)**
   `L228(e)`/`(f)`: this session's own 1-in-200 `pipeline.*` sample
   (~5797 cases) is a real start, but `shader_render` and most of
   `pipeline`'s other sub-suites (only `depth.*` was deep-dived) remain
   unsampled at any real scale -- worth a dedicated wider pass.
4. **(worth 15 minutes, new roadmap item, carried over)** `L245`:
   pre-existing 65-case `sampleCounts` mismatch bucket in
   `api.info.image_format_properties*`. Filed several sessions ago,
   still untouched.
5. **(a few hours, new roadmap item, carried over)** `L246`:
   `dEQP-VK.image.mutable.{2d,2d_array}.*` (72 cases)
   `vkCreateFramebuffer` gap. Filed several sessions ago, still
   untouched.
