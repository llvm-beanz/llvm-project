---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

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

# Request

Can you work on the H-series milestones?

The previous session suggested the next steps:

1. **Investigate the remaining ~63 `mesh_shader.ext.*` failures** — 40
   `api.draw*`/`api.draw_indirect*` cases all sharing `with_task_shader`/
   `with_task_shader_secondary_cmd` suffixes (likely one shared root
   cause), `misc.no_lines`/`no_points`/`no_triangles` (3 cases),
   `properties.max_mesh_output_components` (1 case), 3
   `smoke.*.fullscreen_gradient` (already known pre-existing per H76),
   and 16 `synchronization.*` cases. None yet triaged beyond this list.
   ~15-30 min each for a first diagnostic; the `with_task_shader` bucket
   alone is worth ~40 cases if it's one root cause.
2. **Find or construct a real CTS-level repro for H105.** It's currently
   proven only by a hand-written unit test; a real `groupshared`-heavy
   compute or mesh case with no explicit `align` and a large enough
   struct might expose it on this host. ~30-45 min to search/construct.
3. **H96 is genuinely closed** (confirmed this session, contrary to the
   prior session's own belief it was still open) — no more chunked-batch
   workaround needed for full CTS runs going forward.
