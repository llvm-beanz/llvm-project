---
model: claude-sonnet-5
resume: 1b814ab3-f423-4724-ae98-f676112844de
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

1. **(~20-30 min)** `ENV3(c)`: pick 3-5 specific diverging pixels near the
   boundary, dump their iteration count / intermediate `X,Y,Smooth,ColorIdx`
   values from both FeMe's run and a plain double-precision CPU
   re-implementation of the same loop. If they match the golden image's own
   apparent iteration count but FeMe's differs, that points at real lowering
   imprecision, not inherent sensitivity.
2. **(~10 min, only if #1 says "inherent")** Loosen `offload-test-suite`'s
   `Basic/Mandelbrot.test`'s own `rules.yaml` `PixelPercent`/`DiffRMS` tolerance
   to match observed inherent variance; add this as a fourth patch in
   `feme/offload-test-suite-patches/`.
3. **(after ENV3(c))** Move to `ENV4`: full current CTS corpus collection, needs
   a dedicated clean-host block, not an end-of-session afterthought (carried
   forward from every prior session).
4. **(after ENV4)** `ENV5`: partition the 103 SPIR-V instruction failures + 4
   graphicsfuzz failures into independently tested fixes.
5. **(low priority, time-permitting)** Upstream the 3 `offload-test-suite`
   patches to `llvm-beanz/offload-test-suite` if push access ever becomes
   available, to stop needing the every-session `git am` step.
