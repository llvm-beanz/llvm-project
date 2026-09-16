---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

**Before doing anything else**: `vulkaninfo --summary | grep deviceName` and
confirm `FeMe CPU Vulkan Device`. Every session from now on, every time, not
just once at the start.

# Request

Can you work the H-series milestones?

The last session suggested the next steps:

1. **~15 minutes, cheap diagnostic, do first if picking this back up:** confirm
   whether `WaveActiveMax.test`'s `TID.x % 8`-into-4-element-buffer shape is a
   pre-existing `offload-test-suite` test bug (check git blame/history on that
   file, or just try changing the modulus locally and see whether the CHECK
   lines suddenly match) before spending real time on it as a `feme` bug.
2. **~1-2 days, real payoff (closes 2 failures), not urgent:** H160 -- add
   `spirv.ArrayLength` to the SPIR-V dialect (`SPIRVOps.td`), plus
   (de)serializer and conversion-pattern support. See H160's own roadmap row for
   the exact plan; check whether the existing `RWBuffer<T>::GetDimensions()`
   bound-resource metadata path can be reused for the new op's lowering before
   inventing new plumbing.
3. **~half a day, real payoff (closes up to 5 failures), not urgent,
   upstream-MLIR-flavored:** H124d -- same shape as H160 but for
   `OpDPdx`/`OpDPdy`/`OpFwidth` (opcodes 207-215). Needs new SPIR-V dialect
   derivative ops plus CPU-backend screen-space-derivative
   (quad/2x2-lane-grouping) semantics, which the CPU SIMD renderer does not
   currently implement at all -- larger than H160 for that reason.
