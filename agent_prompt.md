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

Please investigate and fix the issues tracked by milestone L13a:

> **`convertOffsetStructTypeIgnoringDecorations`'s own "tight-vector retry"
> fallback (`SPIRVToLLVMPatterns.cpp`) does not handle a fixed-size array member
> of an identified struct whose declared per-element `stride` does not match
> that struct's own natural (ABI) size** -- e.g. `!spirv.array<2 x
> !spirv.struct<X, (si32 [0])>, stride=16>`, a real
> `-fvk-use-dx-layout`/`-fvk-use-scalar-layout` shape whenever an
> array-of-structs member is immediately followed by another member needing
> 16-byte alignment. Already anticipated verbatim in that function's own doc
> comment ("tracked separately, see roadmap L13a") when L13 landed, but never
> actually added to this document until this L14 audit restored it (a pure
> citation-vs-row bookkeeping gap, not new work L14 itself discovered).
> Confirmed via this L14 audit's own real `feme-opt` reduction of
> `Feature/CBuffer/structs.test`'s own SPIR-V (and via
> `Feature/CBuffer/array-of-structs.test`/`dynamic-struct.test`/`Feature/StructuredBuffer/packed.test`,
> all four hitting the identical `failed to legalize operation
> 'spirv.AccessChain'` error) to be the exact, complete remaining gap in L13's
> own scope: with this fixed, all four of L13's own originally-named cases still
> failing today should pass. Needs its own scoping pass: likely a further
> fallback alongside the existing tight-vector-array retry -- representing the
> identified struct's *own* body as a tightly-packed byte-array stand-in
> stride-wise (mirroring `getTightVectorArrayType`'s own vector case) whenever
> its natural size undershoots the declared stride, reassembled the same way
> `CompositeConstructPattern`'s own struct case already reassembles a
> tight-vector substitution today
