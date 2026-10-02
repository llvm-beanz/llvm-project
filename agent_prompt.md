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

1. **`L339`** (~a dedicated session) -- TCS barrier-splitting
   architectural gap, 14 cases. Still the biggest real-work item on the
   board, carried over many sessions. Start with a minimal standalone
   reproducer, not the full CTS shader.
2. **`L344` item 2** (~a dedicated implementation session, now
   *scoped* -- see `FeMeGraphicsDesign.md`'s new Status subsection
   before starting) -- generalizing the one-barrier split to N
   barriers. Same subsystem/scope class as `L335`; consider doing both
   in the same session since they need the same kind of control-flow
   region-splitting survey.
3. **`L340`** (~a few hours each, 12 cases) -- `misc_draw.fill_overlap_*`
   (needs `--deqp-log-images=enable` first) and
   `misc_draw.switch_domain_origin_*_fast_lib`. Untouched many
   sessions running.
4. **`L335`** (~a dedicated session, deferred many sessions) --
   `line_continuity.{line-strip,polygon-mode-lines}` region-splitting
   gap. Same class as `L344` item 2 above.
5. **`L265`** (~a few hours, lowest priority, many sessions carried
   over) -- ASTC alpha-decode tie-break. Deep-dived once already (see
   its own `Roadmap.md`/`VulkanCTSReport.md` entries), still
   unresolved, still lowest priority. Untouched again this session.
