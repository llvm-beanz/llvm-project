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

1. **`L325` (a few hours, best next pick, newly scoped)**: the
   `static_stipple`/`dynamic_stipple`/`dynamic_stipple_and_topology`
   cluster, 49 of the remaining 70 `rasterization` failures. Likely a
   stipple-arc-length-computation bug, distinct from `L324`'s
   half-open-rule fix (adjacency topology vs. stipple-pattern
   application along the line). Start the same way `L324` did: pick one
   failing case (e.g. `dynamic_stipple.bresenham_lines`), run with
   `--deqp-log-images=enable`, extract the embedded PNGs (see this
   session's and `L324`'s own technique: regex `.qpa` XML by
   `<ImageSet>` boundary, base64-decode, diff with PIL/numpy), and
   compare fragment-count/position against the spec's own stipple
   pattern math (`gl_FragDepth`... actually spec section on
   "Line Stipple" -- check `primsrast.adoc`'s stipple subsection, not
   yet read this session).
2. **`L265` (a few hours, lowest priority, many sessions carried over,
   unchanged)**: ASTC alpha-decode tie-break (4 block sizes). Needs a
   Mesa/lavapipe reference-decoder comparison.
3. **Rasterization's remaining 21 scattered failures** (`stencil`,
   `color_at_beginning`/`color_at_end`, `non_strict_line*`,
   `triangle_fan`/`triangle_strip`, `line-strip`, `polygon-mode-lines`,
   `depth_bias`, `provoking_vertex`, `flatshading`, `line_continuity`,
   `frag_side_effects`, `maintenance5`,
   `d24_unorm_constant_one_greater`, `draw`, `depth`) -- confirmed
   pre-existing (not new regressions from `L324`), individually
   untriaged, likely several distinct small bugs. Not yet scoped into
   its own roadmap item; do that first if picked up.
4. **`offload-test-suite`'s own lit-annotation issues**
   (`spec_const_32_bits.test`/`WaveActiveMax.test`/`array_of_matrices.test`)
   -- low priority, pre-existing, outside this project's scope.
5. **Branch-drift housekeeping**: confirmed unchanged again this
   session (`854cc3f`) -- no action needed until content actually
   diverges.
