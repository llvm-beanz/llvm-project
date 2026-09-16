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

1. **Full session, highest payoff (9 cases at once), largest scope,
   still untouched across many sessions:** H124e(a)'s two-part design
   work (loop-carried-value spilling generalization + nested-divergent-
   branch-in-loop-body support in `matchLoopShape`/`EntryWrapper.cpp`).
   This session's own triage strongly suggests fixing this would *also*
   close `InterlockedExchange.resources.32.test`'s `feme-cpu-linearize`
   gap (likely the same underlying shape in `LoopLinearizer`, not just
   `EntryWrapper`) -- check both `Linearize.cpp`'s `LoopLinearizer` and
   `EntryWrapper.cpp` together, not just the latter.
2. **Large, deprioritized many sessions now:** H124d (upstream MLIR
   SPIR-V `OpDPdx`/`OpDPdy`/`OpFwidth`), `shaderImageGatherExtended`,
   `dyn-res-uav-counter.test`'s address-space mismatch,
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s
   heap corruption.
3. **Do not re-attempt H150** -- confirmed a prior session it's not a
   FeMe-side bug at all.
4. **No other separately-scoped small bugs found this session** -- the
   remaining 18 `check-hlsl-feme-vk` failures are now down to: H124e's
   9-case wrap-entry bucket (item 1 above), the 5-case `Ddx*`/`ddy_fine`/
   `fwidth` group (H124d), and 4 smaller not-yet-individually-triaged
   items (`ByteAddressBuffer/GetDimensions.test`,
   `StructuredBuffer/GetDimensions.test`, `WaveOps/WaveActiveMax.test`
   [H150, confirmed not fixable], `WaveOps/GroupMemoryBarrierWithGroupSync.test`
   [in the H124e bucket]). A future session with less time than a full
   H124e(a) push could triage `ByteAddressBuffer`/`StructuredBuffer`
   `GetDimensions.test` instead -- neither has been individually looked
   at yet.
