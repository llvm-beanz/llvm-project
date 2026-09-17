---
model: claude-sonnet-5
resume: 52e661a0-b284-45ee-892f-3073721ca338
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

1. **Trace L99's actual root cause.** Dump the SPIR-V and/or LLVM IR
   for a passing `mat2` case and the failing `mat2x3` case side by
   side, focused on the matrix's own `OpCompositeConstruct` and the
   `m[i][j]` `OpAccessChain`+`OpLoad` sequence — look specifically at
   whether `getTightVectorArrayType`'s marker-struct substitution
   (H101j) is applied consistently on both the construct side and the
   index/load side for a `vec3` column. Rough estimate: 1–2 hours to
   find the actual divergence, once IR is in hand.
2. **Reduce and scope the other 3 `spec_constant.*` failure buckets**
   (45 `VectorExtractDynamic` legalize failures, 10 `OpTypeArray` count
   failures, ~30 "GEP into vector" failures) — not yet touched this
   session. Each looks like its own distinct gap, not obviously related
   to L99. Rough estimate: 30–60 minutes each to reduce to a single
   case and form a hypothesis, before any fix estimate is possible.
3. **If L99 turns out well-contained, fix it and re-sweep** both
   `composite.matrix.*` (18+ cases) and the full `spec_constant.*`
   group (1170 cases) to confirm the fix's real blast radius — a
   `vec3`-column matrix bug could plausibly affect other GLSL/HLSL
   constructs beyond spec-constant composites (plain matrix literals,
   uniform-block matrices, etc.), so a broader post-fix check is
   warranted before considering it closed.
