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

1. **(check-in, cheap, next session)** Check `/tmp/l437_full_run2/` progress the
   same way this session checked `/tmp/l437_full_run/`: `ps aux | grep deqp-vk`
   to confirm it's still alive (it should be now -- it's detached), and `ls
   /tmp/l437_full_run2/qpa | wc -l` for a progress count. If it's somehow dead
   anyway, repeat this session's recovery technique (extract completed cases
   from **both** `l437_full_run` and `l437_full_run2`'s `qpa` output this time,
   recompute the remainder, relaunch again detached).
2. **(when the sweep eventually finishes, in either output dir or both)**
   Replace `VulkanCTSReport.md`'s whole master per-group table in one pass using
   the **union** of completed cases from every `/tmp/l437_full_run*` directory,
   not just the most recent one.
3. **(standing)** `offload-test-suite` `feme`-branch-vs-`main` drift -- still
   the same single commit (`0cc94c2`), unchanged, still needs a decision from a
   session actually touching that repo for its own sake (not just incidentally,
   as both `L437` and this session did).
4. **(no longer standing, confirmed resolved)** The `FEME_*` env var audit is
   done -- only one existed, already fixed. Don't re-run this check unless a new
   `FEME_*`-prefixed var is added to `gpu_suite_env` in the future.
5. **(no longer standing, confirmed resolved)** `Basic/Mandelbrot.test` --
   492/492 `check-hlsl-feme-vk` passed cleanly again this session with the sweep
   running concurrently. Treat as fixed, not a flake to keep spot-checking.
