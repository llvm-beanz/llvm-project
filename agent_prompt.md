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

1. **(~1 day, dedicated session)** `L407`: attach a leak detector
   (ASan/`LeakSanitizer`, or `valgrind --tool=massif` for a growth
   graph without ASan's slowdown) to a fresh `deqp-vk` build/run
   against a large-but-bounded caselist — the full `dual_source`
   family (8,078 cases, `/tmp/dual_source_all.txt` still on disk) is a
   good target: single test family, previously confirmed to plateau in
   RSS over its first ~125 cases in a quick, non-instrumented run, so
   a leak detector should be able to point at the actual allocation
   site directly instead of just watching aggregate RSS. This is a
   fundamentally different kind of session than code-reading — plan
   for build/tooling setup time (ASan build of FeMe + deqp-vk, or
   getting valgrind to tolerate the ICD's JIT-generated code pages)
   before any actual leak-hunting starts.
2. **(~30 min, quick housekeeping)** Drop `part1`'s
   `use_after_copy.a8b8g8r8_srgb_pack32...` case name from all future
   handoffs — confirmed not a deterministic trigger this session (see
   Finding 2 above). Stop citing it.
3. **(~5 min)** Check `part1` (PID 7228, 365,862+ cases so far, no
   crash yet) and `part2` (PID 14336, 531,232+ cases, 119,904
   `DeviceLost` so far — a 22.6% rate, worth watching whether it keeps
   climbing) again next session before starting new work — either
   could finish or crash with new, actionable information.
4. Drop from future hand-offs — already fully resolved: `L375`,
   `LLVM_EXTERNAL_PROJECTS=OffloadTest` (`L380`), the roadmap
   strikethrough cleanup pass, `L402`/`L406`, `pipeline_library.
   stencil` verification (all prior sessions).
