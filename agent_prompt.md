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

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session's suggested
next steps are:

1. **(dedicated session)** `nested_structs_instance_arrays.31` -- the real fix
   needs the Row math fixed (confirmed correct now) AND a second storage-sizing
   bug found (24-byte overrun past an already-widened 112-byte block). Likely
   geometry-shader multi-vertex-emission related. Next angle: dump the actual
   GEP/store byte offsets the geometry shader's compiled code emits for this
   case directly (not just the resolved Row/Component), to find where the extra
   24 bytes comes from. The underlying architectural gap (distinguishing "array
   of block instances" from "array of struct member" at
   `resolveOffsetWithinElement`'s dispatch point via decoration metadata, not
   just type shape) is still the right long-term fix per the existing code
   comment.
2. **(unknown, new)** `basic_arrays.1` -- not investigated at all yet. Same
   `double free or corruption` signature as `nested_structs_instance_arrays.31`,
   but confirmed pre-existing and (as far as tested) independent. Worth checking
   first whether it's the *same* root cause (likely, given the shared
   `random_geometry.fuzz` generator and identical crash signature) before
   assuming a third distinct bug.
3. **(a few hours)** Still-untriaged tessellation/broader CTS groups from
   several sessions ago: `user_defined_io` (27 cases), `device_group` (7),
   `memory_model.*` races (~24) -- unchanged, still carried over.
4. **(dedicated session, carried over many sessions, unchanged)** `L344` item 2
   / `L335` -- N-barrier generalization, 1+ case (`shader_input_output.barrier`,
   `rasterization.line_continuity.*`).
5. **(lowest priority, many sessions carried over, unchanged)** `L265` -- ASTC
   alpha-decode tie-break, 12 cases.
6. **(a few hours, still overdue, many sessions carried over)** The
   broader-than-tessellation CTS sweep
   (`api`/`pipeline`/`shader_render`/`synchronization`) is still not done.
