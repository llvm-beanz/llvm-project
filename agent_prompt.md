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

Please investigate and fix the issues tracked by milestone L20:

> **`feme::cpu::SPIRVResourceLoweringPass`'s `isSupportedRawElementType`
> (`SPIRVResourceLowering.cpp`) rejects a whole-struct `load`/`store` off a
> resource pointer outright, accepting only a scalar or fixed vector**, found as
> an L19 milestone-description correction: with L19's own fix landed,
> `Feature/StructuredBuffer/packed.test`'s own `Doggo Fido = Buf[GI]; ...;
> Buf[GI] = Fido;` whole-struct-copy idiom now converts cleanly at the
> SPIR-V-to-LLVM layer (an ordinary address-space-11 pointer, not a spurious
> nested handle), but `hasOnlySupportedPointerUses`/`hasOnlySupportedUses` (both
> in `SPIRVResourceLowering.cpp`) still reject the resulting
> whole-`Doggo`-struct `llvm.load`/`llvm.store` outright via
> `isSupportedRawElementType`, which only ever recognizes a
> half/float/double/integer scalar or a fixed vector of one -- never a struct --
> so the pass falls through to `UnsupportedOps.cpp`'s generic "is a
> register-bound resource handle the FeMe CPU target cannot normalize"
> diagnostic, confirmed directly via `FEME_VULKAN_LOG_CREATION_ERRORS=1
> offloader`. Distinct from, and blocking end-to-end pass independently of,
> L16's own already-fixed scope (a struct-typed *field*, reached via
> `getelementptr`, converting to its own further load/store) and L19's own scope
> (the SPIR-V-to-LLVM type-conversion layer): this gap is purely in the CPU
> resource-lowering pass's own raw-load/store mangling, for the specific case of
> loading/storing an entire aggregate value directly, with no `getelementptr`
> navigation into an individual field at all. Needs its own scoping pass: likely
> either (a) extending `isSupportedRawElementType` to accept a struct type too,
> and teaching `createRawLoad`/`createRawStore`/`mangleResourceCallName`'s own
> `appendScalarMangling` (`ResourceCalls.cpp`) to describe an aggregate's own
> flattened field/element types in its mangled name (mirroring how a fixed
> vector's own element type and width are already encoded), or (b) decomposing a
> whole-struct load/store into its own per-field raw loads/stores at this pass,
> reassembled/exploded via `insertvalue`/`extractvalue` the same way
> `CompositeConstructPattern`'s own struct case already does at the
> SPIR-V-to-LLVM layer -- whichever avoids ambiguity with
> `isSupportedTexelElementType`'s own texel-buffer case, which already
> special-cases a texel buffer's own `<4 x T>` element type for a different
> reason (see `hasOnlySupportedUses`'s own comment)
