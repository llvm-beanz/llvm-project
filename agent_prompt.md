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

1. **(~5 min)** Clean up this session's scratch files: `/tmp/l124c_*.mlir`
   (superseded by the committed lit test) and `/tmp/l124c_undef.qpa`,
   `/tmp/ctsrun/l124c_compute_sweep.qpa`/`.stdout` -- none referenced
   by anything committed.
2. **The entire L124 series is now closed** except **L124(v)**
   (the 2 `zero_initialize_workgroup_memory` fails,
   `composites.2`/`types.bool`) -- not yet root-caused at all. Start
   with `--deqp-log-decompiled-spirv=enable` on both cases to see
   their exact `Workgroup`-storage global's type shape (likely `i1`/
   `struct`-typed, since matrix/vector/plain-integer shapes already
   pass per this milestone's own opening fix).
3. Beyond L124(v), the next unstarted milestone is **L125**: triage
   `pipeline.monolithic.*` (465,554 cases, never sampled) -- large,
   needs its own first bucketing pass before any concrete repro can be
   picked, mirroring how L106/L123 approached `subgroups.*`/
   `compute.*`.
4. `ninja check-feme` and the CTS build directories are both
   incremental from here -- reuse them, no reconfigure needed.
