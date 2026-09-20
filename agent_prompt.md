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

1. **(~1 session, ABI-touching)** Implement `L125(v)`: add real integer
   border-color storage to `FemeSamplerDescriptor` (`RuntimeABI.h`,
   growing the struct since the existing `Reserved[3]` headroom is one
   word short of 4 more `int32_t`s), a `mapBorderColorInt`-style
   resolver in `Image.cpp`'s `Sampler` construction (mirroring
   `mapBorderColor`'s own `TRANSPARENT_BLACK`/`OPAQUE_BLACK`/
   `OPAQUE_WHITE` cases as integer 0/1 literals), and a
   `femeRTExpandBorderColorForFormatI32` counterpart to the float
   path's `femeRTExpandBorderColorForFormat` in `FeMeRuntimeCPU.c`,
   applied at all 7 sites in place of today's hardcoded `{0, 0, 0, 1}`.
   Start by re-reading `mapBorderColor`/`femeRTExpandBorderColorForFormat`
   side by side to confirm the exact per-format masking rule (numComp
   truncation + forced alpha=1) applies identically to the int path
   before touching the ABI struct.
2. Once `L125(v)` is fixed, re-run the full `border_swizzle.r16*` sweep
   to confirm the remaining 632 fails close (or reveal a third,
   still-narrower root cause).
3. `L125(p)`/`L125(s)`/`L125(t)`/`L125(u)` remain untouched from prior
   sessions' decomposition -- good alternative picks if `L125(v)`'s
   ABI work stalls or needs a design pause.
4. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
5. Clean up `/tmp/ctsrun/l125q2/*` (this session's own scratch
   QPA/console-log files) before ending a future session.
