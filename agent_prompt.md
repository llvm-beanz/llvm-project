---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

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

# Request

Can you work the H-series milestones?

The last session suggested the next steps:

1. **H167, best next target now, ~half a day to a day.** Three
   candidate fixes are already sketched in the roadmap row: (a) have
   `LinearizePass` mark "known-uniform-despite-syntax" values/branches
   with metadata `SIMDizePass` can consume; (b) teach `SIMDizePass`'s own
   uniformity analysis the same masked-load taint-safety reasoning
   `DiamondFlattener` already has; (c) have `DiamondFlattener`
   conservatively flatten any branch reading a tainted-fed masked load
   even when its own condition isn't flagged, trading a little masking
   overhead for guaranteed downstream agreement. (c) is probably the
   smallest, safest first attempt -- try it first before (a)/(b)'s
   larger cross-pass plumbing.
2. **H164, unbounded, budget a day+.** `InterlockedCompareExchange.32.test`
   segfaults inside JIT'd code with an empty stack. Start by hand-editing
   `feme-opt` output, not a debugger -- three hypotheses already listed
   in the roadmap row (uninitialized spilled lane-pointer slot, a
   `poison` artifact from `OpAtomicCompareExchange`'s result struct, or a
   barrier-split prefix region entered with the wrong wave mask).
3. **H168, not urgent but real, ~an afternoon once picked up.** No repro
   reduced yet -- first step is exactly that: reduce one of the two
   prior incidental repros to a standalone `feme-opt -passes=feme-cpu-simdize`
   case and confirm it reproduces in isolation before touching
   `CreateMaskedScatter`.
4. **`ByteAddressBuffer`/`StructuredBuffer` `GetDimensions.test`** (H160):
   real, well-scoped, but a genuine upstream MLIR SPIR-V dialect gap
   (`OpArrayLength` has no op at all) -- comparable in size to H124d's
   own upstream gap. Not urgent, but the smallest-blast-radius way to
   shrink the failure count by 2 without touching `feme` pass code at
   all, if someone wants an upstream-MLIR-flavored session instead.

