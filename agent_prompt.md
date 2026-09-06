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

Can you work on L60 or other prerequisites blocking the L-series milestones?

The last session reported:

> Filed roadmap L66(a)-(e) as the real, CTS-measured breakdown of everything
> still blocking `shaderResourceMinLod`:
> - (a) `Plain3D`'s total lack of ordinary sampled-image infrastructure -- by
>   far the biggest of the five, comparable in scope to what `Array2D` needed
>   before L60(a)'s own predecessor work, and the natural next target since it
>   blocks the *most* real cases across every one of `textureclamp`/
>   `texturegradclamp`/`textureoffsetclamp`.
> - (b) integer-sampler exclusion -- correctly by-design, named for
>   completeness, not itself actionable.
> - (c) `Dref`+`Grad` shadow sampling -- needs a new intrinsic, not yet
>   designed.
> - (d) `isSupportedOffset`'s `Plain2D`-only `ConstOffset` restriction --
>   pre-existing, unrelated, already implicitly roadmap L33's scope; just
>   newly confirmed as the actual root cause of `textureoffsetclamp`'s broad
>   failures rather than anything `MinLod`-specific.
> - (e) the cross-function same-binding crash discovered above.
>
> Given the CTS-measured impact, `Plain3D` (L66(a)) looks like the highest-
> value next target for a future session, followed by the offset restriction
> (L66(d)) given how many cases it currently blocks across every shape it
> touches.


Which seems like the right place to start.

> **L59's own `Plain2D`/`Cube`, `fixed`/`float`, `fragment`/`vertex`
> explicit-`Grad` slice leaves several distinct, independently-sized gaps still
> open, broken down here rather than re-attempted together, per this project's
> own established splitting precedent**: (a) **`Array2D`/`CubeArray` `Grad`
> sampling** -- `createSample2DArray`/`createSampleCubeArray` have no
> `Bias`/`Grad`-shaped derivative or clamp operand of their own today (same
> restriction `HasBias`'s own roadmap L58 scope already hit), needing those
> builders extended first (partially fixed: `CubeArray`'s own
> `Bias`/`MinLodClamp`/`Grad` support is now done and tested this session,
> reusing `createSampleCubeArray`'s existing roadmap L56 six-operand
> screen-space-derivative infrastructure -- `hasOnlySupportedImageUses`'s
> `HasMinLodClamp`/`HasBias`/`HasGrad` shape checks now also allow `CubeArray`,
> and `lowerImageAccesses`'s `CubeArray` case extracts a real `MinLodClamp`
> operand and, when a `Grad` image operand is present, the real `dPdx`/`dPdy`
> direction derivatives, mirroring `Cube`'s own handling exactly. Real CTS
> re-run of the non-`shaderResourceMinLod`-gated `Bias` cases confirms this:
> `dEQP-VK.glsl.texture_functions.texture.samplercubearray_bias_{fixed,float}_fragment`
> are now 2/2 Pass, up from 0/2; a broader `texture.*bias*` sweep (50 cases)
> confirms 6 Pass (up from 4, the pre-existing L58 `Plain2D`/`Cube` passes), 26
> Fail (unchanged in aggregate -- `Array2D`'s own `Bias` cases still fail, since
> `createSample2DArray` itself was not touched this session), 18 NotSupported
> (unchanged). `MinLodClamp`'s own `CubeArray` counterpart remains unconfirmed
> by real CTS -- every `textureclamp`/`texturegradclamp` case is still gated
> behind the still-disabled `shaderResourceMinLod` feature bit regardless of
> shape, so this sub-item's fix is a prerequisite for safely flipping that bit
> on, not something with its own currently-reachable passing case (a repeat
> `shaderResourceMinLod`-enabled experiment this session,
> `feme/lib/Vulkan/PhysicalDeviceInfo.cpp` temporarily set to `VK_TRUE` then
> reverted, confirmed `Array2D`'s own remaining gap is what still blocks safely
> enabling it: `textureclamp`/`textureoffsetclamp`/`texturegradclamp` each still
> regress a majority of their cases from `NotSupported` to real `Fail` today).
> `CubeArray`'s own `Grad` counterpart is separately blocked in the
> `texturegrad` CTS group by a distinct, pre-existing, unrelated gap confirmed
> present both before and after this session's fix (unaffected by it either
> way), reported as `llvm.spv.resource.handlefrombinding` rejecting a
> register-bound `VulkanBuffer` resource handle -- out of scope for this row,
> not yet filed as its own line. **SUPERSEDED by roadmap L64**: that
> `VulkanBuffer` framing was a misattribution. The real cause was an over-strict
> `Grad` derivative-width check rejecting the arrayed sample itself; the
> `VulkanBuffer` handle named in the diagnostic was an innocent scale/bias
> uniform block that merely happened to be reported first once the rejected
> sample left the whole function unlowered. Fixed in L64; both shapes'
> `texturegrad` cases now pass. `Array2D`'s own `Bias`/`MinLodClamp`/`Grad`
> support remains entirely unstarted -- unlike `CubeArray`,
> `createSample2DArray` has zero derivative-operand infrastructure to build on
> today, a materially bigger prerequisite than `CubeArray`'s was, and should be
> scoped as its own follow-on row rather than attempted together with anything
> else. UPDATE: a later session added this missing derivative-operand
> infrastructure to `createSample2DArray` itself (extending its signature from
> 12 to 18 args with real `DUdX`/`DUdY`/`DVdX`/`DVdY`/`Bias`/`MinLodClamp`
> operands, deliberately still without an `OffsetX`/`OffsetY` `ConstOffset`
> pair, per `isSupportedOffset`'s own doc comment scoping ordinary-sample
> `ConstOffset` against `Array2D` to roadmap L33, not this row), and rewrote
> `femeCpuImageSample2DArrayV4F32` from its previous always-single-tap
> `femeRTComputeClampedLod`-only body to the same `femeRTPlanImplicitLod`-based
> anisotropic multi-tap implementation `femeCpuImageSample2DV4F32` already used,
> reading a fixed array layer for every tap -- a genuine behavioral upgrade
> (`Array2D` sampling now gets real anisotropic filtering when a sampler enables
> it, which it never did before). `hasOnlySupportedImageUses`'s three shape
> checks and `lowerImageAccesses`'s `Array2D` case were widened identically to
> `CubeArray`'s own prior fix, reusing the same
> `getOrSynthesizeSample2DDerivatives` helper `Plain2D` already used (an arrayed
> sample's face-local (U, V) coordinate differentiates identically). Real CTS
> re-run confirms both `Array2D`'s own
> `sampler2darray_bias_{fixed,float}_fragment` cases are now 2/2 Pass, up from
> 0/2 before this fix; the broader `texture.*bias*` sweep (50 cases) now shows 8
> Pass total (up from 6 after the `CubeArray` fix), 24 Fail (down from 26, by
> exactly these 2 newly-passing cases -- every remaining failure is an
> integer-sampler, shadow/`Dref`, or `Plain1D`/`Plain3D` case this row does not
> touch), 18 NotSupported (unchanged); a
> `dEQP-VK.glsl.texture_functions.*.sampler2darray_*` sweep (312 cases, covering
> every texture-function group against this one shape) confirms a strictly
> monotonic before/after improvement with a real `git stash`-based before/after
> comparison: 10 Pass after this fix vs. 8 Pass before (up by exactly these same
> 2 cases), 156 Fail after vs. 158 before (down by exactly 2), 146 NotSupported
> unchanged in both -- no regressions anywhere in this shape's own CTS
> footprint. `Array2D`'s own `Grad` counterpart remains blocked in the
> `texturegrad` CTS group by the same gap already confirmed blocking
> `CubeArray`'s own `Grad` path above (**since root-caused and fixed by roadmap
> L64**, which showed it was never a `VulkanBuffer` gap at all but an
> over-strict `Grad` derivative-width check) (confirmed via
> `FEME_VULKAN_LOG_CREATION_ERRORS=1`: `vkCreateGraphicsPipelines` fails before
> `Grad` lowering is ever reached, for both `fragment`/`vertex` stages and the
> `compute` stage alike) -- out of scope for this row, same as `CubeArray`'s.
> `Array2D`'s own `MinLodClamp` counterpart remains equally unconfirmable by
> real CTS today, gated behind the same disabled `shaderResourceMinLod` feature
> bit as every other shape's clamp variant (`texturegradclamp.sampler2darray_*`
> confirmed `NotSupported` for this exact reason this session). With this fix,
> sub-item (a) is now fully done for both `CubeArray` and `Array2D`'s own
> `Bias`/`MinLodClamp` (mod `shaderResourceMinLod` still being globally
> disabled) and `Grad`-operand-lowering halves; only the shared gap misfiled as
> a `VulkanBuffer` handle problem stood between either shape and a passing
> `texturegrad` case; roadmap L64 has since root-caused it to an over-strict
> `Grad` derivative-width check and fixed it, so both shapes' `texturegrad`
> cases now pass.); (b) **integer-channel (`isampler`/`usampler`) `Grad`
> sampling** -- `hasOnlySupportedImageUses` already rejects any filtered sample
> (`Grad` included) over an integer format outright (`IsInteger` check),
> matching every other filtered-sample restriction, so this is arguably not a
> real gap (GLSL/HLSL's own integer-sampler intrinsics are unfiltered
> `texelFetch`-shaped, not `Grad`-shaped, in the first place) but is named here
> for completeness, per this session's own real CTS sweep showing
> `isampler*`/`usampler*` `texturegrad` cases still failing; (c)
> **`compute`-stage `Grad` sampling** -- confirmed by this session's own re-run
> to still fail identically to `fragment`/`vertex`'s own pre-fix failure, but
> for a distinct, unrelated reason (`vkCreateComputePipelines` itself fails, not
> a `Grad`-specific legalization gap) -- likely the same
> `VK_KHR_compute_shader_derivatives`-shaped gap prior sessions already
> identified blocking other compute-stage sampling groups, not a new one this
> row introduces; (d) **`Grad`+`MinLod` clamp**
> (`texturegradclamp`/`textureoffsetgradclamp`, gated by the still-disabled
> `shaderResourceMinLod` feature bit L52 first found) -- L59's own
> `ImageSampleGradPattern`/`SPIRVResourceLowering.cpp` changes already thread a
> real `MinLod` clamp through for `Plain2D`/`Cube` (mirroring `HasMinLodClamp`'s
> existing `sample.clamp`/`samplebias.clamp` precedent), so this sub-item may
> already be functionally correct -- it remains unconfirmed by real CTS only
> because `shaderResourceMinLod` is not yet advertised as `VK_TRUE` (flipping it
> also requires the `textureclamp`/`textureoffsetclamp` `Bias`+`MinLod` subset
> L58 already covers to be re-confirmed together, since Vulkan features are
> monolithic on/off switches, not per-test-group); (e) **`Dref`+`Grad`
> depth-comparison sampling** (a `sampler2DShadow`/`samplerCubeShadow` variant
> of `textureGrad()`, not yet confirmed present in the CTS corpus by any
> session's own probe so far -- needs a dedicated case-listing sweep to confirm
> real coverage exists before scoping a fix) -- would need its own new
> `llvm.spv.resource.samplecmpgrad`-shaped intrinsic (does not appear to exist
> in `IntrinsicsSPIRV.td` today, unlike `Grad`'s own non-comparison intrinsics
> L59 already consumes), a genuinely bigger, cross-cutting scope mirroring L52
> sub-item (b)'s own `Dref`+`Bias` gap; (f) **sparse-residency `Grad` sampling**
> (`sparse_sampler2d_*`/`sparse_samplercube_*`, the
> `SparseShaderTextureFunctionInstance` CTS variant) -- not yet investigated by
> any session's own probe; likely follows the same residency-feedback
> infrastructure other sparse sampling already uses, but unconfirmed. Each of
> (a)-(f) should be scoped and fixed (or confirmed already fixed, for (d)) as
> its own small, independently-committed, independently-CTS-measured row rather
> than attempted together.
