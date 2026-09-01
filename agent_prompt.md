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

Please investigate and fix the issues tracked by milestone L15:

> **`SIMDize.cpp`'s L11 fix (`widenGroupSharedLoad`'s vector-row gather) is only
> reachable through a plain, unmasked `LoadInst`/`StoreInst` -- not through the
> `feme.cpu.masked.load/store.*.as3` *call* form `feme-cpu-linearize` produces
> whenever the groupshared access is itself inside genuinely divergent control
> flow** (e.g. a real `if (ThreadID.x == 0)` guard, as
> `WaveOps/GroupSharedMatrixRowComponentDataRace.test` itself has) -- found as
> an L14 milestone-description correction (L11's own real named test still fails
> identically today; L11's own row had already disclosed no real end-to-end
> rerun confirmed this, only a from-scratch IR reduction with no divergent
> branch in it at all). `checkVectorDecompositionSupported`'s own
> producer-recognition loop has no case at all for a `feme.cpu.masked.load.*`
> call producing a vector result (only `matchResourceCall`/`matchImageCall`/a
> homogeneous intrinsic/an ordinary `LoadInst` are recognized), so it hits the
> same generic "unsupported producer" diagnostic L11 was supposed to close; even
> if recognized, `widenMaskedLoad` itself would still need its own vector-aware
> path, since it currently builds one illegal `<W x <4 x float>>`
> `llvm.masked.gather` unconditionally (`FixedVectorType::get(CI.getType(),
> WaveSize)` where `CI.getType()` is already a vector) rather than
> `widenGroupSharedLoad`'s existing per-component decomposition. Needs its own
> scoping pass: likely extending `checkVectorDecompositionSupported` to accept a
> `matchMaskedLoad`/`matchMaskedStore` call over a groupshared address as a
> supported vector producer/consumer, and giving
> `widenMaskedLoad`/`widenMaskedStore` a groupshared-specific vector-typed
> branch that reuses `widenGroupSharedLoad`'s own per-component gather logic
> (mirroring how `widenGroupSharedLoad` itself already reuses
> `widenGroupSharedGEP`'s address widening) instead of the generic single-gather
> path
