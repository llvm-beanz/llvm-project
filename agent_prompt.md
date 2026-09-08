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

Can you close out L76(b) from the roadmap or other prerequisites blocking the
L-series milestones?

> **Every `_compute`-stage case of
> `dEQP-VK.texture.filtering.2d_array.combinations.linear_mipmap_linear.linear.*`
> fails with a near-total image mismatch ("got 352 invalid pixels")**,
> discovered by roadmap L76's own real CTS sweep of that group's `_fragment`
> variants (16/16 Pass, confirming L76's own implicit-LOD closure) run alongside
> its `_compute` variants (16/16 Fail). Distinct from roadmap L69's own
> `_compute`-stage derivative-group scope: each failing case's own GLSL shader
> computes `textureGrad(u_sampler, texCoord, dPdx.xy, dPdy.xy)` from a
> manually-reconstructed screen-space finite-difference
> (`interpolate(vec2(coord) + vec2(1.0, 0.0), size) - interpolate(vec2(coord),
> size)`), not a hardware `dFdx`/`dFdy` intrinsic, so it needs no
> `DerivativeGroupQuadsKHR`/`DerivativeGroupLinearKHR` execution mode and is not
> gated by `computeDerivativeGroupQuads`/`Linear` support at all -- this is a
> plain explicit-`Grad` `Array2D` sample with a real, nonzero,
> per-invocation-varying `(dPdx, dPdy)` pair, executed from a compute entry
> point. Not yet started; needs its own real IR reduction of one of these 16
> cases (or a similarly-shaped offloader repro built directly, since
> `Feature/Textures/Array.SampleGrad.test` -- confirmed Pass earlier in this
> same L76 sweep -- is apparently only exercised from a fragment-stage entry
> point, not a compute one) to isolate whether the bug is in
> `femeCpuImageSample2DArrayV4F32`'s own explicit-`Grad` footprint math itself,
> in how the compute-stage entry point's own per-invocation resource/descriptor
> plumbing differs from the fragment-stage path this same runtime function
> already passes for, or in the CTS shader's own `interpolate()` helper's
> barycentric coordinate reconstruction interacting badly with this target's own
> workgroup/invocation-ID layout.
