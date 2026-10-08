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

1. **(~1 day, dedicated session)** `L399`:
   `dEQP-VK.renderpasses.{renderpass1,renderpass2}.suballocation.{unused_clear_attachments,multisample_resolve}`
   DeviceLost, 192 cases. Repro:
   ```
   VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build/tools/feme/tools/feme-vulkan/feme_icd.json \
     /home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk \
     --deqp-case=dEQP-VK.renderpasses.renderpass1.suballocation.unused_clear_attachments.colorunused_colorunused_colorunused_colorunused_depthonly_d32_unused \
     --deqp-log-filename=/tmp/repro.qpa
   ```
   Start by tracing `vkCmdBeginRenderPass`/subpass-attachment-reference handling
   in `feme/lib/Vulkan/CommandBuffer.cpp` and `feme/lib/Graphics/Executor.cpp`
   for how "unused" attachments (present in the render pass, not referenced by
   the active subpass) are resolved — both `renderpass1` and `renderpass2` API
   variants fail equally, so the bug is almost certainly shared
   non-dynamic-rendering code.
2. **(~1 day, dedicated session)** `L398`: `QueueExecutor`/`Fence` teardown
   deadlock. Build a *spec-valid* reproducer first (call `vkDeviceWaitIdle`
   before destroying anything except the fence itself, isolating the
   fence-specific race from the broader object-lifetime class this session's
   reverted reproducer accidentally also hit). If it reproduces, the fix is
   likely `std::shared_ptr`-based reference counting on `Fence` (and possibly
   other in-flight-task-referenced objects) so destruction defers until no task
   still references them.
3. **(~30 min)** Cluster the 3,519 Fail / 1,656 ResourceError cases from
   `feme-20261007-full/report.txt` the same way `L399` was found
   (`sed`/`sort`/`uniq -c` by 4-component case-name prefix) — not attempted this
   session, time ran out.
4. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache item: already resolved, no action
   needed, carried forward unchanged for the record only.
