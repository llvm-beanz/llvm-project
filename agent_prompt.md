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

1. **(~20-30 min)** `L125(r)` (`InterpolateAtCentroid`/`InterpolateAtSample`
   legalization gap, 27 fails) is likely the fastest win of the six: a missing
   SPIR-V-to-LLVM conversion pattern for two GLSL.std.450 extended instructions,
   mechanically similar in shape to other "operation not legalized" gaps this
   roadmap has already closed. Start in `SPIRVToLLVMPatterns.cpp`.
2. **(~15 min)** `L125(q)`'s sub-bucket (1) (80 fails, `border_swizzle`'s
   single/dual-channel non-8-bit gather formats reporting "image fixture format
   is not yet supported") already has its root cause identified this session --
   a mechanical format-support gap, likely a good second pick alongside L125(r).
3. `L125(p)` (440 fails, "Image mismatch" across
   `image.suballocation`/`image_view.view_type`/`sampler.view_type`) is the
   single largest bucket by far but needs its own
   `--deqp-log-decompiled-spirv=enable` trace per area before estimating --
   start here only with more time budgeted.
4. `L125(s)`/`L125(t)`/`L125(u)` (vertex_input format gaps, the bind-point
   bucket, and the small exact_sampling bucket) are all not yet started at all
   -- good picks once the above three are underway or blocked.
5. `L125(m)` (upstream MLIR+LLVM `ConstOffsets` plumbing) remains the other
   open, larger cross-repo item from before this session -- not touched, not a
   quick pick.
6. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
7. This session's own scratch CTS logs (`/tmp/ctsrun/l125c2/*`) are already
   cleaned up -- nothing to do here.
