---
model: gpt-5.6-terra
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file, and the environment-wide
agent skills at /home/dev/.agents/skills.

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

Can you continue the work on feme? The last agent's suggested next steps are:

1. **Eliminate the pipeline recovery tail.** Reduce the 5,653 unrun cases,
   starting with `pipeline_library.extended_dynamic_state.mesh_shader`
   (1,013), `fast_linked_library.extended_dynamic_state.mesh_shader` (918),
   and `pipeline_library.graphics_library.independent_sets_random` (720).
2. **Profile the seven non-subgroup compile-time long poles.** Sample them in
   the optimizer/backend and assign each to L89, L92, or a new root cause
   before changing code.
3. **Reduce current process terminations by group.** Start with the single
   `spirv_assembly` case, then the three `api` cases; keep crash fixes separate
   from ordinary CTS correctness failures.
4. **Implement continuous measurement.** Land roadmap D4/G1/G2 so full-run
   recovery, per-case result reconciliation, and expected failures stop being
   session-local scripts.
