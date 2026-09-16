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

1. **Re-triage `check-hlsl-feme-vk`'s remaining 31 failures fresh** —
   the last several sessions kept re-suggesting the same names
   (`InterlockedAdd/CompareExchange/CompareStore/Exchange/Xor.32.test`,
   `DdxCoarse/DdyCoarse/ddx_fine/ddy_fine/fwidth.test`,
   `WaveActiveMax.test`) without anyone individually confirming their
   root causes — don't assume any two share a cause without checking.
   ~1 hour to bucket, unknown effort to fix each bucket.
2. **H124e** (~several sessions, large, unchanged for many sessions):
   `feme-cpu-simdize`/`feme-cpu-linearize`/`feme-cpu-wrap-entry`
   divergence-handling gaps — needs the same per-case triage as above;
   may overlap with several of the `WaveOps/*` failures.
3. **H124d** (large, deprioritized, unchanged for many sessions):
   upstream MLIR SPIR-V dialect ops for `OpDPdx`/`OpDPdy`/`OpFwidth` —
   likely the root cause behind `DdxCoarse`/`DdyCoarse`/`ddx_fine`/
   `ddy_fine`/`fwidth.test` above; worth confirming that connection
   before starting either separately.
4. **`shaderImageGatherExtended`** (large, noted several sessions back,
   still not filed as its own roadmap row): blocks every `dEQP-VK.glsl.
   texture_gather.*` CTS case regardless of shape/offset. FeMe's own
   gather is `ConstOffset`-only, never true per-invocation dynamic
   offset — advertising this feature honestly is itself a real,
   separate, likely-multi-session capability addition. File a roadmap
   row before starting.
5. Lower priority, deferred 20+ sessions now: `transform_feedback.
   fuzz.random_geometry.all_instance_array.12`'s pre-existing heap
   corruption — `valgrind`'s own trace points at `buildStageStorage`/
   `executeDraws` allocating a too-small buffer.
