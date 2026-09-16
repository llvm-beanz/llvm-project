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

1. **H124e wrap-entry bucket** (now 7 confirmed cases, up from 5 -- highest
   payoff, unfixed for many sessions): `feme-cpu-wrap-entry`'s region-splitting
   pass only supports "a straight-line wave body or a single uniform loop" -- a
   barrier inside any other non-linear control flow shape is rejected outright.
   Fixing this could close up to 6 failures at once
   (`InterlockedAdd`/`CompareExchange`/`CompareStore`/`Exchange.32.test`,
   `InterlockedAdd`/`CompareStore.resources.32.test`). Likely a full session on
   its own -- region-splitting pass design work is harder than the SIMDize-level
   fixes recent sessions made.
2. **`InterlockedCompareExchange.resources.32.test`'s `feme-cpu-simdize`
   divergent-branch gap** (newly confirmed this session, not yet triaged
   further): "the divergence transform (LinearizePass) did not remove it, or
   produced a shape this pass cannot widen" -- needs an IR-level reduction (via
   `feme-opt --feme-convert-spirv-to-llvm`) to find the exact unsupported shape,
   same methodology H143 used.
3. **`InterlockedExchange.resources.32.test`'s `feme-cpu-linearize`
   multi-exit-loop gap** (newly confirmed this session as real, not yet fixed):
   "loop has more than one divergent exit check" -- also needs an IR-level
   reduction before attempting a fix.
4. **`Ddx*`/`ddy_fine`/`fwidth.test` group (5 failures)**: still suspected to
   trace to H124d's missing upstream MLIR `OpDPdx`/`OpDPdy`/`OpFwidth` SPIR-V
   dialect ops, still not individually confirmed across sessions. Large,
   deprioritized.
5. **`dyn-res-uav-counter.test`**: real, narrow bug, address-space mismatch in
   UAV-counter + `ResourceDescriptorHeap` combo. ~1-2 hours, carried over 4+
   sessions untouched.
