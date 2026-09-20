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

1. `L125(p)`/`L125(s)`/`L125(t)`/`L125(u)` remain the untouched rows
   from the `L125(c)` decomposition several sessions back -- good next
   picks. `L125(p)` (440 fails, "Image mismatch" across
   `image.suballocation`/`image_view.view_type`/`sampler.view_type`) is
   the single largest remaining bucket by far but needs its own
   `--deqp-log-decompiled-spirv=enable` trace per area before
   estimating -- start there only with a full session budgeted, not a
   quick pick.
2. `L115(b)` (pull-model interpolation, `InterpolateAtCentroid`/
   `InterpolateAtSample`) remains the other real, larger,
   not-yet-started item flagged several sessions ago -- needs a new
   runtime-callback ABI surface (barycentric/interpolant-plane data
   doesn't exist in `FemeFragmentInvocation` today), properly budgeted
   as its own 1-2 session item, not squeezed in alongside smaller
   fixes.
3. Given this session's own "the ABI already had what we needed"
   surprise, it may be worth a quick sanity pass the next time any
   future roadmap row's own scoping text asserts "needs new ABI
   storage" -- confirm that claim genuinely holds (by reading the
   relevant mapping/resolution code, the way `mapBorderColor` was
   checked here) before committing to the larger design, since it may
   again turn out the existing fields already suffice.
4. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
5. This session's own scratch CTS logs (`/tmp/ctsrun/l125v/*`) are
   already cleaned up -- nothing to do here.
