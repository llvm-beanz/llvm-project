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

1. **(Highest value, ~1 day):** `L145` -- before trusting any future
   full-run numbers, the harness needs a mandatory second pass: re-run
   every case that came back `Fail` or `Crashed`/`TimedOut` in a solo,
   otherwise-idle process, and only report it as real if it reproduces.
   The batch/worker driver script itself isn't in this repo (ad hoc from
   a prior session) -- either find/recreate it or write a small one and
   commit it under `feme/utils/`, wired to call
   `vk_cts_reconcile.py` the same way the existing run did.
2. **Do NOT** restart per-cluster bisection on `L141`/`L142`/`L143`/`L144`
   without a freshly-verified failure list first -- you'll be chasing
   ghosts, as this session's own evidence shows.
3. `L94`'s crash-elimination row is still open but now also suspect --
   don't spend a session on its specific 120 signatures without a fresh,
   `L145`-verified crash list either.
4. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing) --
   still the largest not-yet-started cross-repo item, if a session wants
   real compiler work instead of infrastructure work.
5. No scratch left over to clean up this session (`/tmp/ctsrun/l141/`
   already deleted).
