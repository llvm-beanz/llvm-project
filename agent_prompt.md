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

1. **H124r** (~1-2 hours, real triage): `Gather`/`GatherCmp` against
   `Cube`-shaped handles (3 cases: `Gather.test`, `GatherCmp.test`,
   `CalculateLevelOfDetail.test`). The first two are confirmed this
   session to be "innocent bystander" diagnostics — the actual
   rejected handle in both is a co-resident `TextureCube` in the same
   test function, not the `Texture2D` the error message names.
   `CalculateLevelOfDetail.test`'s own failure mode is not yet
   re-confirmed to be the same pattern. Start with
   `FEME_VULKAN_LOG_CREATION_ERRORS=1` (run the `offloader` binary
   directly, not through `llvm-lit -sv` — the env var didn't surface
   through lit's own capture this session) on each of the 3, confirm
   which handle each diagnostic really names, then widen the shape gate
   to `Cube`/`CubeArray` with face-selection math analogous to the
   existing `Sample`-family `Cube` support.
2. **H124s** (~1 hour, not triaged): `Array.GetDimensions.test` — needs
   its own `FEME_VULKAN_LOG_CREATION_ERRORS=1` run to confirm whether
   `OpImageQuerySize(Lod)` against `Array2D` is simply missing from the
   resource-normalization pass's op list.
3. **H124t** (~1 hour, not triaged): `Array.CalculateLevelOfDetail.test`
   — same as H124s but for `OpImageQueryLod`.
4. **H124k** (~1-2 hours, not started, simple/self-contained): missing
   `PackHalf2x16`/`UnpackHalf2x16` legalization (`Feature/HLSLLib/
   {f16tof32,f32tof16}.test`, 2 cases) — likely similar shape to
   H124f/H124j/H124q's already-fixed patterns.
5. **H124p** (~1-2 hours, not started): `feme-cpu-simdize` doesn't
   handle a divergent call to `llvm.is.fpclass.f32` (`Basic/
   Mandelbrot.test`, 1 case) — worth investigating together with
   H124e (same subsystem).
6. **H124e** (~several sessions, large, unchanged for many sessions):
   `feme-cpu-simdize`/`feme-cpu-linearize`/`feme-cpu-wrap-entry`
   divergence-handling gaps, ~11 of the original 102
   `check-hlsl-feme-vk` failures across 5+ distinct root causes.
7. **H124d** (large, deprioritized, unchanged for many sessions): new
   upstream MLIR SPIR-V dialect ops for `OpDPdx`/`OpDPdy`/`OpFwidth`.
8. **`shaderImageGatherExtended`** (large, newly noted this session, not
   filed as a roadmap row yet): FeMe advertises no support for this
   feature at all, which blocks *every* `dEQP-VK.glsl.texture_gather.*`
   CTS case from running against FeMe regardless of shape or offset.
   FeMe's own gather implementation is `ConstOffset`-only (never a true
   per-invocation dynamic offset), so honestly advertising this feature
   would itself be a real, separate, likely-multi-session capability
   addition — worth a deliberate roadmap filing before starting, not a
   quick flip.
9. Lower priority, deferred 17+ sessions now: `transform_feedback.
   fuzz.random_geometry.all_instance_array.12`'s pre-existing heap
   corruption — `valgrind`'s own trace points at
   `buildStageStorage`/`executeDraws` allocating a too-small buffer.
