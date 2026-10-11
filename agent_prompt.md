---
model: claude-sonnet-5
resume: db99d377-bdef-4dd8-a2b5-787bcf66bc23
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

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~30 min)** `L412`'s `properties.basic` Fail: same missing-case bug
   pattern, but in `fillProperties2Chain` instead of
   `fillFeatures2Chain` — look for `VkPhysicalDeviceSamplerFilterMinmax
   Properties`'s own missing `case`. Should be a near-identical,
   mechanical fix to this session's — probably the fastest remaining
   win available.
2. **(~1 hour)** `L412`'s `shader_subgroup_rotate_property_consistency_khr`
   Fail: trace why `VkPhysicalDeviceSubgroupProperties.supportedOperations`
   doesn't report `VK_SUBGROUP_FEATURE_ROTATE_BIT_KHR`/
   `_CLUSTERED_BIT_KHR` even though the feature bits themselves are
   `VK_TRUE` — look at wherever `supportedOperations` gets built
   (probably `PhysicalDeviceInfo.cpp`).
3. **(~30 min)** `L412`'s `vulkan1p4_limits_validation.general` Fail —
   completely untriaged, bare `Fail (fail)`. Re-run just that one case
   with more verbose logging to get an actual error message first.
4. **(~1-2 hours)** `L412`'s remaining untriaged `part1` buckets: 3
   `binding_model.stages.*`, 2 `api.version_check.*`, 1 each
   `get_device_proc_addr`/`driver_properties` — none yet looked at.
5. **(~2-4 hours, standing item, several sessions old)** `L411`'s real
   residual bug: subpixel rasterization/coverage precision in the
   66-case `b10g11r11_ufloat_pack32` family. Needs a dedicated
   rasterization-focused session — compare FeMe's and the CTS reference
   rasterizer's per-pixel coverage/edge-function values directly near
   the quad boundaries (x=12/x=20), not blend arithmetic.
6. Drop from future handoffs — fully resolved: `L412`'s 12-case ASTC-blit
   sub-bucket (it's `L265`, already deferred, don't re-investigate
   without new diagnostic data), the `api.info.*` 18-Fail bucket (now 3,
   different bugs — see items 1-3 above), `L378(a)`, `L409`/`L410` in
   their entirety, the "46 SIGSEGV + 328 DeviceLost" stability baseline,
   `part1`'s crash mystery, `L407`, the "resource-table exhaustion"
   hypothesis, `L375`, `LLVM_EXTERNAL_PROJECTS=OffloadTest` (`L380`),
   `L402`/`L406`, `pipeline_library.stencil` verification. **Stop citing
   "37 part1 Fails" as one bucket — it's now 3 `api.info` + 12 `L265`
   (deferred) + 10 untriaged (binding_model/version_check/proc_addr/
   driver_properties); track these as separate items going forward.**

