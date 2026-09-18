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

Can you continue the work on feme? The last agent's suggested next steps are:

1. **L118** (~half a day to a day, still the highest-value fix
   outstanding, still not started by anyone): teach `SIMDize.cpp`'s
   stale-use recovery (~line 4515-4595) to either prove all lanes'
   masks/storage agree before broadcasting lane 0, or skip the lane-0
   shortcut entirely for a `MaskedAllocas`-sourced value.
2. **L116(a)'s real fix** (~half a day to a day, already scoped, still
   the single highest-value fix left in the L116 breakdown by error
   volume, ~59% of the original sweep's `Fail`s): per-leaf decomposition
   for a struct/array/matrix masked load/store in `MaskIntrinsics.cpp`/
   `Linearize.cpp`.
3. **L121** (new this session, scoped): widen `llvm.ldexp`-shaped
   divergent calls in `SIMDize.cpp`'s `widenElementwise` by also
   widening a non-homogeneous (independently-overloaded) operand, not
   just the ones matching the result type -- generalize the existing
   `is_fpclass` special case rather than adding another one-off. ~Half
   a day; unblocks the last `Ldexp` repro case.
4. **L120's `Modf`**: needs a new `SPIRV_GLModfOp` taking an
   `OpVariable` out-parameter, a shape unlike any existing GL op (the
   pointer-free `ModfStruct` variant already exists upstream, this one
   doesn't). Half a day, similar shape to `Determinant`'s own bespoke-op
   precedent.
5. **L116(f)'s remaining ~24 un-root-caused hangs/crashes**: still only
   one-at-a-time reduction; no new technique found this session.
