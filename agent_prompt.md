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

1. **(~half day)** Reduce one `winding_*` case (new `L419(n)`, 43/slice, the
   largest remaining sub-cluster) — "Vertex data verification failed" at
   `vktTransformFeedbackSimpleTests.cpp:1749`, a per-primitive
   vertex-rotation/winding-order check, not yet investigated at all. Likely a
   FeMe rasterizer primitive-decomposition bug, distinct from the counter-buffer
   bug just fixed.
2. **(~half day)** Triage `holes_*` (4/slice) and
   `multi_stream_single_raster_non_zero` (1/slice) — much smaller, not yet
   looked at; could plausibly share `winding_*`'s root cause or be separate
   again.
3. **(~2-4 hours)** `L418(a)`: optimize the CPU rasterizer's `Draw` path for
   large MSAA targets (91% of the profiled 4.55s case) — use `gdb` to find the
   hot loop, no sampling profiler available in this environment.
4. **(~half day)** `L418(b)`: reconcile the ~5s standalone `L418` timing with
   60s/600s full-suite timeout reports — likely `QueueExecutor` thread
   contention, needs a `run_vulkan_cts.py`-driven contention test to confirm.
5. **(standing)** Keep `VulkanCTSReport.md`, both Vulkan inventories, and the
   `L418`-`L419` roadmap counts synchronized after every fix and isolated CTS
   rerun.
6. **(standing)** Keep checking the `VkBool32`/`WaveActiveMax`
   offload-test-suite pattern before calling anything a new regression — not
   exercised this session, still applies whenever `check-hlsl-feme-vk` work
   resumes.
7. **(standing)** Grep `Roadmap.md` for a candidate roadmap ID before using it
   anywhere.
8. **(standing, new this session)** Before trusting a carried-forward "still
   unstarted" claim across multiple sessions, check `git log --oneline --all |
   grep -i <ID>` and the roadmap row itself directly — this is the second time
   handoff notes have silently gone stale across a compaction boundary (first
   was the `L146` ID-collision near-miss).
