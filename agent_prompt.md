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

1. **(~1 day, dedicated session)** Fix `L409`. Start by generalizing
   `Fence`'s retain/release pattern (`L398`, `Sync.cpp`) to `Pipeline`
   first (the type proven here), using `leakprobe_uaf.cpp` as the
   regression test (add it as a proper gtest-based unit test once the fix
   lands, not just a standalone throwaway). Confirm the exact same crash
   reproduces in a current `check-feme`-style unit test shape before
   fixing, then confirm it's gone after.
2. **(~2-4 hours after the above)** Audit whether `RenderPass`,
   `Framebuffer`, descriptor sets, images, and buffers share the same raw-
   pointer-capture-with-no-keep-alive pattern (only `Pipeline` and the
   render-pass/framebuffer pair are directly proven so far). Fix each one
   found, following the same retain/release template.
3. **(~30 min, after the fix lands)** Re-run `part2`'s full caselist
   (`/tmp/feme_full_caselist_part2` or similar) and confirm the
   `DeviceLost` rate drops from 22.74% toward 0%. This is the real
   acceptance criterion for closing `L409`.
4. **(~5 min)** Check `part1` (PID 7228) one more time before starting the
   `L409` fix session — if it's finally crashed, grab the backtrace before
   doing anything else; it may now be directly explainable by `L409`
   without needing further investigation.
5. Drop from future hand-offs — fully resolved, stop citing: `L407`
   (closed, not a leak — see 3 sessions ago), the "resource-table
   exhaustion" hypothesis from the last handoff (superseded by this
   session's actual finding), `L375`, `LLVM_EXTERNAL_PROJECTS=OffloadTest`
   (`L380`), roadmap strikethrough cleanup pass, `L402`/`L406`,
   `pipeline_library.stencil` verification, `part1`'s
   `use_after_copy.a8b8g8r8_srgb_pack32...` case (confirmed not a
   deterministic trigger).
