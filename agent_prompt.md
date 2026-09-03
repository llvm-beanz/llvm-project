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

Can you work on H8u or other prerequisites blocking the H-series milestones?

> **`STORAGE_IMAGE_ATOMIC_BIT` for `r32_{sint,uint}`, split off from H8s.** A
> real CTS re-run found `R32_SINT`/`R32_UINT` still missing this bit, confirmed
> genuinely mandatory via CTS's own `s_required*` tables
> (`vktApiFeatureInfo.cpp`). Investigated this session and found the real
> blocker: a storage-image atomic needs SPIR-V's `OpImageTexelPointer` (opcode
> 60) to first materialize an addressable pointer from the image handle before a
> following `OpAtomicIAdd`/`OpAtomicExchange`/etc. can operate on it (unlike an
> ordinary buffer/shared-memory atomic, which already deserializes and converts
> fine today, needing no `feme`-side change at all -- MLIR's own `spirv` dialect
> already models every `Atomic*` op generically over any pointer).
> `OpImageTexelPointer` itself has **zero** representation anywhere in MLIR's
> own `spirv` dialect -- no op definition (unlike `OpImageRead`/`OpImageWrite`,
> which both have one) and no named opcode-enum case at all in `SPIRVBase.td`
> (confirmed by grep) -- so a real driver-side SPIR-V binary using it (exactly
> what `dEQP-VK`'s own image-atomics conformance tests compile to) cannot even
> be deserialized by `feme::SPIRVImporter`, the same "unhandled opcode" shape as
> the pre-existing
> `feme::SPIRVImporter`-cannot-deserialize-LLVM-SPIR-V-backend-output known gap
> (Design.md, found by roadmap R14). Documented as its own new "Known gap" in
> Design.md, alongside the concrete shape closing it would need (a new
> `spirv.ImageTexelPointer` op in MLIR's own `SPIRVImageOps.td` plus its own
> opcode-enum entry -- real, scoped, tractable work in `mlir/`, this same tree
> -- see roadmap R39, which now tracks doing it directly rather than only
> documenting it, matching F8c's own precedent for landing a real
> upstream-shaped MLIR SPIR-V fix in this project) and the further `feme`-side
> work still needed after that (a `SPIRVToLLVMPatterns.cpp` conversion pattern
> lowering the new op into the existing `createResourcePointer` intrinsic so a
> following `Atomic*` op becomes an ordinary LLVM `atomicrmw`/`cmpxchg`, and
> `SPIRVResourceLowering.cpp`'s `lowerImageAccesses` learning to rewrite an
> `AtomicRMWInst`/`AtomicCmpXchgInst` user of a storage-image `getpointer` call
> into a new `feme.cpu.image.atomic.*` runtime entry point mirroring
> `feme.cpu.image.store.2d.v4i32`'s own precedent). None of that feme-side work
> is even testable until the upstream gap closes, since no such SPIR-V module
> can be imported today -- `STORAGE_IMAGE_ATOMIC_BIT` stays honestly unset for
> `R32_{SINT,UINT}` until it does
