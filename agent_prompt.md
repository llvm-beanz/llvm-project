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

Please investigate and fix the issues tracked by milestone L19:

> **A struct-typed storage-buffer array element's own `spirv.ptr<StructType,
> StorageBuffer>` is misclassified by
> `isBufferBlockStorage`/`getBufferBlockElement` (`SPIRVToLLVMPatterns.cpp`) as
> itself a top-level buffer-block pointer**, found as an L18
> milestone-description correction: `isBufferBlockStorage` returns `true`
> unconditionally for *any* `StorageBuffer`-storage-class pointer to *any*
> struct (never checking for a `Block` decoration at all in that branch, unlike
> its own `Uniform`-storage-class/`BufferBlock`-decoration branch just below
> it), so a `RWStructuredBuffer<Doggo>`'s own per-element `Doggo` struct pointer
> (reached once `spirv.AccessChain` has already selected one array element, as
> `Feature/StructuredBuffer/packed.test`'s own `Doggo Fido = Buf[GI]; ...;
> Buf[GI] = Fido;` whole-struct-copy idiom does, unlike every other
> `StructuredBuffer` test, which only ever navigates directly to an individual
> scalar/vector field via a single multi-index access chain) is spuriously
> converted into a *second*, nested `spirv.VulkanBuffer` resource handle instead
> of ordinary memory (address space 11), confirmed directly via
> `FEME_VULKAN_LOG_CREATION_ERRORS=1 offloader` (`'llvm.store' op operand #1
> must be LLVM pointer type, but got '!llvm.target<"spirv.VulkanBuffer", ...>'`)
> and a real `feme-opt --feme-convert-spirv-to-llvm` reduction of
> `packed.test`'s own SPIR-V. Needs its own scoping pass: likely requiring
> `isBufferBlockStorage`'s `StorageBuffer`-class branch to also check for the
> struct's own `Block` decoration (mirroring its `Uniform`-class/`BufferBlock`
> branch immediately below), confirming every currently-passing
> `StructuredBuffer`/`ConstantBuffer`/`cbuffer` test's own top-level block
> struct still carries that decoration (so this tightened check does not regress
> any of them), and checking whether any further pattern (e.g. a struct-typed
> `spirv.Store`/`spirv.Load` reaching an ordinary address-space-11 pointer)
> needs its own new support once this misclassification is fixed
