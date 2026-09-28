---
model: claude-sonnet-5
resume: 1ebfe39e-5f48-4c14-810b-08a0301888a7
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

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(a few hours, dedicated session)** `L233`: root-cause
   `vkGetPhysicalDeviceImageFormatProperties`'s ignored `VkImageTiling`
   parameter. Start with the clearest lead --
   `dEQP-VK.api.info.image_format_properties.2d.linear.r32_sfloat`
   (`Fail (sampleCounts != VK_SAMPLE_COUNT_1_BIT)`, a one-line fix:
   force `SampleCounts = VK_SAMPLE_COUNT_1_BIT` whenever
   `tiling == VK_IMAGE_TILING_LINEAR`) -- before tackling the two
   harder, not-yet-root-caused leads (`Invalid dimensions for 1D
   image` on some compressed/depth-stencil 1D-linear formats;
   `maxResourceSize smaller than minimum required size` on at least
   one 3D-linear format). Also check whether
   `dEQP-VK.api.version_check.entry_points`/
   `dEQP-VK.api.get_device_proc_addr.non_enabled` (2 more failures
   from the same sample) share a root cause or are unrelated.
2. **(a few hours, dedicated session)** `L234`: triage 41
   `pipeline.monolithic.*` failures, starting with the 26-case
   `sampler.*` majority (largest single group, likely one shared root
   cause) before the 5 smaller groups
   (`logic_op_na_formats`/`logic_op`/`spec_constant`/`no_position`/
   `render_to_image`/`creation_cache_control`).
3. **(a full dedicated session)** `L232`: the storage `CubeArray`
   `/6` layer-count fix -- needs classification-level surgery
   (distinguishing storage `Cube`/`CubeArray` from `Array2D` without
   regressing their shared, already-passing fetch/store lowering).
   Not a quick follow-up; budget a real session for it.
4. **(a few hours)** Pick one of `L227(d)` (3 `Graphics/MeshShaders/*`
   image-comparison failures) or `L228(b)`/`(c)` (compressed-format
   blits, MSAA multi-layer clears) -- still open, unchanged for many
   sessions now, and `L228(b)`/`(c)` were both re-confirmed still
   active by this session's own broad sample.
5. **(quick, at the very start of the next session)** Re-check
   `offload-test-suite`'s local `feme` branch -- no drift this
   session, but it has drifted at least twice in recent sessions.
   `git log --oneline feme -3` should show `9351791` at the tip; if
   not, `git reset --hard 9351791` restores it.
