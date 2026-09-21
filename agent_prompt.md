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

1. **(~30-60 min each, quick picks)** `L134`'s 6 remaining open sub-rows,
   roughly by expected size: `L134(g)` (1 case, `basic_draw.misc.
   maintenance5`) is the smallest; `L134(d)` (`implicit_sample_shading`, 12
   cases, 3 distinct shapes) and `L134(e)` (`shader_layer` at layer 256, 8
   cases) are next; `L134(b)` (`output_location.array`, 24 cases across
   many formats) and `L134(c)` (`multiple_interpolation`, 64 cases) are
   mid-sized; `L134(a)` (`indexed_draw`/`maintenance6`, 64 cases, the
   largest single family) is likely the most involved. **Before starting
   any of them, check their CTS failure message text first** -- if it's
   another "expected: X, got: X" exact-value mismatch, it may be the same
   `L132`/`L134(f)` barycentric-sum-not-exactly-1.0 class of bug in yet
   another consumer of `Bary0/1/2` (there may be more beyond depth and
   color -- `EdgeDist`/line antialiasing at ~3453 hasn't been checked for
   this same gap yet). If the message is something else entirely (a real
   `Fail` with visibly different values, a crash, a wrong-format rejection,
   etc.), it's a genuinely separate bug needing its own investigation.
2. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing) --
   still the largest not-yet-started cross-repo item, needs its own
   dedicated session.
3. **`L115(b)`** (pull-model interpolation) -- still flagged as needing a
   new runtime-callback ABI surface, not a quick pick.
4. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
5. **(~2 min)** `/tmp/ctsrun/l132fix/` (127MB, this and the prior session's
   scratch logs) can be deleted once a future session no longer needs its
   raw `.qpa`/`.log` files -- nothing in it is referenced by anything
   committed.
