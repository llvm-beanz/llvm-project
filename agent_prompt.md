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

Can you implement roadmap milestone F12b?

> **A builtin `Input` vector's `spirv.AccessChain` (selecting one lane, e.g.
> `gl_GlobalInvocationID.x`) fails SPIR-V->LLVM legalization, split off F12a's
> own measured-impact finding.**
> `dEQP-VK.pipeline.monolithic.push_descriptor.compute.incremental_updates*`
> (the same 4 cases F12a's own text names) now fail `vkCreateComputePipelines`
> with `'llvm.getelementptr' op operand #0 must be LLVM pointer type or LLVM
> dialect-compatible vector of LLVM pointer type, but got 'vector<3xi32>'` once
> F12a's own std140 fix is applied. `BuiltInAddressOfPattern`/`LoadValuePattern`
> (SPIRVToLLVMPatterns.cpp) already model a builtin `Input` variable like
> `gl_GlobalInvocationID` as a plain SSA value rather than memory, and handle a
> *direct* load of the whole variable correctly, but an `spirv.AccessChain`
> selecting a single lane first (`gl_GlobalInvocationID.x`'s own `OpAccessChain
> %ptr_uint_Input %gl_GlobalInvocationID %uint_0` -- the shape glslang emits
> when only one component is ever read, apparently distinct from the shape it
> emits when the whole vector or more than one lane is read, which is presumably
> why every other passing shader in this report's own scope indexes by
> `gl_GlobalInvocationID.x`/`.xy`/etc. without issue) has no dedicated pattern
> of its own and falls through to MLIR's own default `AccessChainPattern`, which
> assumes its base operand converted to a real `!llvm.ptr` the way every other
> (memory-backed) `spirv.AccessChain` base does, and builds a `getelementptr`
> treating the raw vector value as if it were one instead. Root-causing needs a
> new pattern recognizing an `spirv.AccessChain` whose base is one of these
> value-modeled builtin variables and rewriting it (plus the `spirv.Load` that
> always follows it) to an ordinary `llvm.extractelement`, mirroring how
> `MatrixCompositeExtractPattern`'s own lane-selecting logic already works for a
> value rather than memory
