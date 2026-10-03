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

1. **(a few hours, start here)** `fill_overlap_triangles_*` (4 cases,
   `L351`) -- new angle: floating-point boundary tie, not topology.
   Pick one known-mismatching pixel from `--deqp-log-images=enable`'s
   `ErrorMask`, and instrument/print the exact interpolated `d` value
   FeMe computes there (via the barycentric weights at that screen
   position) alongside `int(d * numConcentricTriangles) % 3`. Check if
   `d` sits within float epsilon of a `k/5` boundary (this test's tess
   levels give `numConcentricTriangles=5`). If so, narrow down *which*
   float operation disagrees with the reference: the ring
   `CumulativeScale` product, the rasterizer's own barycentric
   interpolation, or the TES's own cast. This is the same bug *class*
   as `L265` below, so techniques from any future `L265` session may
   transfer.
2. **(dedicated session, unchanged, carried over many sessions)** `L344`
   item 2 / `L335` -- N-barrier generalization, 1 case
   (`shader_input_output.barrier`). Fully scoped already in
   `FeMeGraphicsDesign.md`'s Status subsection; needs someone to spend
   the multi-file implementation session.
3. **(lowest priority, many sessions carried over, unchanged)** `L265` --
   ASTC alpha-decode tie-break, 12 cases. Untouched again this session.
   Next angle (from two sessions ago, still unattempted): compare
   decoded 4-texel neighborhoods pixel-by-pixel between `astc_5x5` and
   `astc_8x8` for a structural property correlating with tie direction.
4. **(low priority, out of scope, unchanged)** `offload-test-suite`'s
   own lit-annotation backlog is at zero (confirmed clean 2 sessions
   ago); not independently re-verified this session since no FeMe
   runtime-behavior change landed. Cheap confirmation to run first if
   picking back up `offload-test-suite` work.
5. **(process note)** When an A/B test of a well-reasoned hypothesis
   comes back negative, revert and document rather than keep a
   non-improving change -- this session's own `bridgeEdgeMirrored`
   detour is a concrete example: technically more "reference-faithful"
   code is not automatically a fix if the test's real failure mode is
   numerical, not topological.

