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
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you complete H6g-b-a-i-a-i-c?

> **`vkCreateGraphicsPipelines` fails at JIT-link time with "Symbols not found:
> [ feme.cpu.resource.load.raw.v2f32, feme.cpu.resource.load.raw.v3f32,
> feme.cpu.resource.load.raw.v3i32, feme.cpu.resource.load.raw.v2i32,
> feme.cpu.resource.load.raw.v4i32 ]"**, now the new dominant blocker in the
> same 80-case `dEQP-VK.mesh_shader.ext.in_out.*` bucket H6g-b-a-i-a-i-b's own
> `fcmp`/`icmp`/reduce/vectorizable-intrinsic fixes let those cases progress
> past `feme-cpu-simdize` entirely (confirmed by re-running the full 560-case
> bucket and spot-checking several individual cases with
> `FEME_VULKAN_LOG_CREATION_ERRORS=1` against the real `deqp-vk`/`feme` Vulkan
> ICD once H6g-b-a-i-a-i-b's own fix landed). Root cause isolated:
> `feme/runtime/CPU/FeMeRuntimeCPU.c` only defines the scalar
> (`feme.cpu.resource.load.raw.i32`/`.f32`) and full-`<4 x T>`-width
> (`feme.cpu.resource.load.raw.v4f32`/`.v4i32`) raw-buffer-load overloads today
> (see its own `asm("feme.cpu.resource.load.raw.v4f32")`-labeled function and
> neighbors) -- the 2- and 3-component overloads a `vec2`/`vec3`/`ivec2`/`ivec3`
> mesh-shader input/output actually needs are simply missing, so the JIT's
> `orc::LLJIT` can't materialize a call to them at pipeline-creation time. The
> store-side (`feme.cpu.resource.store.raw.*`) and typed-buffer
> (`feme.cpu.resource.{load,store}.typed.v4{f32,i32}`) paths were not checked
> for the same v2/v3 gap and may need the same fix. Not yet fixed: needs new
> `feme.cpu.resource.load.raw.v2f32`/`.v3f32`/`.v2i32`/`.v3i32` (and likely the
> store-side/typed-buffer counterparts, pending the same gap check) runtime
> functions mirroring the existing `v4f32`/`v4i32` ones'
> bindless-descriptor-lookup-then-masked-load shape, plus confirming
> `feme::cpu::ResourceCalls`/`ResourceLowering.cpp` already emit calls to these
> names for narrower vector widths (or need their own fix to do so)
