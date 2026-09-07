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

Can you close out L69 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`_compute`-stage sampling with any screen-space-derivative-dependent operand
> (`Grad`, or an ordinary implicit-LOD `texture()` call computing its own LOD
> from `dFdx`/`dFdy`) fails outright at `vkCreateComputePipelines` itself**,
> confirmed by roadmap L60's own final closing re-run (this session) to be the
> sole remaining reason every `_compute`-stage variant of
> `texturegrad`/`texturegradoffset`'s own shadow-sampling sweep still fails, and
> named piecemeal as an already-known, unrelated gap by well over a dozen prior
> rows (L59/L60/L63/L65/L66(h)/L66(i)/L66(j)/L66(k) among others) without ever
> getting its own tracking line. Root cause: real per-invocation screen-space
> derivatives (`dFdx`/`dFdy`) are only meaningful for an execution model with
> adjacent invocations grouped into 2x2 quads (a fragment shader's own
> guaranteed helper-invocation-padded quad layout) -- a compute shader's own
> invocation grouping (`gl_WorkGroupSize`) has no such guarantee at all, so
> Vulkan gates any derivative-dependent compute-shader operation behind the
> separate `VK_KHR_compute_shader_derivatives` extension (confirmed "Not
> implemented" in `VulkanExtensionInventory.md`), which lets a shader opt in to
> one of two explicit derivative-group layouts
> (`DerivativeGroupQuadsKHR`/`DerivativeGroupLinearKHR`) that a real
> implementation must then honor when grouping invocations for a derivative
> computation. Breaking down the remaining work rather than attempting it in one
> pass, per this project's own established splitting precedent: implementing
> this needs (1) advertising the `VK_KHR_compute_shader_derivatives`
> extension/feature struct itself (`PhysicalDeviceInfo.cpp`, mirroring how every
> other extension this target already implements is advertised); (2) recognizing
> the new `DerivativeGroupQuadsKHR`/`DerivativeGroupLinearKHR` SPIR-V execution
> modes on a compute entry point (likely `EntryPoints.cpp`, alongside
> `LocalSize`'s own existing execution-mode handling); (3) a real design
> investigation into how this target's own compute-stage invocation scheduling
> could be made to actually group invocations into the declared derivative
> layout (today's compute-stage lane-widening/scheduling has no quad-awareness
> at all, unlike the fragment stage's own guaranteed-quad dispatch, so this is
> likely the largest single piece of work here); and (4) threading a genuine,
> non-zero derivative into `femeRTPlanImplicitLod`'s existing
> implicit-LOD/`Grad` machinery for a compute-stage caller, which today always
> receives a zeroed derivative pair for this stage (confirmed via
> `FEME_VULKAN_LOG_CREATION_ERRORS=1`: every compute-stage sampling case fails
> `vkCreateComputePipelines` itself, before any of this target's own
> SPIR-V-to-LLVM legalization or CPU lowering ever runs, so the actual blocker
> is upstream of any code this project's own sampling work has touched so far).
> Not yet started; each of (1)-(4) should be scoped and fixed as its own small,
> independently-committed, independently-CTS-measured row rather than attempted
> together, mirroring every other large cross-cutting gap this project's own
> history has split up.
