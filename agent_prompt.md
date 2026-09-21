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

1. **`L115(b)`** (pull-model interpolation, now covering both its own original
   scope and the former `L135`) -- the only interesting open item this session
   found. Needs a new runtime-callback ABI surface, not a quick pick: (a) a new
   stage op (e.g. `feme.stage.input.interpolate`) carrying resolved
   element/row/component plus a runtime mode and operand(s); (b) a new
   per-invocation runtime-callback mechanism in `Executor.cpp` (modeled on
   `ImageCalls.cpp`'s existing texture-sampling precedent) exposing enough of
   `Executor.cpp`'s own per-lane triangle data (`Tri.Pos`/`InvW`/`Varyings`,
   `Area`, `Quad.PixelX`/`PixelY`) to recompute barycentric weights at a
   runtime-supplied point; (c) `SPIRVToLLVMPatterns.cpp` conversion patterns for
   `spirv.GL.InterpolateAt{Centroid,Sample,Offset}` themselves. Estimated 1-2
   full sessions given the new ABI surface -- start a fresh session dedicated to
   just this, don't try to squeeze it into a continuation.
2. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
3. No scratch left over to clean up this session (`/tmp/ctsrun/l136/` and its
   contents already deleted).
