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

1. **(a few hours)** `L265` -- ASTC alpha-decode tie-break (4 block
   sizes). Lowest priority, many sessions carried over, unchanged.
   Needs a Mesa/lavapipe reference-decoder comparison.
2. **(a few hours each, 21 cases total)** Rasterization's remaining
   scattered failures, now fully enumerated this session:
   `conservative.overestimate.*.lines.degenerate.0_00`,
   `depth_bias.d24_unorm_constant_one_less`,
   `flatshading.{triangle_strip,triangles}`,
   `frag_side_effects.color_at_{beginning,end}.{depth_never,kill,
   terminate_invocation}` (6 cases), `line_continuity.polygon-mode-lines`,
   `maintenance5.non_strict_line*` (4 cases),
   `polygon_as_large_points.mesh_dynamic_polygon_mode`,
   `provoking_vertex.draw.default.triangle_list`,
   `rasterization_order_attachment_access.{depth,stencil}.*` (4 cases).
   Not yet individually triaged -- pick one cluster (e.g.
   `frag_side_effects`, 6 cases, looks like the largest same-cause
   group) and start there.
3. **(unknown)** Tessellation triage still open from several sessions
   back: `user_defined_io` (27 cases), `shader_input_output`/
   `misc_draw`/`common_edge`/`matrix_multiplication`/
   `geometry_interaction` groups. Not touched this session.
4. **(low priority, out of scope)** `offload-test-suite`'s own
   lit-annotation issues (`spec_const_32_bits.test`/`WaveActiveMax.test`/
   `array_of_matrices.test`).
5. **Branch-drift housekeeping**: re-checked this session, still a
   non-issue (`854cc3f`, unchanged).
