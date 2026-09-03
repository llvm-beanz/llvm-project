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
> (`vktApiFeatureInfo.cpp`). The real blocker this row found was that a
> storage-image atomic needs SPIR-V's `OpImageTexelPointer` (opcode 60) to first
> materialize an addressable pointer from the image handle before a following
> `OpAtomicIAdd`/`OpAtomicExchange`/etc. can operate on it, and that instruction
> had zero representation anywhere in MLIR's own `spirv` dialect (no op
> definition, no opcode-enum case) -- see roadmap R39, now done:
> `spirv.ImageTexelPointer` is added to MLIR (`SPIRVImageOps.td`/`SPIRVBase.td`,
> its own commit) and the `feme`-side `SPIRVToLLVMPatterns.cpp` conversion work
> is also done (`ImageTexelPointerPattern` plus, discovered along the way, a
> full set of `AtomicRMWPattern`/`AtomicCompareExchangePattern` instantiations
> -- MLIR's upstream `spirv` -> `llvm` conversion turned out to have *no*
> lowering pattern for any `Atomic*` op at all, contrary to this row's own prior
> framing; `feme` now supplies its own). A real SPIR-V module using
> `OpImageTexelPointer` + `OpAtomicIAdd` against a storage image now imports,
> converts, and produces an ordinary `llvm.atomicrmw`/`llvm.cmpxchg` --
> confirmed by a new `spirv-to-llvm-image-atomic.mlir` FileCheck test (`ninja
> check-feme` passes in full, 2431/2458, 0 regressions). What is **not** yet
> done, and is what actually gates the format-feature bit:
> `SPIRVResourceLowering.cpp`'s CPU-side lowering has no case for an
> `AtomicRMWInst`/`AtomicCmpXchgInst` user of a storage-image `getpointer` call
> (only `LoadInst`/`StoreInst`, which carry a whole `<4 x i32>` texel rather
> than the scalar 32-bit component an image atomic operates on), and no
> `feme.cpu.image.atomic.*` runtime entry points exist yet -- split off as its
> own row, H8v, since it is a real, substantially larger combinatorial expansion
> (one runtime entry point per atomic kind, times every storage-image shape)
> than the IR-level work this row itself needed.
> `VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT` stays honestly unset for
> `R32_{SINT,UINT}` until H8v lands and a real
> `dEQP-VK.image.atomic_operations.*` case passes end to end

