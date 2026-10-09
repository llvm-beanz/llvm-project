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

1. **(~5 min)** Check `part1`/`part2`/`part3` (PIDs 7228/14336/70573) again —
   `part3` is closest to finishing (~98%). Once it finishes, concatenate all 6
   `logs/part*.stdout.log` and re-cluster the entire case space (skip `part6`'s
   `L403`-stale `transform_feedback.primitives_generated_query` bucket).
2. **(~1 day, dedicated session)** `part1`'s SIGSEGV — still not reproduced
   across multiple sessions now (last known trigger case:
   `dEQP-VK.api.copy_and_blit.core.use_after_copy.a8b8g8r8_srgb_pack32.transfer_dst_optimal.1024x1024x2_tq_color_att_flag_img2img_msaa`,
   unconfirmed).
3. **(~1 day, dedicated session)** `L402`:
   `subpass_dependencies.late_fragment_tests` depth/stencil `Fail` cluster (36
   cases, filed but not fixed per `VulkanCTSReport.md` item 35) — the next
   concrete, already-scoped bug to root-cause.
4. **(~30 min, low priority)** Roadmap.md strikethrough cleanup: several IDs
   (`L377`, `L378`, `L380`, `L381`, base IDs without a lettered suffix, plus
   likely more) are fully resolved per `VulkanCTSReport.md` but not struck
   through — the same bookkeeping gap that caused `L375` to be repeatedly
   mis-handed-off as open. Worth a dedicated pass to prevent future sessions
   wasting a check-in on already-closed items.
5. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache item: already resolved, no action
   needed, record-only (carried forward unchanged across many sessions now —
   safe to stop repeating in future handoffs).
