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

1. **H124e** (large, unchanged for many sessions, now with 1 new confirmed data
   point): `feme-cpu-wrap-entry`'s "barrier inside non-linear control flow"
   error. Confirmed this session to be the *exact same* diagnostic blocking
   `InterlockedAdd/CompareExchange/CompareStore/Exchange.32.test` (4 failures,
   not fewer) -- worth prioritizing next given the multi-test payoff. Unknown
   effort, likely a full session on its own (region-splitting/wrap-entry pass
   work is inherently harder than the SIMDize-level fixes this session made).
2. **The `.resources.32.test` variants**
   (`InterlockedAdd`/`CompareExchange`/`CompareStore`/`Exchange.resources.32.test`,
   4 failures): confirmed this session to be a **separate, not-yet-triaged bug
   family** -- they use the resource-heap `feme.cpu.resource.atomic.*`
   runtime-call path, not raw `cmpxchg`/`atomicrmw`, so this session's fix does
   not touch them. Not yet triaged at all. ~30-60 min to at least get a
   diagnostic via `offloader` + `FEME_VULKAN_LOG_CREATION_ERRORS=1`.
3. **`GetDimensions.test` x4**
   (`ByteAddressBuffer`/`StructuredBuffer`/`TypedBuffer`): suspected (not
   confirmed) to share H124m's `OpArrayLength` gap, already on the roadmap as
   deprioritized. See "next action" above.
4. **`dyn-res-uav-counter.test`**: real, narrow bug, address-space mismatch in
   UAV-counter + `ResourceDescriptorHeap` combo. ~1-2 hours, carried over 3+
   sessions untouched.
5. **`Ddx*`/`ddy_fine`/`fwidth.test` group (5 failures)**: still suspected to
   trace to H124d's missing upstream MLIR `OpDPdx`/`OpDPdy`/`OpFwidth` SPIR-V
   dialect ops, still not individually confirmed. Large, deprioritized.
6. **`WaveActiveMax.test`/`WaveReadLaneAt.mtx.test`/`WaveIsFirstLane.test`/`ComponentAccumulationDataRace.test`/`GroupMemoryBarrierWithGroupSync.test`/`matrix.test`/`inc_counter_array_imm_idx.test`**:
   still individually untriaged, carried over many sessions. Don't assume any
   two share a cause without checking -- this bit prior sessions repeatedly.
7. **`shaderImageGatherExtended`**: large, multi-session capability gap (FeMe's
   gather is `ConstOffset`-only), carried over many sessions, still not filed as
   its own roadmap row.
8. Lower priority, deferred 25+ sessions:
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s
   pre-existing heap corruption (valgrind points at
   `buildStageStorage`/`executeDraws`).
