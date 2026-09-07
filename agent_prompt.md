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

> With L66(e) now closed, roadmap L66's own only remaining open sub-item
> is **L66(c)**: the `Dref`+`Grad` shadow-sampling intrinsic gap. No
> `llvm.spv.resource.samplecmpgrad`-shaped intrinsic exists in
> `IntrinsicsSPIRV.td` today (unlike `Grad`'s own non-comparison
> intrinsics, which roadmap L59 already consumes) -- this needs:
>
> 1. A new SPIR-V-to-LLVM raising pattern recognizing `OpImageSampleDrefExplicitLod`/
>    `OpImageSampleDrefImplicitLod` with a `Grad` image operand (today's
>    `Dref`-raising code presumably only recognizes `Lod`/`Bias`/no-operand
>    variants -- needs checking).
> 2. A new intrinsic declaration in `IntrinsicsSPIRV.td` (mirroring
>    `llvm.spv.resource.samplegrad`'s own non-`Dref` shape, but with an
>    added `Dref` operand, mirroring how the existing non-`Grad`
>    `llvm.spv.resource.samplecmp`/`.samplecmp.clamp` already add `Dref` to
>    the ordinary `sample`/`sample.clamp` shape).
> 3. `isDrefSampleIntrinsic`/`hasOnlySupportedImageUses`'s own `Dref`
>    handling in `SPIRVResourceLowering.cpp` extended to recognize this new
>    intrinsic shape, plus real lowering to a runtime entry point (likely a
>    new `femeCpuImageSampleCmpGradXXX` family, one per already-supported
>    `Dref`-capable shape: `Plain1D`/`Array1D`/`Plain2D`/`Array2D`/`Cube`/
>    `CubeArray`).
> 4. This is a genuinely bigger, more cross-cutting scope than L66(d)/(e)
>    were -- likely deserves its own further breakdown into per-shape rows
>    (mirroring how L67 broke `Plain3D`'s own `Bias`/`Grad`/`ConstOffset`
>    into separate rows) rather than being attempted as one single change,
>    once someone begins investigating it in earnest.
>
> Once L66(c) is resolved (or confirmed out of scope), roadmap L66 will be
> fully complete, and the `shaderResourceMinLod` flip/measure/revert
> experiment (L65's own scope) can finally be re-run once more before
> actually enabling the bit for real.

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
> cross-cutting scope mirroring L52(b)'s own `Dref`+`Bias` gap; ~~(d)
> **`isSupportedOffset`'s pre-existing `Plain2D`-only `ConstOffset` restriction
> for an *ordinary* (non-`Dref`) sample** blocks `textureoffsetclamp` for every
> other shape regardless of `Bias`/`MinLodClamp` support -- confirmed via a real
> re-run: `sampler1d`/`sampler1darray`/`sampler2darray`/`sampler3d`'s own
> `Bias`+`MinLodClamp` combination all pass `textureclamp` (no offset) but fail
> `textureoffsetclamp` (with offset) identically, while `Plain2D`'s own
> identical combination passes both -- pre-existing, unrelated to `MinLod`
> itself (roadmap L33's own still-open scope, not a new gap this row
> introduces);~~ (fixed, filed as roadmap L66(d): `Plain3D`'s own share of this
> restriction was fixed by roadmap L67(c); this final `Plain1D`/`Array1D` share
> confirms both shapes' own ordinary-sample `ConstOffset` is a bare scalar
> `i32`, never a vector -- a materially different shape than every other
> sample-capable shape's own vector-typed offset (confirmed via a real `deqp-vk`
> SPIR-V capture of both `sampler1d`/`sampler1darray`'s own `textureOffset()`
> cases), since SPIR-V's own `ConstOffset` dimensionality tracks the image's
> real dimension count (1 for a 1D image) excluding any array layer, the same
> "+1" carve-out `GradDerivativeWidth` (roadmap L64) already applies to a `Grad`
> derivative. `isSupportedOffset` gained a new `AllowPlain1DArray1D` parameter
> (mirroring the existing `AllowArray2D` pattern) accepting this scalar case
> only for an ordinary (non-`Dref`) sample's own caller; along the way this
> surfaced a real pre-existing bug where the depth-comparison (`Dref`) sample
> path shares this same function and would have incorrectly started accepting a
> nonzero offset too had the new branch not been scoped behind this flag -- the
> `Dref` call site passes no such flag and continues to require the trivial
> always-zero case for these two shapes, since no real CTS case exercises a
> nonzero `ConstOffset` against a depth-comparison `Plain1D`/`Array1D` sample.
> `lowerImageAccesses`'s `Plain1D`/`Array1D` branch now extracts a real `Offset`
> value and threads it through both `createSample1D`/`createSample1DArray` (each
> widened with a new `Offset` operand, reusing the existing `OffsetX`/`OffsetY`
> fields rather than adding a dedicated one, since there is only one component
> here) and `femeCpuImageSample1DV4F32`/`femeCpuImageSample1DArrayV4F32`'s own
> runtime entry points (replacing a previously hardcoded `/*OffsetX=*/0` literal
> at each `femeRTSampleFiltered1D` call site). New unit-test coverage across all
> three touched phases: `ImageCallsTest.cpp`'s
> `MatchesSample1DCallWithBiasAndMinLodClamp`/`MatchesSample1DArrayCallWithBiasAndMinLodClamp`
> now assert a real `Offset` value; `SPIRVResourceLoweringTest.cpp` gained two
> new positive tests
> (`LowersSampleConstOffsetToPlain1D`/`LowersSampleConstOffsetToArray1D`)
> confirming a bare scalar `i32` offset lowers correctly, plus fixes to several
> pre-existing tests that had used a `<1 x i32> zeroinitializer` vector for a
> *zero* offset against these two shapes (now `i32 0`, matching the real scalar
> ABI); `ImageSamplingTest.cpp` gained two new positive correctness tests
> (`Sample1DHonorsNonZeroTexelOffset`/`Sample1DArrayHonorsNonZeroTexelOffset`,
> mirroring `Sample3DHonorsNonZeroTexelOffset`'s own precedent) confirming a
> real nonzero offset shifts the sampled texel by the expected amount, and
> confirming `Array1D`'s offset only affects `U`, never `ArrayLayer`. A new lit
> test (`spirv-resource-lowering-image-sample-1d-offset.ll`) confirms both
> shapes' `ConstOffset` lowers successfully; this fix also required correcting a
> pre-existing lit test (`spirv-resource-lowering-image-samplegrad-1d.ll`) that
> had used the same `<1 x i32>` vector shape for `Grad`'s own always-zero
> trailing offset operand -- since that intrinsic's `isSupportedOffset` check is
> shared with the ordinary-sample path, the newly scalar-only check correctly
> rejected that file's now-outdated vector shape, so it was updated to `i32 0`
> to match reality (not a functional regression, purely a pre-existing test
> artifact). `check-feme`: 2662/2721 pass, 0 fail, 59 unsupported (up from
> 2661/2720 pre-fix baseline this session's own tree started from, +1 net new
> lit test plus this row's own new unit tests). Real CTS, a full
> `dEQP-VK.glsl.texture_functions.textureoffset.*.sampler1d*`/`sampler1darray*`
> re-run across all 5 wrap modes (120 non-`isampler`/`usampler` cases): 60 Pass
> (up from 0, exactly the ordinary `fixed`/`float` `_fragment`/`_vertex` cases
> across all 5 wrap modes -- every one of these previously failed
> `vkCreateGraphicsPipelines` outright), 30 Fail (the pre-existing, unrelated
> `sampler1d{,array}shadow` `Dref` cases, correctly still rejected since the
> `Dref` path's own `isSupportedOffset` call site is deliberately unaffected by
> this fix), 30 NotSupported (the pre-existing, unrelated `_compute`-stage
> `VK_KHR_compute_shader_derivatives` gap other sampling groups already hit) --
> no regressions. A broader `dEQP-VK.glsl.texture_functions.*.sampler1d*` sweep
> (1355 cases, every texture-function group against both shapes) confirms 160
> Pass, 572 Fail, 623 NotSupported, completing cleanly with no crashes; since
> this fix is purely additive (only a previously-rejected offset case now
> lowers), no regression is possible by construction. No
> `Vulkan14FeatureInventory.md`/`VulkanExtensionInventory.md` update is needed:
> `ConstOffset` is a core SPIR-V image operand with no gating Vulkan feature or
> extension.)  ~~(e) **a newly discovered `SPIRVResourceLoweringPass` crash**
> (roadmap L65's own aside) when two functions in one module each declare a
> resource handle at an identical binding number for two different image shapes
> -- a real use-after-free (`Instruction::eraseFromParent` deletes a `%samp`
> handle while a still-live sample call in the *other* function still references
> it), reproduced with a minimal ordinary-sample (non-`Grad`) repro, so
> unrelated to any of (a)-(d) above; not yet root-caused, needs its own
> investigation before it can be ruled in or out as a real CTS-reachable
> multi-entry-point hazard.~~ (root-caused and fixed: the crash is not actually
> about "two functions" specifically -- it is `lowerImageAccesses`'s own
> trailing cleanup loop unconditionally erasing every handle in its own
> `HeapIndices` map, without checking whether all of that handle's *own* users
> were actually rewritten away first. A sample call is only ever rewritten (and
> its own image/sampler handles' use-count decremented) from its *image*
> handle's own side (`CI->getArgOperand(0) != Handle` skips the sampler side
> deliberately, to avoid a double rewrite); if that image handle's own (set,
> binding) identity conflicts with a *different* declaration anywhere else in
> the module (`run`'s own per-handle `Entry.Conflicting` check, not
> per-function), the image handle -- and therefore the whole sample call reached
> through it -- is correctly excluded from `HeapIndices` and left entirely
> unrewritten. But if the *paired sampler* handle's own identity happens not to
> conflict (a real, plausible shape: one shared sampler binding used
> consistently by multiple entry points, each sampling a *different* shape of
> image at another, conflicting binding), that sampler handle is still accepted
> into `HeapIndices` on its own -- and the old code erased it unconditionally at
> the end regardless, even though the still-unrewritten sample call was still a
> live user of it. Fixed with a one-line guard (`if (Handle->use_empty())`)
> before erasing each handle, leaving any handle that still has real users
> (because its own paired handle was excluded elsewhere) alone -- exactly the
> same "left un-rewritten, for `checkSupportedRaisedOps` to reject" outcome a
> conflicting *buffer* handle already gets, just extended to cover this
> image/sampler pairing's own extra cross-handle dependency. Confirmed via a
> minimal repro (two functions, one `Plain2D` image handle and one `Plain3D`
> image handle at the same (set, binding), sharing one single non-conflicting
> sampler binding between them) that reliably crashed `feme-opt` before this fix
> and no longer does after it. New test coverage at both the IR-lowering-pass
> phase (`SPIRVResourceLoweringTest.cpp`'s new
> `LeavesConflictingImageShapeWithSharedSamplerBindingAlone`, asserting the pass
> no longer crashes and both functions' sample calls remain correctly
> unrewritten while each function's own non-conflicting sampler handle still
> contributes real bound-resource metadata) and a mirrored new lit test
> (`spirv-resource-lowering-conflicting-image-shape.ll`, extending
> `spirv-resource-lowering-conflicting.ll`'s own existing buffer-conflict
> precedent to this image-shape-conflict shape). `check-feme`: 2664/2723 pass, 0
> fail, 59 unsupported (+2 net new tests, 0 regressions). Real CTS: no direct
> real CTS case was found that exercises this exact cross-handle scenario (a
> single SPIR-V module with two entry points, one shared sampler binding, and
> two conflicting image bindings of different shapes is an unusual authoring
> pattern this session's own sweeps did not surface) -- this remains a defensive
> robustness fix for a real, confirmed-reproducible crash rather than one
> directly observed unblocking a specific failing CTS case. A
> `dEQP-VK.glsl.texture_functions.textureoffset.*.sampler1d*` re-run (120 cases,
> confirming roadmap L66(d) itself is unaffected) and two broad regression
> sweeps -- `dEQP-VK.glsl.texture_functions.texture.*` (208 cases) and the full
> `dEQP-VK.image.*` group (49,229 cases) -- all complete cleanly with no crashes
> and identical Pass/Fail/NotSupported counts to before this fix, confirming no
> regression anywhere.)  Once (a), (c), and (d) are resolved (or confirmed out
> of scope), the flip experiment should be re-run once more before actually
> enabling the bit, since (b) is by design and (e) is orthogonal to sampling
> shape support.
