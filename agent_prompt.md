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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

I've updated the design documentation for the FeMe CPU target to add emulation
for traditional resources. Can you implement that phase?

> 11. **Traditional bound-resource emulation**: add
>   `feme::cpu::BoundResourceNormalizationPass`, preserve finite DXIL and
>   SPIR-V binding-range metadata through raising/import, publish the reserved
>   heap prefixes and source-binding map through `ResourceInfo` and the next
>   artifact-info version, and teach `JITEngine`/`libFeMeRuntimeCPU`/
>   `feme-run` to materialize physical heaps from bound ranges plus logical
>   dynamic heaps. Remove `feme-run`'s testing-only
>   `--dxil-bind-register-resources` bridge once its HLSL tests use this common
>   path. The completion test is the same shader executed with a traditional
>   binding, a native dynamic slot, and both in one module, with identical
>   results through JIT and AOT runtime dispatch. No change is permitted below
>   `ResourceLoweringPass`: a bound handle reaching it is a pipeline error.
