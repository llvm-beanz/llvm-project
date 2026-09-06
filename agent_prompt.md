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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L52 or other prerequisites blocking the L-series milestones?

> **L50's own (a)-(f) breakdown leaves 4 sub-items open after this session's (d)
> fix and L51's own further split-out of (f)**: (a) **`Plain1D`/`Array1D` shadow
> sampling** (`sampler1d{,array}shadow_*`) has no ordinary (non-comparison)
> sampled-image path on the CPU target at all yet for either shape -- needs its
> own new 1D-sampling infrastructure
> (`ImageCallKind::Sample1D`/`Array1D`-equivalents,
> `createSample1D`/`createSample1DArray`, matching runtime entry points)
> *before* a `SampleCmp1D`/`SampleCmpArray1D` counterpart is even possible, a
> materially bigger prerequisite than any other item in this row; (b) **a `Bias`
> image operand** (`sampler{2d,cube}shadow_bias_fragment` and siblings, 5 real
> confirmed-failing CTS cases per this session's own re-run) fails at
> `ConvertSPIRVToLLVMPass` legalization itself, before
> `SPIRVResourceLowering.cpp` ever sees it --
> `ImageSampleDrefImplicitLodPattern`'s own `SupportedMask`
> (`SPIRVToLLVMPatterns.cpp`) only allows `ConstOffset`/`MinLod`, not `Bias`,
> and no `llvm.spv.resource.samplecmp*` intrinsic form threads an explicit bias
> through today, unlike the ordinary
> `spv_resource_samplebias`/`.samplebias_clamp` family already handled for a
> non-dref sample -- needs its own new intrinsic-lowering design (a genuinely
> bigger, cross-cutting scope touching real LLVM SPIR-V backend intrinsic
> definitions, not just feme-internal code, per this session's own investigation
> into why (b) was deferred in favor of (d)); (c) **`samplecmp_clamp`'s own
> trailing `MinLod` clamp operand** (not yet confirmed present in any real
> failing CTS case measured so far) needs each `createSampleCmp*` builder
> extended with a `MinLodClamp` parameter, mirroring `createSample2D`'s own
> roadmap L26 precedent; (e) **the entirely separate LOD-query intrinsics**
> (`spv_resource_calculate_lod`/`.calculate_lod_unclamped`, the 190-case
> `texturequerylod` group, confirmed unaffected at 0/190 by every L48/L50 re-run
> so far) have no CPU-lowering consumer at all on either the SPIR-V or DXIL
> frontend and need a genuinely new runtime design: a real screen-space
> coordinate derivative (`dFdx`/`dFdy`) threaded through to a query call,
> needing its own design pass to confirm where a per-invocation derivative is
> (or could be made) available in this target's per-lane execution model.
> (`Cube`/`CubeArray`'s own real, nonzero `ConstOffset` sub-item (d) named in
> L50's own original report does not exist -- SPIR-V forbids `ConstOffset`
> against `Dim::Cube` outright, confirmed again by this session's own
> `isSupportedOffset` review, so there is no remaining `(d)`-shaped gap to file
> here; `(f)`'s `CubeArray`-shadow rendering bug is already its own row,
> **L51**, not re-listed here.) Each of (a), (b), (c), (e) should be scoped and
> fixed as its own small, independently-committed, independently-CTS-measured
> row rather than attempted together, per this project's own established
> (partially fixed: sub-item (a)'s ordinary, non-comparison Plain1D/Array1D
> sampling is done and tested -- this was the highest-value, most tractable
> target this session (8 real confirmed-failing CTS cases,
> sampler1d{,array}_{fixed,float}_{fragment,vertex}, versus (c)'s zero confirmed
> cases and (b)/(e)'s much larger cross-cutting scope, per this session's own
> real deqp-vk probe). New ImageCallKind::Sample1D/Sample1DArray entries
> (ImageCalls.h/.cpp) mirror Sample2DArray's own simpler shape (no screen-space
> derivatives/ConstOffset/MinLod clamp, unlike Sample2D's richer roadmap H7i/L26
> additions) rather than attempting a richer first pass; new
> createSample1D/createSample1DArray builders and
> femeCpuImageSample1DV4F32/Sample1DArrayV4F32 runtime entry points
> (FeMeRuntimeCPU.c, new femeRTSamplePoint1D/Linear1D/Filtered1D filtering
> helpers reusing every existing dimension-agnostic addressing/mip helper) round
> out the new path. classifySampledImage2DHandle (SPIRVResourceLowering.cpp) now
> recognizes Dim::1D (previously never checked at all, unlike the storage-image
> classifier, which already did) for both plain and arrayed shapes;
> hasOnlySupportedImageUses's SampleCoordWidth formula gained
> Plain1D=1/Array1D=2 cases; lowerImageAccesses's ordinary-sample switch gained
> an early special case for Plain1D's bare-scalar coordinate (per SPIR-V's own
> scalar, non-vector-wrapped convention for a 1-component coordinate -- see
> isCoordN's own comment) and Array1D's 2-component (u, layer) vector, ahead of
> the generic CreateExtractElement(Coord, 0/1) every other shape shares (which
> would otherwise crash on Plain1D's scalar). The pre-existing
> dref/depth-comparison rejection of Plain1D/Array1D
> (hasOnlySupportedImageUses's own IsInteger || Shape == ImageShape::Plain1D ||
> Shape == ImageShape::Array1D branch) is deliberately left untouched --
> SampleCmp1D/SampleCmpArray1D (the 4 real
> sampler1d{,array}shadow_{fragment,vertex} cases) remain unstarted follow-on
> work, filed as its own row, L54, below, per this project's own established
> splitting precedent, rather than attempted together with the ordinary-sampling
> infrastructure this session added. Also deliberately left unstarted this
> session (out of scope, no real CTS case exercises it): a real 1D
> OpImageFetch/texelFetch(sampler1D, ...) path -- hasOnlySupportedImageUses's
> own fetch-shape branch now explicitly rejects Plain1D/Array1D there too,
> rather than silently miscomputing a fetch coordinate width, until a real CTS
> case motivates adding it. New tests: 3 SPIRVResourceLoweringTest unit tests (2
> positive, Plain1D/Array1D classification+lowering; 1 negative, confirming a
> Plain1D OpImageFetch is still left unlowered), 4 ImageSamplingTest runtime
> unit tests (linear/point filtering, array-layer selection, inactive-lane
> masking), reusing the already-existing makeImage1D/makeImage1DArray helpers
> (roadmap H19c/H19e). ninja check-feme: 2584/2643 discovered, 59 pre-existing
> Unsupported, 0 Failed (up by exactly the 7 new tests this phase adds); no
> regressions. Real
> dEQP-VK.glsl.texture_functions.texture.sampler1d{,array}_{fixed,float}_{fragment,vertex}
> re-run: 8/8 now Pass, up from 0/8 before this fix; a broader sampler1d* sweep
> (24 cases) confirms exactly the expected side-effect-free outcome: 8 Pass
> (this fix), 10 Fail (unchanged -- 4 _bias_fragment blocked by sub-item (b), 4
> *shadow_{fragment,vertex} blocked by the still-open
> SampleCmp1D/SampleCmpArray1D counterpart filed as L54), 6 NotSupported
> (unchanged -- _compute/shadow _compute, an unrelated
> VK_KHR_compute_shader_derivatives gap). FeMeGraphicsDesign.md/FeMeCPUDesign.md
> reviewed: no deviation to record (neither ever scoped sampled-image support to
> 2D/Cube shapes only in a way this widening contradicts).
> Vulkan14FeatureInventory.md/VulkanExtensionInventory.md reviewed: no change
> needed (internal CPU-lowering plumbing only, no new feature/extension surface
> advertised). Sub-items (b) Bias and (c) samplecmp_clamp's MinLod operand
> remain open under this same row; the deferred SampleCmp1D/SampleCmpArray1D
> counterpart is re-filed as L54, below, and sub-item (e)'s LOD-query intrinsics
> are now done and re-filed as L57, below, per this project's own established
> L26->L33/L45->L47/L46->L48/L48->L50/L50->L51 precedent. UPDATE: a later
> session investigating this same sub-item (b) found and fixed a distinct, more
> tractable gap -- ordinary (non-`Dref`) `Bias` sampling against
> `Plain2D`/`Cube` had zero recognition in `SPIRVResourceLowering.cpp` at all
> (not just the literal `Dref`+`Bias` combination this sub-item names), despite
> already being fully legalized upstream by `ImageSampleImplicitLodPattern`.
> That fix is filed and measured as its own row, **L58**, below, per this
> project's own established splitting precedent; sub-item (b)'s own literal
> `Dref`+`Bias` gap (needing a new LLVM core intrinsic before
> `SPIRVResourceLowering.cpp` can even see it) remains open and unstarted.)
