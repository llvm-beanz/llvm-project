---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

1. **(~15 min, easy win)** File or draft an upstream VK-GL-CTS issue/PR
   against `vktMeshShaderInOutTestsEXT.cpp`: `PerPrimitiveData` and
   `PerVertexData` need to pad their `Vec3`/`IVec3` array fields to
   16-byte-stride (e.g. store as `Vec4`/`IVec4` and only fill the first
   3 components, or add explicit trailing padding members) to match the
   `std430` layout the test's own generated GLSL declares. Until that
   lands upstream, this specific 90-case cluster should be treated as
   an expected/known-CTS-issue failure, not a FeMe regression to chase.
2. **`L147`'s remaining clusters** -- `ubo.*` (713 cases, next-smallest
   after `mesh_shader.ext`, which is now fully triaged: 1 fixed, 90
   explained as CTS-side). `binding_model.shader_access` (11,834 cases,
   the overwhelming majority) is the eventual big one, likely wants its
   own dedicated session given the scale.
3. **`L148`** (14-case `subgroups.ballot_broadcast.*.
   requiredsubgroupsize{64,128}` hang cluster from `L146`) is still
   untouched -- a hang, not a crash, so expect to need a debugger or
   verbose logging rather than a stdout diagnostic.
4. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing)
   -- still the largest not-yet-started cross-repo item, if a session
   wants a change of pace from CTS triage.
5. `/tmp/l149/*` scratch (QPAs, stdout/stderr captures, the
   `offsetof_test.cpp`/binary) can be deleted; nothing there is
   referenced by anything committed. Both CTS build directories and
   `check-feme` remain incremental -- no reconfigure needed.
