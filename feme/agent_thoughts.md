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
