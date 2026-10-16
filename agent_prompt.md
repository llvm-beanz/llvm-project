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

1. **(check-in, cheap)** The full-CTS re-triage (shellId from this session,
   `/tmp/l437_full_run/`) is still running, ~190/1,800+ batches in. A future
   session should check its progress first via `ls /tmp/l437_full_run/qpa/ | wc
   -l` and the report file, and only replace `VulkanCTSReport.md`'s master table
   once it's actually complete -- don't trust a partial count as final.
2. **(standing, now explained, probably closeable as "standing")**
   `Basic/Mandelbrot.test` -- no longer an open flake investigation. It was
   deterministic under sufficient background load and is now fixed at the actual
   root cause (lit env passthrough). A future session doesn't need to keep
   "spot-checking" it as a standing item; if it ever fails again, check first
   whether `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS` is actually present in the
   failing subprocess's own environment (e.g. via `/proc/<pid>/environ`) before
   assuming a new regression.
3. **(standing)** `offload-test-suite` `feme`-branch-vs-`main` drift -- still
   the same single commit (`0cc94c2`), unchanged. This session *did* touch
   `offload-test-suite` (the lit.cfg.py fix), but that's a local,
   uncommitted-upstream change on top of the existing drift, not a resolution of
   the drift question itself -- still needs its own decision from a session that
   wants to actually reconcile it.
4. **(worth a quick look, ~15 min)** Grep
   `offload-test-suite/test/CMakeLists.txt` and `lit.cfg.py` for any *other*
   `FEME_*`-prefixed env vars set on the outer build command the same way
   `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS` was -- if any exist, they likely have the
   exact same silent-no-op bug and are worth checking/fixing in the same pass.
5. **(when the full re-triage finally completes)** Replace
   `VulkanCTSReport.md`'s entire master per-group table in one pass (per
   `L436`'s own recommendation), rather than patching individual rows piecemeal.
