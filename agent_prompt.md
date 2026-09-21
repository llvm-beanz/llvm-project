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

1. **(~15-20 min, smallest pick)** `L134(i)` -- `B10G11R11_UFLOAT_PACK32`
   missing an `unpackColor` case (`ImageFixture.cpp`) and its
   `isSupportedColorAttachmentFormat` (`RenderPass.cpp`) entry. A real
   `packClearColor` encode already exists for this format to mirror in
   reverse; the 6 CTS cases this session isolated
   (`b10g11r11-ufloat-pack32-{highp,mediump}`, plain/`-output-{float,vec2}`)
   are ready to re-verify against once fixed.
2. **`L134(h)`** -- the 4-case `feme-cpu-simdize` crash on a
   vector-wider-than-scalar array-output shape. Not root-caused; the
   crash's `.bc`-named divergent value suggests a bitcast `SIMDize.cpp`'s
   divergence allowlist doesn't recognize -- needs its own IR-dump-based
   trace (reuse this session's `FEME_DEBUG_DUMP_STAGE_IR` technique, or a
   targeted look at `SIMDize.cpp` directly).
3. **`L134(c)`** (`multiple_interpolation`, 64 cases) and **`L134(a)`**
   (`indexed_draw`/`maintenance6`, 64 cases) -- the 2 still-open original
   `L134` sub-rows, in that size order.
4. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing) --
   still the largest not-yet-started cross-repo item, needs its own
   dedicated session.
5. **`L115(b)`** (pull-model interpolation) -- still flagged as needing a
   new runtime-callback ABI surface, not a quick pick.
6. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
7. This session's scratch (`/tmp/ctsrun/l134b/`, `/tmp/l134b/`) has
   already been cleaned up; nothing left over to delete.
