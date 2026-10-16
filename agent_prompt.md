---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(standing item retired)** `offload-test-suite` drift is now closed for good
   -- don't re-flag "7 ahead / 1 behind llvm-beanz/feme" as a pending decision
   in future sessions. If the ahead/behind counts change (e.g. a genuinely new
   commit appears on `llvm-beanz/feme`), re-check with `git patch-id` the same
   way before assuming it's unresolved drift.
2. **(check-in, cheap)** Keep checking `/tmp/l437_full_run2/` progress each
   session (`ps aux | grep deqp-vk`, `ls /tmp/l437_full_run2/qpa | wc -l`). It
   went from 7 to 9 completed batches across this session alone -- healthy but
   slow going given ~1,800+ total batches remain. If it ever dies, recover using
   the union-of-both-directories technique from `L438` (`l437_full_run` +
   `l437_full_run2`'s `qpa` output combined), not either one alone.
3. **(when the sweep finally completes)** Replace `VulkanCTSReport.md`'s whole
   master per-group table in one pass using the union of completed cases from
   both `/tmp/l437_full_run*` directories, per `L436`/`L438`'s own
   recommendation -- still not actionable yet, sweep nowhere near done.
4. **(standing, still open)** `Basic/Mandelbrot.test` -- confirmed fixed at the
   actual root cause (`L437`), not a flake needing spot-checks; 492/492 clean
   again this session. No further action needed unless it fails again, in which
   case check `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS` reachability first per
   `L437`'s finding.
5. **(idea for a future session with real wall-clock budget)** Once
   `/tmp/l437_full_run2/` finishes, that's the highest-leverage next step
   overall (a full fresh per-group baseline covering ~3.25M cases) -- worth
   prioritizing over any single-family CTS triage pass, since individual-family
   re-sweeps risk re-discovering already-fixed ground (as happened with
   `synchronization` in an earlier session).
