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

Can you work on L66 or other prerequisites blocking the L-series milestones?

The last session reported:

> With L67 now fully closed, the L-series' own still-open items are:
> - **L66(c)**: the `Dref`+`Grad` shadow-sampling intrinsic gap (no
>   `llvm.spv.resource.samplecmpgrad`-shaped intrinsic exists in
>   `IntrinsicsSPIRV.td` today) -- a genuinely bigger, cross-cutting scope
>   mirroring roadmap L52(b)'s own `Dref`+`Bias` gap, untouched again this
>   session.
> - **L66(d)**: `isSupportedOffset`'s remaining `Plain1D`/`Array1D`
>   restriction (now that `Plain3D`'s own share is fixed by this session's
>   L67(c) work) -- likely the next highest-value, lowest-risk target,
>   since it should be a small, mechanical repeat of this exact same
>   change against the two 1D shapes (both already have real sampled-image
>   infrastructure and a `createSample1D`/`createSample1DArray` builder
>   each; only the offset-width/operand-threading needs adding, mirroring
>   today's `Plain3D` work almost exactly, except with a 1-wide rather than
>   3-wide offset).
> - **L66(e)**: the newly-discovered `SPIRVResourceLoweringPass` crash when
>   two functions in one module each declare a resource handle at an
>   identical binding number for two different image shapes -- not yet
>   root-caused, needs its own investigation before it can be ruled in or
>   out as a real CTS-reachable multi-entry-point hazard.


Which seems like the right place to start.

> **L65's own re-run of the `shaderResourceMinLod` flip/measure/revert
> experiment gives the first fully-accurate, real-CTS-measured breakdown of what
> still blocks safely enabling this feature bit, superseding every
> `VulkanBuffer`-framed claim in L60/L61's own text (roadmap L64 already
> disproved that framing; this row gives the replacement)**, broken down here
> per this project's own established splitting precedent: ~~(a) **`Plain3D` has
> no ordinary sampled-image infrastructure of its own at all** -- no
> `createSample3D` exists (confirmed: zero references anywhere in
> `ImageCalls.cpp`/`.h`), so even a plain `texture(sampler3D, ...)` fails
> outright (`texture.sampler3d_{fixed,float}_fragment`, 0/8 Pass in a real
> re-run) -- a materially bigger prerequisite than any other shape's own gap
> here, on the same order as `Array2D`'s pre-L60(a) starting point, and should
> be scoped as its own follow-on row (ordinary sampling first,
> `Bias`/`MinLodClamp`/`Grad` only after) rather than attempted alongside
> anything else~~ (fixed: `classifySampledImage2DHandle` now recognizes a
> non-arrayed `SPIRVDim3D` handle (mapped to a new `ImageShape::Plain3D`,
> mirroring `classifyStorageImage2DHandle`'s existing `Arrayed` rejection for
> the same dimension), `hasOnlySupportedImageUses` gives it a 3-component
> `SampleCoordWidth`, and a new `createSample3D`/`ImageCallKind::Sample3D` plus
> a new `feme.cpu.image.sample.3d.v4f32` CPU-runtime entry point
> (`femeCpuImageSample3DV4F32`, backed by a real isotropic-only
> `femeRTPlanImplicitLod3D` implicit-LOD calculation and an 8-corner-trilinear
> `femeRTSampleLinear3D`, mirroring `Sample1D`'s own isotropic-only precedent --
> no CTS case exercises anisotropic filtering against a volume texture) provide
> real ordinary sampling: a genuine 3-component `(U, V, W)` coordinate, real
> screen-space-derivative-driven implicit LOD, and real trilinear/point mip
> filtering, deliberately scoped to ordinary sampling only -- still no
> `Bias`/`MinLodClamp`/`ConstOffset`/`Grad` operand, each filed as its own new
> roadmap L67 follow-on row below. New unit tests across all three touched
> phases (`ImageCallsTest.cpp`'s `createSample3D` round-trip,
> `SPIRVResourceLoweringTest.cpp`'s positive/negative `Plain3D` lowering
> coverage, `ImageSamplingTest.cpp`'s point/trilinear/inactive-lane runtime
> coverage) plus a new IR-lowering-phase lit test. `check-feme`: 2652/2711 pass
> (up from 2645), 0 fail, 59 unsupported. Real CTS, direct re-run of
> `texture.sampler3d_{fixed,float}_{fragment,vertex,compute}` (8 cases): 4/8
> Pass, up from 0/8 (the `_fragment`/`_vertex` cases, exactly as expected for
> this row's ordinary-sampling-only scope); the remaining 2 `_bias` Fails and 2
> `_compute` NotSupported are both out of this row's scope (`Bias` filed as
> L67(a); `_compute`'s failure is the same pre-existing, unrelated
> `VK_KHR_compute_shader_derivatives` gap other compute-stage sampling groups
> already hit). A broader `dEQP-VK.glsl.texture_functions.*.sampler3d_*` sweep
> (502 cases, every texture-function group against this one shape) confirms 8
> Pass total, 266 Fail, 228 NotSupported after this fix; since `Plain3D` had
> zero sampled-image infrastructure of any kind before this session (every such
> case either failed pipeline creation outright or was already `NotSupported`
> for an unrelated reason), all 8 of these passing cases -- `texture`'s own 4
> confirmed above plus 4 more from another ordinary-sampling-shaped group (e.g.
> `textureProj`, whose projective divide is typically resolved before an
> ordinary sample rather than needing its own dedicated SPIR-V opcode) -- are
> net-new passes with 0 regressions possible by construction.); (b)
> **integer-channel (`isampler`/`usampler`) filtered sampling** --
> `hasOnlySupportedImageUses`'s own `IsInteger` check already rejects any
> filtered sample outright for every shape, matching GLSL/HLSL's own unfiltered
> `texelFetch`-shaped integer-sampler intrinsics -- not a real gap, named here
> for completeness only, confirmed still the sole cause of every
> `isampler*`/`usampler*` fail in this session's own
> `textureclamp`/`texturegradclamp` re-run; (c) **`Dref`+`Grad` depth-comparison
> sampling** (`sampler{1d,1darray,2d,2darray,cube}shadow_fragment` under
> `texturegradclamp`, 5 real confirmed-failing cases this session's re-run
> measured) -- would need a new `llvm.spv.resource.samplecmpgrad`-shaped
> intrinsic, which does not exist in `IntrinsicsSPIRV.td` today (unlike `Grad`'s
> own non-comparison intrinsics L59 already consumes), a genuinely bigger
> cross-cutting scope mirroring L52(b)'s own `Dref`+`Bias` gap; (d)
> **`isSupportedOffset`'s pre-existing `Plain2D`-only `ConstOffset` restriction
> for an *ordinary* (non-`Dref`) sample** blocks `textureoffsetclamp` for every
> other shape regardless of `Bias`/`MinLodClamp` support -- confirmed via a real
> re-run: `sampler1d`/`sampler1darray`/`sampler2darray`/`sampler3d`'s own
> `Bias`+`MinLodClamp` combination all pass `textureclamp` (no offset) but fail
> `textureoffsetclamp` (with offset) identically, while `Plain2D`'s own
> identical combination passes both -- pre-existing, unrelated to `MinLod`
> itself (roadmap L33's own still-open scope, not a new gap this row
> introduces); (e) **a newly discovered `SPIRVResourceLoweringPass` crash**
> (roadmap L65's own aside) when two functions in one module each declare a
> resource handle at an identical binding number for two different image shapes
> -- a real use-after-free (`Instruction::eraseFromParent` deletes a `%samp`
> handle while a still-live sample call in the *other* function still references
> it), reproduced with a minimal ordinary-sample (non-`Grad`) repro, so
> unrelated to any of (a)-(d) above; not yet root-caused, needs its own
> investigation before it can be ruled in or out as a real CTS-reachable
> multi-entry-point hazard. Once (a), (c), and (d) are resolved (or confirmed
> out of scope), the flip experiment should be re-run once more before actually
> enabling the bit, since (b) is by design and (e) is orthogonal to sampling
> shape support.
