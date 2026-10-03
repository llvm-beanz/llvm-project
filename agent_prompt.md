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

1. **(a few hours, start here)** `fill_overlap_triangles_*` (4 cases) --
   root-cause the centroid-blob defect found in step 4 above. Start by
   auditing `bridgeRingsByEdge`/`bridgeEdge` (`Tessellator.cpp` ~line 141)
   for an analogous diagonal/ordering choice near the innermost rings,
   where the bands are thinnest and any deviation is most visible. The
   diagnostic images are already known-useful; re-run
   `--deqp-log-images=enable` on both `_equal_spacing_draw` and
   `_fractional_even_spacing_draw` if the first angle doesn't pan out.
2. **(dedicated session, unchanged, carried over many sessions)** `L344`
   item 2 / `L335` -- N-barrier generalization, 1 case
   (`shader_input_output.barrier`). Fully scoped already; needs someone to
   actually spend the multi-file implementation session across the five
   subsystems listed in step 5 above.
3. **(lowest priority, many sessions carried over, unchanged)** `L265` --
   ASTC alpha-decode tie-break, 12 cases. No new angle found or attempted
   this session. The concrete next angle from two sessions ago (compare
   decoded 4-texel neighborhoods pixel-by-pixel between `astc_5x5` and
   `astc_8x8` for a structural property correlating with tie direction)
   is still the best lead, just not attempted again this session.
4. **(low priority, out of scope, unchanged)** `offload-test-suite`'s own
   lit-annotation backlog is at zero (confirmed clean last session); no
   new issues found this session since no new `check-hlsl-feme-vk` run was
   needed (no FeMe ICD changes this session affect its own runtime
   behavior in a way distinguishable from the CTS numbers already tracked
   above -- this wasn't independently re-verified this session and would
   be a cheap confirmation to run first thing next time if picking back
   up `offload-test-suite` work).
5. **(process note)** The `.qpa` base64-image-extraction technique (step 2
   above) is now the standard first move for any `misc_draw`/tessellation
   visual-diff investigation -- much faster than ASCII heatmaps or numeric
   diffs alone, and it's what turned a presumed multi-session port into a
   6-line fix this session. Keep reaching for it before assuming a
   topology-level root cause.
