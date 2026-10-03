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

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session's suggested
next steps are:

1. **(systematic grep, ~1 hour, STILL not finished)** Handoff item 3's
   audit is still only half-done: only `Event` has been checked beyond
   the already-known-fine `Fence`/`Semaphore`/`QueryPool`. Still
   unchecked: anything touching `vkCmdCopyQueryPoolResults`'s buffer-write
   path for completeness (explicitly called out in the original handoff
   and still never looked at), plus any other object mutated by a
   `QueueExecutor` worker and read by the host thread -- `Image`/`Buffer`
   host-visible-memory bookkeeping fields, `Framebuffer`, anything in
   `DescriptorSet.{h,cpp}` that caches state. Worth a dedicated pass
   grepping for every class in `feme/lib/Vulkan/*.h` that's both
   `Alloc.create`'d (heap, pointer-stable) and referenced from
   `CommandBuffer.cpp`'s deferred-execution path.
2. **(worth investigating once, low cost)** Check whether ThreadSanitizer
   can be wired into a `check-feme`-adjacent build config. If `Event`
   hid for this long, there may be more of these, and a poll-based unit
   test alone can't prove a fix's correctness the way TSan actually can.
   Even a one-off manual TSan build+run of `FeMeVulkanTests` would add
   real confidence beyond what's been done so far.
3. **(a few hours)** Continue tessellation/broader triage:
   `user_defined_io` (27 cases), `device_group` (7),
   `geometry.input.triangle_strip_adjacency` (6), `memory_model.*` races
   (~24) -- all still untouched, carried over many sessions now.
4. **(dedicated session, carried over many sessions, unchanged)** `L344`
   item 2 / `L335` -- N-barrier generalization, 1 case
   (`shader_input_output.barrier`). Fully scoped in
   `FeMeGraphicsDesign.md`; still needs the actual implementation
   session.
5. **(lowest priority, many sessions carried over, unchanged)** `L265` --
   ASTC alpha-decode tie-break, 12 cases. Next angle, still unattempted:
   compare decoded 4-texel neighborhoods pixel-by-pixel between
   `astc_5x5`/`astc_8x8` for a structural property correlating with tie
   direction.
6. **(process note)** `vulkaninfo --summary` without `VK_ICD_FILENAMES`
   silently reports Mesa `llvmpipe`, not FeMe -- always set the env var
   explicitly before the mandatory per-session device check, or the
   check can pass against the wrong driver without any error.
