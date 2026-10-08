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

1. **(~30 min)** Check `feme-20261008-full/`'s `part3` worker (PID 70573 at last
   check, ~83% done) — once finished, concatenate all 6 `logs/part*.stdout.log`
   and re-cluster the *entire* case space. **Remember `part6`'s
   `transform_feedback.primitives_generated_query` bucket there is `L403`-stale,
   not new** — skip it when re-clustering, don't re-diagnose it.
2. **(~1 day, dedicated session)** Build isolated reproducers for `part1`'s
   SIGSEGV (last case before the crash:
   `dEQP-VK.api.copy_and_blit.core.use_after_copy.a8b8g8r8_srgb_pack32.transfer_dst_optimal.1024x1024x2_tq_color_att_flag_img2img_msaa`)
   and `part2`'s `std::terminate`/`Aborted` (immediately after
   `dEQP-VK.memory.concurrent_access.shader_and_host`'s `DeviceLost`) — two
   distinct, concrete, case-pinned crash leads, neither chased this session due
   to time. Both are new-information (exact trigger case identified) versus
   every prior session's "aggregate counts only" status on this item.
3. **(~1 day, dedicated session)** `part4`'s 13,754-case `DeviceLost` cluster in
   `pipeline_library.vertex_input.*` — confirmed still present, completely
   separate from this session's `L404` `Fail`-bucket fix, not yet clustered by
   sub-shape or root-caused at all.
4. **(long-carried-over, still open)** `L375`:
   `switch_continue_{geom,tessc,tesse}` `DiamondFlattener` assertion — not
   touched this session, needs its own dedicated design session per the existing
   roadmap entry.
5. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache item: already resolved (see
   `L380`), no action needed, carried forward unchanged for the record only.
