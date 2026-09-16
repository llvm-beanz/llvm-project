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

1. **~1-2 hours: extend the triage script(s) to handle multi-shader
   pipelines** (vertex+fragment, vertex+geometry+fragment, etc. --
   whatever each test's own `# RUN:` lines actually declare), then use
   it to triage `Graphics/VertexShaderResourceCube.test`.
2. **~1-2 hours, real bug, narrow scope**: root-cause and fix
   `Feature/DynamicResources/dyn-res-uav-counter.test`'s address-space
   mismatch in the UAV-counter + `ResourceDescriptorHeap` combination.
3. **~30 min-1 hour: continue individually triaging the remaining ~22
   of the 26 `check-hlsl-feme-vk` failures**, one at a time, via
   `run_test2.sh` (real `dxc`, matches the actual `check-hlsl-feme-vk`
   target -- see the methodology-trap note above for why this
   matters) -- still don't assume any two share a root cause without
   checking (`InterlockedAdd/CompareExchange/CompareStore/Exchange/Xor.32.test`,
   `DdxCoarse/DdyCoarse/ddx_fine/ddy_fine/fwidth.test`, `WaveActiveMax.test`,
   `Feature/*/GetDimensions.test` (matches H124m, `OpArrayLength` gap,
   already on the roadmap as deprioritized) are all still individually
   unconfirmed this session).
4. **~1 hour: file a roadmap row for the new
   `feme.cpu.resource.store.raw.i8` runtime gap** found via this
   session's `dEQP-VK.ssbo.layout.random.nested_structs*` spot-check,
   then fix it -- likely a small, self-contained addition mirroring
   H137's own `i64`/`v2i64` pattern in `FeMeRuntimeCPU.c`.
5. **~half a day: investigate `array_of_matrices.test`'s flaky
   unexpected-pass** (full-suite-only, not reproducible standalone) --
   don't remove its `XFAIL` until this is understood; likely needs
   `valgrind`/an uninitialized-read detector run inside the exact
   worker-parallel `llvm-lit` invocation the full suite uses.
6. **H124e** (large, unchanged for many sessions):
   `feme-cpu-simdize`/`feme-cpu-linearize`/`feme-cpu-wrap-entry`
   divergence-handling gaps -- still needs per-case triage, don't
   assume shared cause with any of the above.
7. **`shaderImageGatherExtended`** (large, carried over many sessions,
   still not filed as its own roadmap row): blocks every
   `dEQP-VK.glsl.texture_gather.*` CTS case. File a roadmap row before
   starting.
8. Lower priority, deferred 23+ sessions now:
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s
   pre-existing heap corruption.
