---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

## Suggested next steps

1. **(~5 min)** No scratch files created this session outside
   `/tmp/l124a_repro.mlir` (already existed from before compaction) --
   nothing new to clean up.
2. Pick up **L124(b)** next
   (`dEQP-VK.compute.pipeline.basic.remove_global_load_pass`): a plain
   `OpConstant`/`OpConstantComposite` used as a module-scope
   `OpVariable`'s initializer isn't modeled anywhere in the deserializer
   -- needs a new attribute on `spirv.GlobalVariable` (alongside
   `initializer`/`zero_initialized`) plus matching serializer/
   `SPIRVToLLVM` lowering support. Not yet scoped in more detail than
   the roadmap entry itself -- start there.
3. Alternatively, **L124(c)**
   (`dEQP-VK.compute.pipeline.basic.undefined_values`, `OpCopyLogical`
   entirely unmodeled) or **L124(d)**
   (`dEQP-VK.compute.pipeline.device_group.device_index`, `gl_DeviceIndex`
   not wired up in the CPU compute pipeline) are the other two
   remaining `compute.*` items -- each is a standalone feature gap, not
   yet started, roughly similar scope to L124(b).
4. The 2 `zero_initialize_workgroup_memory` fails
   (`composites.2`, `types.bool`) seen in this session's sweep aren't
   yet broken out as their own roadmap letter -- worth adding a new
   L124 sub-item for them if picked up, since they weren't touched
   this session and their root cause is unconfirmed.
5. `ninja check-feme` and the CTS build directories are both
   incremental from here -- reuse them, no reconfigure needed.
