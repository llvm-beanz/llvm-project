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

The previous agent invocation ended without actually doing anyting.

Can you work on H8x or other prerequisites blocking the H-series milestones?

> **Ordinary SSBO (`HandleKind::Storage`)/shared-memory atomics also fail
> `SPIRVResourceLowering.cpp`'s `hasOnlySupportedPointerUses`, discovered while
> scoping H8w.** H8w deliberately gated its new atomic branch on `Writable &&
> IsTexel`, true only for `HandleKind::TexelStorage` -- `IsTexel` is false for a
> plain `Storage`/`StorageStruct` handle (an `SSBO`/`buffer` block, not a texel
> buffer), so an ordinary storage-buffer atomic (`atomicAdd` on an SSBO member
> in GLSL, common and unremarkable shader code, not a texel-buffer- or
> image-specific feature) still hits the same generic "unsupported pointer use"
> rejection today. No CTS case has surfaced this gap yet only because no prior
> H-series row happened to exercise an SSBO atomic against `feme`'s own ICD, not
> because the gap doesn't exist -- a real
> `dEQP-VK.ssbo.atomic_operations.*`-shaped (or similarly named) case should be
> expected to fail identically to how H8u's real case failed before H8v/H8w. The
> fix shape is expected to be small and almost entirely reuse H8w's own new
> `createAtomic*Typed`/`feme.cpu.resource.atomic.*.typed.i32` machinery
> (`ResourceCalls.h/.cpp`, `FeMeRuntimeCPU.c`) -- likely only
> `hasOnlySupportedPointerUses`'s own gating condition needs to widen (e.g.
> `Writable \&\& (IsTexel \|\| Kind == HandleKind::Storage \|\| Kind ==
> HandleKind::StorageStruct)`) and `lowerAccesses`'s non-`IsTexel` branch needs
> the same atomic-rewrite block `IsTexel`'s branch just gained -- but this needs
> its own real IR reduction of an actual SSBO-atomic CTS case (mirroring the
> H6g-b/H6j/H6k/H6l/H8u-w chain's own technique throughout) to confirm before
> landing, not assumed from source inspection alone
