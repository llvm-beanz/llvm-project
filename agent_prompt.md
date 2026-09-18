---
model: claude-sonnet-5
resume: 52e661a0-b284-45ee-892f-3073721ca338
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

The network cut out while you were working on this, can you continue and
complete the work?

Can you continue the work on feme? The last agent's suggested next steps are:

1. **L116(a)** (~30 min to confirm same-as-C8b, then unknown to fix
   depending on answer): grep `SIMDize.cpp`'s own aggregate
   `insertvalue`/`extractvalue` handling and `MaskIntrinsics.cpp`'s
   `appendScalarMangling` side by side -- confirm whether they share one
   underlying "aggregate values through the CPU backend" gap before
   writing any code.
2. **L116(c)'s `Determinant`** (~half a day, same shape as L115(a)):
   add `SPIRV_GLDeterminantOp` to `SPIRVGLOps.td` (needs a square-matrix
   operand shape, not one of the existing generic patterns) plus a
   feme-side lowering (this one's just arithmetic, no runtime callback
   needed unlike L115(b) -- should close fully in one session).
3. **L116(e)'s two one-off bugs** (~1-2 hours combined): good if you
   want two visible wins before tackling (a) or (c).
4. **L116(f)'s remaining 22 unroot-caused hangs/crashes**: one-at-a-time
   reduction, same technique used on the two already investigated.
5. Once L116 closes (or is judged big enough to move on from), go back
   to L106's other untriaged candidates: `pipeline.monolithic.*`,
   `subgroups.*`, `compute.*` -- still nobody has picked these up.
