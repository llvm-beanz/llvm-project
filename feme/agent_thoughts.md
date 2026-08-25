# Agent Thoughts

## Roadmap F8a: shader-side `subpassInput` local-read consumption

**Task**: implement roadmap milestone F8a and close out F8.

### Starting point

F8 had already implemented and validated `vkCmdSetRenderingAttachmentLocations`/
`vkCmdSetRenderingInputAttachmentIndices` as real commands, with the
attachment-location remap genuinely honored by the executor. But
`vkCmdSetRenderingInputAttachmentIndices`' own mapping was only recorded,
never consulted, because no shader-side SPIR-V `subpassInput` local-read
consumption existed at all. `dynamicRenderingLocalRead`/
`VK_KHR_dynamic_rendering_local_read` and its two limit fields stayed
unadvertised as a result. F8a's job was to close that gap.

### Audit (item 1)

The roadmap text asked to first confirm whether `OpTypeImage(Dim=SubpassData)`
and `OpImageRead`'s subpass-local form are modeled by MLIR's `spirv` dialect.
They are (`SPIRV_D_SubpassData`, `SPIRV_ImageReadOp`'s "if Dim is SubpassData,
Coordinate is relative to the current fragment location" text) -- but the
audit found a real, unmodeled gap one level down: the `InputAttachmentIndex`
decoration itself (SPIR-V code 43) had no case in either MLIR's SPIR-V
*deserializer* or *serializer* (`mlir/lib/Target/SPIRV/{Deserialization,
Serialization}`). Without it, a `subpassInput` variable's own binding never
survives a round trip through real SPIR-V binary at all. Both were one-line
fixes: folding the decoration into the existing single-integer-literal case
group each file already has for `DescriptorSet`/`Binding`/`Location`/etc.,
since MLIR's generic decoration-attaching mechanism (used for `component`/
`index` too) needs no dedicated `GlobalVariableOp` attribute. The serializer
half wasn't found until much later, while writing the end-to-end `DrawTest`
that actually assembles real SPIR-V binary from MLIR text -- a good reminder
that "audit first" benefits from an end-to-end proof, not just a read of the
op/type definitions.

### Design decisions (item 2)

The core design question was: how does a subpass read reach the currently-
bound render-target attachment, given it deliberately bypasses the
descriptor-set image model (per the roadmap text's own framing:
`feme::vulkan::RenderTargetBinding`, not a descriptor-set image)?

I looked at three existing "vocabularies" this codebase already has for
moving values between SPIR-V and the CPU runtime:

1. **`llvm.spv.resource.*` intrinsics** (real image/buffer descriptor
   access, resolved through `feme::cpu::ResourceHeap`/`ImageHeap`). Doesn't
   fit: a subpass read has no descriptor to resolve through at all.
2. **`feme.cpu.image.*` calls** (`ImageCalls.h`, e.g.
   `feme.cpu.image.load.2d.v4f32`) -- the *runtime* helper family a resolved
   image access eventually lowers to. This turned out to be exactly reusable:
   a subpass attachment and a descriptor-bound 2D image share the same
   `FemeImageDescriptor` shape (pointer, format, width/height, mip layout),
   so the *read* itself needed no new runtime C code -- only a new *heap*
   (`SubpassInputHeap`) built from attachments instead of descriptors.
3. **`feme.stage.*` calls** (`StageOps.h`, e.g. `feme.stage.input.load`) --
   FeMe's own source-independent vocabulary for stage IO, always created at
   the *LLVM IR* level (`CanonicalizeStagePass`), never at MLIR-conversion
   time by any existing pattern.

I chose to introduce a new `StageOpKind::SubpassLoad`
(`feme.stage.subpass.load(attachment_index, component)`) and, breaking with
precedent #3, create it *directly from the MLIR SPIRVToLLVM conversion
pattern* (`SubpassLoadPattern`) rather than deferring to `CanonicalizeStagePass`.
Reasoning: `CanonicalizeStagePass` recognizes stage IO through a very
specific "address-space-7/8 global variable" convention that only fits
`Input`/`Output` storage class variables; a subpass image is `UniformConstant`,
loaded via `spirv.Load`+`spirv.mlir.addressof`, an entirely different shape.
Rather than teaching that pass a second convention, emitting the call
directly at MLIR-conversion time (an ordinary `LLVM::CallOp` to a declared
`LLVM::LLVMFuncOp`, the same shape `feme.cpu.resource.*`/`feme.cpp.image.*`
calls already use once lowered, just earlier in the pipeline) was more
surgical and kept `CanonicalizeStagePass` untouched.

`SubpassLoadPattern` deliberately never references the underlying variable's
own `llvm.spv.resource.handlefrombinding` handle -- the whole point is that a
subpass read does *not* go through it. That handle becomes dead code, which
turned out to need its own small fix (see "Bugs found" below).

### Threading the heap through (items 2-3)

Getting `feme.stage.subpass.load` from "exists as an IR call" to "reads a
real pixel" needed changes at every layer `feme.stage.input.load` already
touches, plus one new one:

- `StageArgsLayout.h`/`RuntimeABI.h`: `FemeShaderResources` gained
  `SubpassInputHeap`/`SubpassInputHeapCount`, mirroring `ImageHeap`/
  `ImageHeapCount` but populated from attachments, not descriptors.
- A new `feme::cpu::SPIRVSubpassLoweringPass` (deliberately *not* folded
  into `SPIRVResourceLoweringPass`, since a subpass input isn't a
  `spirv.VulkanBuffer` handle at all and doesn't fit that pass's
  handle-collection machinery) appends `subpass_input_heap`/
  `subpass_input_heap_count` parameters to any function using
  `feme.stage.subpass.load`, the same Function-replacement shape
  `addResourceEnvParams` uses for its own eight.
- `FragmentWrapper.cpp`'s `lowerFragmentSubpassLoad` reads the invocation's
  own `FragCoord` (`FemeFragmentInvocation::Position`, already available for
  every fragment invocation) and calls the *existing*
  `feme.cpu.image.load.2d.v4f32` runtime helper against the new heap --
  reusing runtime C code rather than duplicating it.
- `feme::vulkan::runDraw`'s new `buildSubpassInputHeap` (CommandBuffer.cpp)
  builds one `FemeImageDescriptor` per logical input-attachment index
  directly from the currently-bound color/depth/stencil attachments,
  resolved through `GraphicsState::ColorAttachmentInputIndices`/
  `DepthInputAttachmentIndex`/`StencilInputAttachmentIndex` -- F8's own
  fields, finally consulted. Threaded through `PreparedDraw`/`Executor.cpp`.

### Bugs found getting a real shader through the whole pipeline

Writing `DrawTest.SubpassLoadReadsBackTheColorAttachmentItWrote` (two draws
in one dynamic-rendering instance: the first fills the attachment solid red,
the second reads it back via `subpassLoad` and writes solid green) exercised
the *entire* pipeline for the first time with this new op, and found several
real bugs, each fixed and covered by its own test/comment:

1. **MLIR serializer gap** -- found only once the test tried to assemble
   real SPIR-V binary from MLIR text containing `input_attachment_index`.
2. **`checkSupportedRaisedOps` rejected a totally unused resource handle.**
   `SubpassLoadPattern` leaves the variable's own `handlefrombinding` call
   dead on purpose; the existing check rejected *any* surviving
   `handlefrombinding`, whether or not it was ever read. Now a call with
   `use_empty()` is accepted, alongside the existing root-constant carve-out.
   Updated the two existing tests for this function (which, in hindsight,
   were themselves testing an *unused* handle -- exactly the shape this
   change now accepts instead of rejects) to actually read their handle, and
   added a dedicated test for the new allowance.
3. **`SPIRVSubpassLoweringPass` double-processed a function.** Iterating a
   live `M.functions()` (even with `make_early_inc_range`) while replacing
   functions in place let the newly-created replacement (appended at the
   end of the module's function list) get visited a second time in the same
   loop. Fixed by snapshotting candidates first, exactly like `SIMDizePass`'s
   own `Entries` snapshot already does for the identical reason.
4. **Missing metadata copy.** The same pass's Function-replacement helper
   forgot to copy `feme.signature` (and everything else) onto the
   replacement -- `FragmentWrapperPass` reads that metadata back much later
   and errored with "requires attached feme.signature metadata".
5. **`StageOpKind::SubpassLoad` needed to be marked overloaded**, even
   though its result type never actually varies (always `f32`). Non-
   overloaded meant its scalar declaration and `SIMDizePass`'s widened
   `<W x f32>` form collided under one symbol name once both existed;
   `CallBase::getCalledFunction()` refuses to resolve a call whose own
   function type disagrees with its callee's declared type, so the widened
   call silently failed to match `isStageOpCall`, survived unlowered all the
   way to the JIT, and failed with "Symbols not found". Marking it
   overloaded gives the widened form its own `feme.stage.subpass.load.f32`
   symbol, the same mechanism every other kind that survives to SIMDize
   widening already relies on.
6. **A second, separate extension-name list.** `PhysicalDeviceInfo.cpp`
   keeps its own by-name extension list (distinct from
   `vk_gen_entrypoints.py`'s `SUPPORTED_EXTENSIONS`, which only drives
   entry-point dispatch generation) for extensions real CTS cases enable
   regardless of `apiVersion`. Found by actually running
   `dEQP-VK.renderpasses.dynamic_rendering.*.local_read.*`, which reported
   `NotSupported` until this list also got the new extension.

Debugging technique worth noting: when a pipeline stage silently drops or
misroutes IR with no error message, a temporary `errs()` dump (env-var-gated)
right after the suspect pass, plus a couple of throwaway `errs()` prints
inside the function under suspicion, found bugs 3-5 far faster than staring
at the final compiled module. All debug scaffolding was removed before the
final commits.

### Scope and what's left (F8b)

`buildSubpassInputHeap` resolves `DepthInputAttachmentIndex`/
`StencilInputAttachmentIndex` into heap slots already, but only actually
populates a slot for a single-sample attachment -- a multisample one is left
unpopulated (reads as zero) rather than guessing at a per-sample layout no
test demonstrates, and no depth/stencil-format subpass read has been
exercised at all yet. Per "advertise only what passes",
`dynamicRenderingLocalReadDepthStencilAttachments`/
`dynamicRenderingLocalReadMultisampledAttachments` stay `VK_FALSE`, and the
remaining work is split off as roadmap F8b.

### CTS reality check

Running the real `dEQP-VK.renderpasses.dynamic_rendering.*.local_read.*`
suite (54 cases) after advertising the extension found that 38 of them fail
on an entirely unrelated, pre-existing `feme::cpu::SIMDizePass` limitation
(component decomposition for a divergent vector used outside a handful of
recognized shapes -- the documented "roadmap milestone 7 deviation"): their
real, `glslang`-compiled GLSL shaders happen to produce that shape
regardless of whether they touch `subpassInput` at all, so pipeline
*creation* fails before this row's own code ever runs. This is why a
dedicated hand-written `DrawTest` was necessary to actually prove
`subpassLoad` produces correct pixels -- the CTS suite's own shaders can't
reach that code path yet. Fixing the milestone-7 SIMDize gap is out of this
row's scope; noted in `VulkanCTSReport.md` as a confirmed, unrelated
blocker rather than assumed.

## Roadmap F8b: depth/stencil and multisample subpass-input local-read coverage

**Task**: implement roadmap milestone F8b and close out F8.

### Starting point

F8a left two things unfinished, both called out in its own status note:
`buildSubpassInputHeap` (CommandBuffer.cpp) resolves `DepthInputAttachment
Index`/`StencilInputAttachmentIndex` into heap slots, but (a) bails out of
`populate()` entirely for any attachment with `SampleCount != 1`, and (b) had
never actually been exercised against a real depth or stencil format --
every subpass-load test so far used a plain color attachment.

### Investigation: where's the gap, actually?

Before writing any fix, I traced what would happen today if a shader read a
`D32_FLOAT` depth attachment through `subpassLoad`. `buildSubpassInputHeap`'s
`populate()` already handles this fine at the C++ level: `getFixtureFormat
ElementSize` (ImageFixture.cpp) already has cases for `D16_UNORM`/`D32_FLOAT`/
`S8_UINT`, so the descriptor gets built with a correct `Format`/`Width`/
`Height`/layout. The read itself goes through `feme.cpu.image.load.2d.v4f32`,
which resolves to `femeRTFetchTexel2D` in the CPU runtime
(FeMeRuntimeCPU.c) -- and *that* function's format-guarded element-size
table (`femeRTImageFormatElementSize`) had no case for any of the three
depth/stencil formats, so `ElemSize == 0` and every such fetch silently read
back all-zero. So the roadmap's "format-decode gap" was real, and it was in
the runtime, not in `buildSubpassInputHeap` as the row's own phrasing might
suggest at first read.

### Fix (b): depth/stencil format decode

Added `D16_UNORM` (case 31, 2 bytes)/`D32_FLOAT` (case 32, 4 bytes)/
`S8_UINT` (case 35, 1 byte) to `femeRTImageFormatElementSize`, and matching
decode cases to `femeRTUnpackImageTexel`: `D16_UNORM`/`S8_UINT` normalize to
`[0.0, 1.0]` in component 0 (the same convention `A8_UNORM` already uses for
its own single normalized component); `D32_FLOAT` is the identity case, like
`R32_FLOAT`. This is a deliberately narrow, "just the format table" fix --
real Vulkan's stencil-aspect `subpassLoad` is actually an unsigned-integer
read (`usubpassInput`/`OpTypeImage` with an integer sampled type), but
`SubpassLoadPattern` (SPIRVToLLVMPatterns.cpp) and `feme.stage.subpass.load`
(StageOps.h) are both hard-wired to `f32` today, with no integer-typed
counterpart -- adding one is a bigger, separate piece of work than the
"format-decode gap" this row's own text asked for, so I scoped the fix to
what makes the existing `f32`-only pipeline produce a correct, honest value
for a normalized 8-bit stencil reference, not to modeling `usubpassInput`
itself.

### Fix (a): multisample heap population + a latent bug it uncovered

`buildSubpassInputHeap`'s `populate()` no longer bails out on `SampleCount >
1`; it now derives `Dst.SampleCount`/`Layout.SampleStride`/`Layout.RowPitch`
the same way `Image.cpp`'s `computeSubresourceLayouts` already does for a
real (non-fixture) multisampled image: every sample of one texel is stored
contiguously (`SampleStride == ElemSize`), and a row is `Width` texels of
`SampleCount * ElemSize` bytes each. `femeRTFetchTexel2D`/
`femeRTFetchTexel2DI32` were updated to derive their per-texel addressing
stride from `SampleStride`/`SampleCount` instead of always assuming
`SampleCount == 1` -- this always reads sample 0 of a multisampled texel,
since nothing downstream can request a different one yet (see "what's still
not done" below).

Making `femeRTFetchTexel2D` actually *read* `SampleStride` (previously an
inert field nothing consulted) immediately broke an existing, passing test:
`ASTCSampledImageDispatchTest.SamplesARealDecodedTexelRatherThanAllZero`.
Tracing it down: `decodeASTCImageForSampling` (CommandBuffer.cpp), which
pre-decodes an ASTC block-compressed image into per-texel RGBA8 storage
before handing it to the runtime, had `Result.MipLayouts[L] = {Offset,
RowPitch, SlicePitch, SlicePitch}` -- the aggregate-initializer's fourth
field is `SampleStride`, mistakenly set to `SlicePitch` (a large, decidedly
non-multisample value) instead of `0`. This was harmless for as long as
`SampleStride` went unread; making it meaningful is exactly what F8b's own
"already model one, unused so far" phrasing was pointing at, and it
surfaced a real, pre-existing latent bug the moment it stopped being inert.
Fixed by setting that field to `0` (an ASTC image is never multisampled in
real Vulkan, matching `Image.cpp`'s own comment on the same point). Caught
this by running the full `check-feme`/`FeMeVulkanTests` suite after the
runtime change, not just the new tests -- worth remembering: a field
described as "unused so far" is exactly the kind of thing likely to have an
inconsequential-until-now wrong value sitting somewhere.

### Tests added

- `ImageSamplingTest.LoadFetchesD16Unorm`/`LoadFetchesD32Float`/
  `LoadFetchesS8Uint` (unittests/Runtime/CPU): direct JIT-and-call tests of
  the three new runtime decode cases, following the existing per-format test
  pattern in this file exactly.
- `ImageSamplingTest.LoadFetchesSample0OfMultisampledTexel`: a 2-texel,
  4-sample `R32_FLOAT` image, checking that texel (1, 0)'s fetch does not
  alias one of texel (0, 0)'s samples -- the addressing bug the `SampleStride`
  fix targets directly.
- `DrawTest.SubpassLoadReadsBackTheDepthAttachmentItWrote`/
  `SubpassLoadReadsBackTheStencilAttachmentItWrote`: the CTS-shaped
  end-to-end tests the roadmap row asked for, following
  `SubpassLoadReadsBackTheColorAttachmentItWrote`'s own shape closely but
  clearing the depth/stencil attachment to a known value instead of
  drawing into it first (no depth-writing shader machinery needed for what
  this row is actually testing: that a real depth/stencil texel round-trips
  through `subpassLoad`, not the depth/stencil *test* itself). Picked clear
  values (`128.0 / 255.0` for depth, `200` for stencil) that are exactly
  reproducible through the eventual `R8G8B8A8_UNORM` color-store rounding,
  to keep the pixel assertions exact rather than tolerance-based.

### What F8b does and does not close

Both halves of F8a's own "remaining quarter" note are addressed: depth/
stencil format decode (b), and a correct multisample heap layout (a). This
is enough to honestly flip `dynamicRenderingLocalReadDepthStencilAttachments`
to `VK_TRUE` (`EntryPoints.cpp`, `PhysicalDeviceInfoTest.cpp` expectation
updated to match). It is *not* enough for
`dynamicRenderingLocalReadMultisampledAttachments`: no caller threads an
explicit sample index through a subpass load anywhere in the stack --
`feme::StageOpKind::SubpassLoad` has no `Sample` operand, and
`SubpassLoadPattern` explicitly rejects any `spirv.ImageRead` that carries
one (`hasImageOperands`). Closing that needs a `Sample` operand added to the
stage op itself, a `SubpassLoadPattern` case that reads it instead of
rejecting it, and `lowerFragmentSubpassLoad`/the runtime threading it into
the texel address -- real, separate plumbing through four different files,
not a mechanical extension of this row's own fix. Split off as roadmap F8c
rather than claimed as done.

### Roadmap bookkeeping

F8b is struck through as done for its depth/stencil scope, with the
multisample-specific remainder split off as F8c (per "if you do not [fully]
complete it, add lettered entries" -- F8c rather than F8bXX, since F8b's own
letter is already taken and the remaining work is better framed as its own
follow-on row than a re-split of F8b itself). `Vulkan14FeatureInventory.md`
and `VulkanExtensionInventory.md` updated to match: `dynamicRenderingLocal
ReadDepthStencilAttachments` real, `dynamicRenderingLocalReadMultisampled
Attachments` still `n/a`/`VK_FALSE` with its new F8c citation.
