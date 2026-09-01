---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Please investigate and fix the issues tracked by milestone L9:

> **`SPIRVResourceLoweringPass` never normalizes a single-channel-format
> texel-buffer (`Dim::Buffer` image) store**, discovered via L2's own triage:
> `classifyTexelBufferHandle`/`hasOnlySupportedUses`/`isSupportedTexelElementType`
> (`SPIRVResourceLowering.cpp`) already correctly treat every `OpImageRead` as
> returning a full `<4 x T>` regardless of the underlying format's real channel
> count (per SPIR-V's own spec, matching the header comment's already-recorded
> V4 scope note) -- but a **write** to a single-channel format (e.g.
> `R32i`/`R32f`, `RWBuffer<int>`/`RWBuffer<float>`'s own SPIR-V shape) is not
> similarly widened by `dxc`/MLIR's own SPIR-V-to-LLVM conversion: reduced
> directly (a two-line `RWBuffer<int> In/Out; Out[0] = In[0];` shader,
> `feme-translate --import-spirv`/`feme-opt --feme-convert-spirv-to-llvm`) to
> `llvm.store %10, %18 : i32, !llvm.ptr` -- a **scalar** `i32` store, not the
> `<4xi32>` the load side already produces and this pass's own
> `isSupportedTexelElementType` requires -- so this handle is left un-normalized
> entirely and falls through to `UnsupportedOps.cpp`'s generic "register-bound
> resource handle ... cannot normalize into a heap access" diagnostic. Fixing
> this needs more than just accepting the narrower scalar type at the IR-shape
> check: `FeMeRuntimeCPU.c`'s own `femeCpuResourceStoreTypedV4I32`/`V4F32`
> helpers always write a full 16-byte (`<4 x T>`) element stride, which would be
> wrong for a format whose real per-element stride is 4 bytes (a single channel)
> -- needs its own format-aware, narrower-than-`<4 x T>` runtime store (and,
> symmetrically, load) helper pair, exactly the "physically-narrower-than-`<4 x
> T>` per-format padding this milestone does not add" the pass's own
> pre-existing header comment (and `FeMeVulkanDesign.md`'s "V4 status note")
> already flags as deliberately out of scope -- so this is confirmed,
> scoped-out-until-now work, not a surprise gap. Accounts for the entire
> `Basic/Matrix/*.test` family's own failures (all of which use `RWBuffer<int>`
> purely as their I/O mechanism, unrelated to matrices themselves) plus several
> single-channel `Feature/Textures`/`Feature/StructuredBuffer`/`Feature/CBuffer`
> cases
