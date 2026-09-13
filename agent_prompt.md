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

# Request

Can you work on H101j or other blocking work to make progress on the H-series
milestones?

> **`CanonicalizeStage.cpp` doesn't understand the new "tight" `array<N x
> array<Mxf32>>` matrix/array-of-vectors representation
> `SPIRVToLLVMPatterns.cpp` now emits** (newly exposed by H101i's own closing
> fix): `random_vertex.all_instance_array.11` now hits
> `feme-graphics-validate-stage` errors (`'feme.stage.output.store' ...
> component N is out of range for element 0/1/2`, plus `unresolved stage-IO
> global-variable access to 'spirv_var_46'`) instead of the legalization crash
> H101i fixed -- `CanonicalizeStage.cpp`'s row/component-shape resolution logic
> expects a matrix column or array-of-vectors element to convert to a real
> `VectorType`, and doesn't yet recognize the tight, alignment-free `array<N x
> array<Mxf32>>` shape H101i's fix can now produce for the same source member.
> Most of the ~77 `*instance_array*` cases still fail at pipeline creation for
> this reason (`VK_ERROR_INITIALIZATION_FAILED`); 2 (`all_instance_array.9`,
> `all_instance_array.68`) get further, to a wrong-value XFB `Mismatch`, and
> `random_geometry.all_instance_array.11` gets furthest, to a `JIT session
> error: Symbols not found: [ spirv_var_46 ]`. Not yet triaged -- needs
> `CanonicalizeStage.cpp`'s row/component-shape detection (`getStageIORowShape`
> or similar) extended to recognize a tightly-packed `array<N x array<Mxf32>>`
> member the same way it already recognizes `array<N x vector<Mxf32>>`
