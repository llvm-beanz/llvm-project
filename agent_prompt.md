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

1. **(~20-30 min)** `L125(z)`: the `blend.clamp.*` 4/6-fail bucket,
   including the unreconciled contradiction with the old `H99a` row's
   own "21/21 pass" claim -- start with a `--deqp-log-images=enable`
   trace on one case (e.g. `blend.clamp.r8g8b8a8_unorm`) following
   this session's own methodology, and specifically check whether the
   "clamp" group's own semantics (blending past `[0, 1]` and expecting
   the packed result to be clamped) reveals a missing clamp somewhere
   in `mergeColor`'s own pipeline.
2. Finish the `pipeline.monolithic.blend.*` regression sweep this
   session only got 8,073 of the way through (timed out at 30 min) --
   worth letting run to completion in the background at the start of a
   future session, purely as extra regression confidence (no fails
   found yet beyond the already-known 4 `blend.clamp.*` ones).
3. `L125(s)`/`L125(t)` (vertex_input format gaps, bind-point bucket)
   remain untouched from several sessions back -- good alternative
   picks if `L125(z)` stalls.
4. `L125(m)`/`L125(n)` (upstream MLIR+LLVM `ConstOffsets` plumbing)
   remains the other large, not-yet-started cross-repo item -- not a
   quick pick, needs its own dedicated session.
5. `L115(b)` (pull-model interpolation) remains flagged from several
   sessions ago as a larger, not-yet-started item needing a new
   runtime-callback ABI surface -- also not a quick pick.
6. The BC-format CTS coverage gap noted across multiple prior sessions
   (`sampler.view_type.*.format.*bc*.address_modes.
   *clamp_to_border*` matches 0 cases) still hasn't been investigated
   -- worth a quick dedicated look next time nothing else is more
   pressing.
7. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
8. This session's own scratch CTS logs are already cleaned up (along
   with two prior sessions' leftover `l125u*`/`l125x` directories) --
   nothing to do here.
