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

1. **H124u** (~1-2 hours, filed a few sessions back, still open):
   `CalculateLevelOfDetail` against a `Cube`/`CubeArray` handle — the
   `hasOnlySupportedImageUses`/`isQueryLodIntrinsic` gate this session
   widened to `Array2D` still excludes `Cube`/`CubeArray` entirely.
   Unlike `Array2D`'s 2-component coordinate, a cube's own LOD query
   coordinate is a 3-component direction vector (not yet confirmed via
   `spirv-dis` — check that first), and the LOD formula itself needs
   `femeRTComputeCubeUVDerivatives`-style face selection, not a bare
   reuse of `QueryLod2D`. Look at `femeRTPlanImplicitLod`/
   `femeRTComputeUnclampedQueryLod`'s own existing Cube-aware code paths
   (used by ordinary Cube sampling) for the pattern to mirror.
2. **H124k/H124q/H124r/H124s/H124t are now what remains of H124q's
   original 7-case bucket, all closed.** Re-run
   `check-hlsl-feme-vk`'s full failure list fresh (33 failures now) and
   re-bucket by root cause — several of the 33 look like fully separate,
   unstarted issues (`InterlockedAdd/CompareExchange/CompareStore/
   Exchange/Xor.32.test`, `DdxCoarse/DdyCoarse/ddx_fine/ddy_fine/
   fwidth.test`, `WaveActiveMax.test`, `Mandelbrot.test`) — don't assume
   any two share a cause without individually triaging first.
3. **H124p** (~1-2 hours, not started, carried over 3+ sessions):
   `feme-cpu-simdize` doesn't handle a divergent call to
   `llvm.is.fpclass.f32` (`Basic/Mandelbrot.test`, 1 case) — worth
   pairing with H124e (same subsystem).
4. **H124e** (~several sessions, large, unchanged for many sessions):
   `feme-cpu-simdize`/`feme-cpu-linearize`/`feme-cpu-wrap-entry`
   divergence-handling gaps — needs per-case triage first, don't assume
   one fix covers all.
5. **H124d** (large, deprioritized, unchanged for many sessions):
   upstream MLIR SPIR-V dialect ops for `OpDPdx`/`OpDPdy`/`OpFwidth`.
6. **`shaderImageGatherExtended`** (large, noted 2 sessions back, not
   yet filed as its own roadmap row): blocks every `dEQP-VK.glsl.
   texture_gather.*` CTS case regardless of shape/offset. FeMe's own
   gather is `ConstOffset`-only, never true per-invocation dynamic
   offset — advertising this feature honestly is itself a real,
   separate, likely-multi-session capability addition. File a roadmap
   row before starting.
7. Lower priority, deferred 19+ sessions now: `transform_feedback.
   fuzz.random_geometry.all_instance_array.12`'s pre-existing heap
   corruption — `valgrind`'s own trace points at `buildStageStorage`/
   `executeDraws` allocating a too-small buffer.
