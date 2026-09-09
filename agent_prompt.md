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

Can you close out L60 from the roadmap or other prerequisites blocking the
L-series milestones?

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
> than attempted together. UPDATE: this session re-measured every remaining
> sub-item now that roadmap L66 has permanently enabled `shaderResourceMinLod`.
> (d) **`Grad`+`MinLod` clamp** is confirmed fixed: a real `texturegradclamp`
> re-run (52 cases) shows 19 Pass, 19 NotSupported, and all 14 remaining `Fail`s
> are exactly the by-design `isampler*`/`usampler*` filtered-integer-sampling
> exclusion, none newly broken. (e) **`Dref`+`Grad` depth-comparison sampling**
> is confirmed fixed for every shape (closed piecemeal by roadmap
> L66(c)/L66(f)-(i)): a real `texturegrad.*shadow*` re-run (24 cases) shows 10
> Pass, 9 NotSupported, and all 5 remaining `Fail`s are `_compute`-stage cases
> -- confirmed the same gap as sub-item (c) below, not a new one; a broader
> `texturegradoffset.*shadow*` sweep (90 cases, every wrap mode) shows 40 Pass,
> 30 NotSupported, and all 20 remaining `Fail`s are likewise exclusively
> `_compute`-stage. (f) **sparse-residency `Grad` sampling** is now investigated
> for the first time: a real sweep of all 76 `texturegrad{,offset}*.sparse_*`
> cases reports 76/76 `NotSupported` ("Format not supported"), confirmed the
> same pre-existing, already-tracked sparse-residency infrastructure gap roadmap
> H27 already scopes (`sparseResidencyImage2D`/`shaderResourceResidency` and
> friends remain unadvertised) -- not a `Grad`-specific gap of its own, and out
> of this row's own scope. (c) **`compute`-stage `Grad` sampling** is confirmed
> still blocked, but by a distinct, genuinely bigger, cross-cutting gap than
> anything else on this row: every `_compute`-stage failure across (d)/(e)'s own
> re-runs above (and every other compute-stage sampling group prior sessions
> already found) traces to the same missing `VK_KHR_compute_shader_derivatives`
> extension (confirmed unimplemented in `VulkanExtensionInventory.md`) -- a real
> per-invocation screen-space derivative is only available in a fragment
> invocation's own 2x2 quad-grouped execution model today, and no compute-stage
> equivalent exists in this target's runtime at all. This has never had its own
> roadmap line despite being named piecemeal across a dozen-plus rows; filed now
> as its own new tracking row, **roadmap L69**, to break down the remaining work
> rather than continue re-discovering it per-row. With (a)/(b)/(d)/(e)/(f) all
> now resolved or confirmed correctly out of scope, sub-item (c) -- tracked by
> L69 -- is the sole remaining item keeping this row open.
