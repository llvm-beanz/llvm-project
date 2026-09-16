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

1. **~1-2 hours: reduce `InterlockedCompareExchange.32.test`'s aggregate-value
   bug to its exact minimal IR shape** (lower the imported SPIR-V dialect to
   LLVM IR via `feme-opt --feme-convert-spirv-to-llvm`, find the actual
   `cmpxchg`/aggregate-consuming instruction) before filing a roadmap row.
   Likely a 4th producer shape SIMDize needs (a `cmpxchg` result pair, or an
   `extractvalue` chain off one) -- don't assume it shares H142's exact fix
   shape without checking.
2. **~30 min: file roadmap rows for the InterlockedExchange/Xor groupshared-GEP
   pair** and the InterlockedAdd wrap-entry barrier case, both already-known
   H124e bucket members, so the bucket's own case count stays accurate.
3. **~30 min-1 hour: continue triaging the remaining ~20 of the 25
   `check-hlsl-feme-vk` failures** individually (`DdxCoarse`/`DdyCoarse`/
   `ddx_fine`/`ddy_fine`/`fwidth.test`, `WaveActiveMax.test`,
   `WaveReadLaneAt.mtx.test`, `WaveIsFirstLane.test`,
   `ComponentAccumulationDataRace.test`, `GroupMemoryBarrierWithGroupSync.test`,
   the 4 `GetDimensions.test` variants, `dyn-res-uav-counter.test`,
   `inc_counter_array_imm_idx.test`, `matrix.test`) -- still don't assume any
   two share a cause.
4. **~1-2 hours, real bug, narrow scope** (carried over): root-cause and fix
   `Feature/DynamicResources/dyn-res-uav-counter.test`'s address-space
   mismatch.
5. **~1 hour** (carried over): file + fix the `feme.cpu.resource.store.raw.i8`
   runtime gap found via `dEQP-VK.ssbo.layout.random.nested_structs*`.
6. **H124d** (large, deprioritized): upstream MLIR SPIR-V `OpDPdx`/`OpDPdy`/
   `OpFwidth` -- likely the root cause behind the `Ddx*`/`ddy_fine`/`fwidth`
   group above; confirm during step 3's triage rather than assuming.
7. **`shaderImageGatherExtended`** (large, still not filed as its own roadmap
   row): blocks every `dEQP-VK.glsl.texture_gather.*` case.
8. Lower priority, deferred 24+ sessions now:
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s
   pre-existing heap corruption.
