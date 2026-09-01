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

Please investigate and fix the issues tracked by milestone L12:

> **Indexing an unbounded (runtime-sized) array of resource handles
> (`RWBuffer<int> Buf[]`) fails pipeline creation** with `'llvm.getelementptr'
> op result #0 must be LLVM pointer type or LLVM dialect-compatible vector of
> LLVM pointer type, but got '!llvm.target<"spirv.SignedImage", i32, 5, 2, 0, 0,
> 2, 24>'` -- found as an L10 milestone-description correction:
> `Feature/ResourceArrays/overflow-unbounded-array.test` was grouped under L10's
> own `si32` family, but its real failure is structurally unrelated (an
> `llvm.getelementptr` computing an offset directly into a resource-handle-typed
> value, rather than a byte/element offset into ordinary memory, which the LLVM
> dialect's own GEP verifier rejects since a handle is not a pointer). Distinct
> from -- and a strictly larger gap than -- `SPIRVResourceLowering.cpp`'s
> existing bounded-array-of-handles support (confirmed by checking
> `classifyTexelBufferHandle`/`ResourceGlobalVariablePattern`'s own existing
> array handling, which assumes a compile-time-constant array length
> throughout); needs its own scoping pass to determine where a *runtime*-sized
> handle array should be represented (most likely a descriptor-indexing-style
> indirection through `VkDescriptorSetLayoutBinding`'s own
> `VARIABLE_DESCRIPTOR_COUNT` flag plus a runtime bounds computation, rather
> than the current fixed-stride GEP scheme) before it can be scoped as a fix
