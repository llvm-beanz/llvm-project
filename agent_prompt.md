---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
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

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(a few hours)** The overdue full/broad Vulkan CTS re-run: this
   session and the last several have only ever compared the
   1088-case `tessellation.*` sample (plus, previously, the
   29451-case `draw.*` group once). The full `vk-default` mustpass
   list is genuinely large (~3.7M lines across 196 group files) --
   running the whole thing in one sitting is likely multi-hour+.
   Worth either budgeting a dedicated session for it, or picking a
   handful of the largest/most-central groups (`api`, `pipeline`,
   `shader_render`, `synchronization`) as a wider-than-tessellation
   but still bounded sample.
2. **(unknown)** No specific tessellation failures are known to
   remain untriaged right now -- the remaining 156/1088 failures in
   the `tessellation.*` sample are all pre-existing, already-tracked
   groups (`invariance`, `user_defined_io`, `primitive_discard`,
   `shader_input_output`, `misc_draw`, `tesscoord`,
   `common_edge`, `matrix_multiplication`, `geometry_interaction`) --
   worth picking one of these next if continuing tessellation work,
   `user_defined_io` (27 cases) or `primitive_discard` (24 cases) look
   like the next-largest untriaged chunks.
3. The pre-existing `llvm.lifetime.start/end can only be used on
   alloca or poison` crash on hlsl-sourced tessellation-control
   shaders (confirmed again this session, unrelated, halts the
   `tessellation.*` batch at `fractional_spacing.hlsl_{even,odd}` and
   every `winding.*.hlsl_*` case) still has no dedicated roadmap row --
   worth adding one and root-causing it properly instead of treating
   it as background noise every session.
4. No git stashes to clean up this session (one `git stash`
   push/pop pair used mid-session purely to A/B-test the fix against
   its own new unit test, popped back immediately after).
