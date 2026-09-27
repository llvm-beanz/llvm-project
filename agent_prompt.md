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

1. **(a full dedicated session, likely the biggest single item left)** Design
   and implement async `vkQueueSubmit` for `L228(h)`/`L228(i)`: write a short
   design note in `FeMeVulkanDesign.md` proposing a worker-thread-per-`VkQueue`
   (or shared pool) model first, then implement it in `Sync.cpp` so
   `vkQueueSubmit`/`vkQueueSubmit2` return immediately without blocking on an
   unmet wait, with `vkQueueWaitIdle`/`vkDeviceWaitIdle`/fence-wait blocking on
   that queued work instead. Confirm whether `Sync.h`'s existing `Semaphore`
   mutex/condvar (from `L228(a)`) is enough for the needed cross-thread state,
   and think carefully about whether the CPU executor's image/buffer/pipeline
   state is safe to touch from a background thread concurrently with further API
   calls on the caller's own thread.
2. **(a few hours, dedicated session)** Pick one of `L227(a)`-`(d)` (still open,
   unchanged since last session) or `L228(b)`/`(c)` (compressed-format blits,
   MSAA multi-layer clears) -- whichever fits the next session's time budget.
3. **(a few hours)** `L228(e)`/`L228(f)`: the broader-than-tessellation CTS
   re-run (`api`/`pipeline`/`shader_render`/`synchronization`) is still overdue
   -- this session's own work stayed scoped to the `timeline_semaphore` cluster
   only.
4. **(quick, at the very start of the next session)** Re-check
   `offload-test-suite`'s local `feme` branch before trusting
   `check-hlsl-feme-vk` exists -- `git log --oneline feme -3` should show
   `adf0fc1` at the tip; if it's back at `main`'s own tip, `git reset --hard
   adf0fc1` restores it (no re-cherry-pick needed, the commit is still there).
