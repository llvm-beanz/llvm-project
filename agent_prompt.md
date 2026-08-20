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
after each change and update the VulkanCTSReport.md.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you complete milestone D3 on the roadmap?

> **Per-bucket attribution of D0's net +2,553 newly-failing cases**, at C1-C8's
> level of rigor. One bucket is already traced (`ubo.*.std430`'s 2,650 cases: a
> pre-existing, already-tracked `feme-cpu-simdize`
> divergent-vector-decomposition gap, C3's own "milestone 7 deviation", newly
> *reached* rather than newly created); `spirv_assembly.instruction.compute`
> (417), `synchronization.op.{multi,single}_queue` (277), and the remaining tail
> are not yet traced
