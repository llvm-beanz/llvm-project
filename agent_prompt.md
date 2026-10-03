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

1. **(2 minutes, do this first)** Check `/tmp/cts_sweep/progress.log` and `tail
   -c 2000` the newest `.stdout.log` -- the sweep was running unattended when
   this session ended. Parse whatever new groups finished (`draw` was in
   progress; `glsl`/`texture`/`ycbcr` etc. likely done or close by now) and
   append to `VulkanCTSReport.md`. Keep going down the `CTS_GROUPS` list in
   `run_sweep.sh` -- `api`/`pipeline`/`binding_model`/`shader_object`/`image`
   are the last 5, deliberately saved for last (large/slow).
2. **(a few hours, carried over, unchanged)** `L344` item 2 / `L335` -- the
   N-barrier-generalization implementation session. Now confirmed by *two*
   independent CTS findings (`shader_input_output.barrier` and, this session,
   `rasterization.line_continuity.*`) to be the same root cause blocking real
   cases. Scoped already in `FeMeGraphicsDesign.md`. Worth bumping priority
   given it's now blocking 2+ known clusters, not 1.
3. **(lowest priority, many sessions carried over, unchanged)** `L265` -- ASTC
   alpha-decode tie-break, 12 cases. Next angle, still unattempted: compare
   decoded 4-texel neighborhoods pixel-by-pixel between `astc_5x5`/`astc_8x8`.
4. **(worth doing once, low cost)** Re-run
   `mesh_shader.ext.misc.many_mesh_work_groups_*` one more time with zero other
   concurrent heavy processes running, purely to rule out a genuine (vs.
   contention-induced) timing bug with full confidence. Current evidence (clean
   3/3 isolated repro) already favors "not a real bug," so this is a
   belt-and-suspenders check, not an urgent one.
5. **(process note for future sessions)** `build-tsan/` is a legitimate,
   reusable asset -- don't rebuild it from scratch next time TSan is needed;
   just `ninja -C build-tsan FeMeVulkanTests` after any
   `Sync.h`/`QueryPool.h`/similar change to get a fast confirmation. It shares
   ccache with the main `build/` dir but needs its own object cache entries
   (sanitizer flags change the cache key), so expect the *first* rebuild after a
   code change to take a bit, not zero time.
