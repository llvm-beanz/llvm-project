---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L84 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`Basic/Matrix`'s own `matrix_groupthread_swizzle_{one,zero}_based.test`
> cases still fail a real numeric `BufferExact` mismatch, split out of L83's own
> closing investigation this session once the unrelated `RowMajor`
> storage-layout bug those two cases were originally filed alongside was fixed
> and confirmed not to cover them**: both cases dispatch `numthreads(4,1,1)` (4
> threads, one per `SV_GroupIndex` value 0..3), each writing exactly one
> row/column of a plain `Function`-storage local `int4x4` matrix (via a
> `switch(GI)` selecting which swizzle-group to assign) from `In[GI]`, then
> reading the same row/column back and scattering it into `RowOut`/`ColOut` at a
> `GI`-dependent offset. Real re-run (`check-hlsl-vk-basic-matrix`) shows every
> output element reads back as `1` (`In[0]`'s own value) instead of the expected
> per-thread `[1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4]`/`[1,2,3,4, 1,2,3,4, 1,2,3,4,
> 1,2,3,4]` -- i.e. every thread's own computation behaves as if it ran with
> thread 0's own `GI`/`In[GI]` value, not its actual one. A real IR reduction
> (`dxc -fvk-use-dx-layout -spirv` + `spirv-dis` on a minimal repro isolating
> just this shape) confirms `dxc` itself compiles this correctly: the imported
> SPIR-V's own two nested `spirv.Switch`es on `%2` (the real, per-invocation
> `LocalInvocationIndex`-loaded value) each have 4 real, distinct case arms
> feeding real per-lane-varying block-argument phis, with no compile-time
> constant-folding opportunity visible at this level -- strongly suggesting the
> bug is introduced later, most likely a uniformity-analysis misclassification
> (this project's own `feme::cpu::WaveTTIImpl`/`UniformityInfo`,
> `feme/lib/Analysis/CPU/WaveUniformity.cpp`) wrongly treating the
> `LocalInvocationIndex`-derived switch condition (or one of its own
> phi-recombined results) as uniform, causing
> `feme::cpu::SIMDizePass`/`LinearizePass` to broadcast lane 0's own value to
> every lane instead of correctly widening per-lane -- mirroring the exact shape
> of prior uniformity-classification bugs this project's own H-track already
> found and fixed (e.g. roadmap L43's `AtomicRMWInst` case), but for a `Switch`
> on a builtin-loaded, not yet independently confirmed. Needs: (1) a real
> captured pre-`SIMDize`/pre-`Linearize` IR dump of this exact repro (the same
> env-gated `FEME_DEBUG_DUMP_PIPELINE_STAGE_IR` technique documented in
> L41-L45's own rows) to confirm exactly which value/branch is misclassified,
> and (2) a fix most likely in `WaveUniformity.cpp`'s own
> `getValueUniformity`/`isDivergentAtDef` logic, following the L43 precedent,
> rather than anywhere in `SPIRVToLLVMPatterns.cpp` (this row is unrelated to
> L83's own storage-layout scope, despite sharing a test file)
