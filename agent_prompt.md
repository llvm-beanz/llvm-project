---
model: claude-sonnet-5
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

**When you find an issue with environment or process**: Codify it in Feme's
.instructions.md file so future sessions don't lose it.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session suggested
the next steps:

1. **ENV3: stop compiler aborts first.** Use
   `assertion-crash-cases.txt`: five SPIR-V 1.4 loop-control cases assert
   on a detached insertion point; one graphicsfuzz unsupported loop
   leaves malformed IR before a terminator assertion. Add regressions
   at each affected translation phase before changing lowering.
2. **ENV3: fix Clang Mandelbrot.** It reproduces alone with a genuine
   SIMDization divergent-branch diagnostic. Increasing the wait budget
   fixes DXC's separate timeout, not this compiler failure. Keep it
   visible rather than adding an XFAIL.
3. **ENV4: collect the full current corpus.** Use the corrected runner
   without competing builds/offload jobs. Execute the remaining cases,
   recover incomplete batches, and verify residuals sequentially.
   The initial multi-case rounds have no process timeout; monitor their
   progress and retain stalled-process evidence rather than claiming
   that the final single-case timeout bounds the whole run.
4. **ENV5: implement contracts, then advertise.** Start with the current
   mandatory-feature failure and partition the 103 SPIR-V instruction
   failures and four graphicsfuzz failures. Older tessellation/graphics
   residuals remain historical until rerun.

**Next action (under two minutes):** open
`feme/docs/Roadmap.md` at ENV3 and the retained
`assertion-crash-cases.txt` to select the first compiler reproducer.
