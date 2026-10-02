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

1. **(dedicated session)** Start the `fill_overlap_quads_*` port: implement
   Mesa's `QuadGeneratePoints`/`QuadGenerateConnectivity` concentric-ring
   generation in FeMe's `Tessellator.cpp`, including the
   `StitchRegular`/`StitchTransition` table-driven connectivity. Tackle the
   quad domain first and verify it in isolation before touching the
   triangle domain -- they're independent fixes per the scoping above.
   `/tmp/mesa_tess.cpp` (this session's fetch) is gone (cleaned up at
   session end); re-fetch from
   `https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/src/gallium/auxiliary/tessellator/tessellator.cpp`.
2. **(separate dedicated session)** Port `TriGeneratePoints`'s fixed-point
   barycentric math for `fill_overlap_triangles_*`, once the quad port is
   done and stable -- don't conflate the two, they're genuinely separate
   algorithms in the reference.
3. **(design session, carried over many sessions, unchanged)** `L344` item
   2 / `L335` -- N-barrier generalization, 1 case
   (`shader_input_output.barrier`). Already fully scoped in
   `FeMeGraphicsDesign.md`; still needs someone to actually spend the
   multi-file implementation session.
4. **(lowest priority, many sessions carried over, unchanged)** `L265` --
   ASTC alpha-decode tie-break, 12 cases. Blit-geometry hypothesis ruled
   out this session. Next angle to try: compare the actual decoded
   4-texel neighborhoods feeding the bilinear blend for `astc_5x5` vs.
   `astc_8x8` pixel-by-pixel (not just confirm the tie-landing mechanism,
   which is already proven) -- look for a structural property (texel
   position within its source block, e.g.) that correlates with which tie
   direction each group needs.
5. **(process note)** The `offload-test-suite` lit-annotation backlog
   (item 4 on handoff lists for ~6+ sessions) is now zero. If new
   `check-hlsl-feme-vk` failures appear in future sessions, triage them
   the same way this session did (standalone `offloader -debug-layer` run
   plus reading the actual VK API bridging code) rather than deferring
   them as "out of scope" by default -- two of this session's three turned
   out to be a real, fixable bug and a real, fixable stale annotation, not
   environment noise.
