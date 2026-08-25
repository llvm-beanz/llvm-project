---
model: claude-sonnet-5
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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement roadmap milestone F12a?

> **A `std140` uniform buffer array's `spirv.AccessChain` fails SPIR-V->LLVM
> legalization when dynamically indexed, split off F12's own measured-impact
> finding.**
> `dEQP-VK.pipeline.monolithic.push_descriptor.compute.incremental_updates*` (4
> cases) all fail `vkCreateComputePipelines` with `failed to legalize operation
> 'spirv.AccessChain' that was explicitly marked illegal`, indexing a
> `layout(std140) uniform Input { uint data[16]; } ubo;` block by
> `gl_GlobalInvocationID.x` (`!spirv.ptr<!spirv.struct<(!spirv.array<16 x i32,
> stride=16> [0]), Block>, Uniform>`) -- unlike the equivalent `std430 buffer`
> (storage buffer) array, which every other passing shader in this report's own
> scope already indexes dynamically without issue. `std140`'s own 16-byte array
> stride for a scalar element (as opposed to `std430`'s 4-byte one) is the one
> shape difference a uniform-buffer array's own lowering must additionally
> handle that a storage-buffer array's does not; root-causing needs comparing
> the two paths in `feme::cpu::SPIRVResourceLoweringPass`/the SPIR-V->LLVM
> conversion patterns to find where the `std140` stride case is unhandled
