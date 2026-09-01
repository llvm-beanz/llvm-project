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

Please investigate and fix the issues tracked by milestone L16:

> **`SPIRVResourceLowering.cpp`'s
> `hasOnlySupportedUses`/`hasOnlySupportedPointerUses` never allows a
> `getelementptr` past a `HandleKind::Uniform` (real read-only `cbuffer`)
> resource's own `llvm.spv.resource.getpointer` result** (`AllowGEPs` is
> hard-coded `Kind == HandleKind::Storage \|\| Kind ==
> HandleKind::StorageStruct`, never `Uniform`) -- found as an L13a
> milestone-description correction: `structs.test`'s own struct-typed `cbuffer`
> member (`X x1;`, `X` an identified struct with its own field `a1`) now
> converts cleanly at the SPIR-V-to-LLVM layer (L13a's own fix), but the CPU
> resource-lowering pass this row names still rejects the resulting
> `getpointer`-then-`getelementptr`-into-`x1.a1` chain outright, via
> `UnsupportedOps.cpp`'s generic "is a register-bound resource handle the FeMe
> CPU target cannot normalize" diagnostic (confirmed directly with
> `FEME_VULKAN_LOG_CREATION_ERRORS=1 offloader`, since `llvm-lit`'s own
> `lit.cfg.py` strips that env var). Distinct from, and blocking end-to-end pass
> independently of, L13a's own scope: **any**
> `cbuffer`/direct-field-storage-block member needing further field navigation
> beyond a single flat scalar/vector load (i.e. any struct-typed member at all,
> not just the newly-legalizable padded ones) has apparently never been
> supported by this pass -- `CBuffer/vectors.test`'s own passing case has no
> struct-typed member, so never exercised this path. Needs its own scoping pass:
> likely enabling `AllowGEPs` for `HandleKind::Uniform` too, confirming
> `hasResolvableGEPByteOffset`'s existing byte-offset math needs no
> `Uniform`-specific change, and checking whatever emits the actual runtime call
> for a GEP-resolved `Uniform` address (`createRawLoad`/`mangleResourceCallName`
> and siblings) already handles a nonzero byte offset the same way the
> already-supported `Storage`/`StorageStruct` GEP path does
