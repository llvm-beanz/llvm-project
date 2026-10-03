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

1. **(dedicated session, unchanged, now the single largest `memory_model.*`
   blocker)** The `OpSpecConstantOp`-as-`OpSpecConstantComposite`-
   constituent MLIR dialect gap, 52 cases. Needs an upstream-MLIR-style
   design change (new module-scope symbol form for
   `spirv.SpecConstantOperation`, or extending `SpecConstantCompositeOp`'s
   constituents to accept SSA operands). Consider posting to the MLIR
   list before attempting solo, per the prior session's note.
2. **(a few hours)** The 3 `message_passing.permuted_index.*`-adjacent
   item from an earlier handoff is now done (this session). Next
   untouched cluster: `geometry_interaction`/`matrix_multiplication`/
   `shader_input_output`/`misc_draw`/`common_edge` groups from the much
   older 136-case tessellation residual list -- still not re-checked
   this session; given today's device_group/user_defined_io surprise,
   **re-run these first** before assuming they're still broken.
3. **(worth investigating once, low cost, carried over many sessions)**
   Wire up ThreadSanitizer for a one-off manual `FeMeVulkanTests` run.
   Lower urgency now that the thread-safety audit (item above) turned
   up nothing across every remaining object class, but still worth
   doing once as a second, stronger confirmation.
4. **(dedicated session, carried over many sessions, unchanged)** `L344`
   item 2 / `L335` -- N-barrier generalization, 1 case
   (`shader_input_output.barrier`). Fully scoped in
   `FeMeGraphicsDesign.md`; still needs the actual implementation
   session.
5. **(lowest priority, many sessions carried over, unchanged)** `L265`
   -- ASTC alpha-decode tie-break, 12 cases. Next angle, still
   unattempted: compare decoded 4-texel neighborhoods pixel-by-pixel
   between `astc_5x5`/`astc_8x8` for a structural property correlating
   with tie direction.
6. **(a few hours, still overdue)** The broader-than-tessellation CTS
   sweep (`api`/`pipeline`/`shader_render`/`synchronization`,
   `binding_model` explicitly skipped for size) is still not done --
   carried over many, many sessions now without anyone picking it up.
   Worth relaunching in the background early next session specifically
   so it's not deferred again.
