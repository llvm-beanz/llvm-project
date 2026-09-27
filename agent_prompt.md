---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
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

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(start here, multi-hour)** `L221`: the quad-domain half of the
   same tessellator-algorithm rewrite. Spec's real approach: build a
   full regular grid across the *whole* domain from the inner
   tessellation levels, discard its outer ring, independently
   re-subdivide the *true* outer edges from the outer levels, then
   bridge the two -- unlike the triangle domain, no recursive ring
   structure, but 4 distinct `m`/`n`-degenerate-axis combinations to
   handle explicitly. Full breakdown already written into the roadmap
   `L221` row (spec citations, suggested implementation order,
   verification plan) -- follow the same rigorous methodology `L220`
   used: exact analytic unit tests validated against real output, full
   crack-free/winding regression sweep, real CTS before/after QPA
   diff.
2. Once `L221` lands, revisit `L216` (`tess_io.max_in_out.with_f16.*`,
   confirmed this session to be quad-domain-specific, not
   triangle-domain) -- strong hypothesis it shares this same root
   cause.
3. A full/broad CTS re-run is overdue (last one: 2026-09-26; L205/
   L206/L208/L210-L214/L217/L218/L219/L220/L201(c) have all landed
   since). Worth doing once `L221` lands, for one consolidated
   before/after picture rather than piecemeal reruns.
4. `check-hlsl-feme-vk` against the `offload-test-suite` `feme` branch
   -- still available per the standing instructions, not touched
   again this session.
5. No git stashes this session (confirmed empty).
