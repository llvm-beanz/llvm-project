---
model: claude-sonnet-5
resume: 1ebfe39e-5f48-4c14-810b-08a0301888a7
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

Please continue working on the FeMe Vulkan ICD. The previous session's suggested
next steps are:

1. **(new, best next pick -- unblocks visibility into the rest of
   `texel_buffer`)** Root-cause the `deqp-vk` process crash at
   `dEQP-VK.texture.texel_buffer.uniform.packed.a2b10g10r10-uint-pack32`:
   `error: Dim must not be SubpassData or Buffer` / an MLIR
   `SampledImageType::get` assertion failure. Start by finding where FeMe's
   SPIR-V lowering constructs a `SampledImageType` for a texel-buffer
   (`samplerBuffer`-shaped) sampled image -- likely a missing/wrong `Dim` enum
   value for the buffer-image case. Determine whether this is a FeMe-specific
   lowering bug (fix in `feme/`) or a genuine MLIR `spirv` dialect bug (fix
   outside `feme/`, needs an isolated reproducer per the standing instruction)
   before touching anything.
2. **(new, ~106 cases, a half-day)**
   `texture.shadow.{1d,1d_array,2d,2d_array,cube,cube_array}` -- 106 failures,
   all "Image verification failed" against a depth-comparison-sampling shape.
   Not yet root-caused. Good next pick after the crash above, since it's the
   single largest untriaged cluster.
3. **(new, ~21 cases, a few hours)** The `rasterization` group's remaining 82
   failures (all stipple/adjacency combinations with Bresenham-mode lines,
   confirmed unrelated to `L312`'s own width fix -- a genuinely different bug in
   the same code path) need their own root-causing session. Standalone repro
   already confirmed:
   `dEQP-VK.rasterization.primitives.dynamic_stipple.bresenham_lines` reports a
   fragment-count mismatch (227 vs. 233 expected) against the diamond-exit rule.
4. **(new, ~16+5 cases, a few hours)** `texture.explicit_lod.2d.sizes.*` (16
   failures, mipmap filtering) and `texture.multisample` (5 failures) --
   smaller, untriaged clusters from the same `texture` sample.
5. **(a few hours, lower priority, carried over many sessions, unchanged)**
   `L265`'s ASTC alpha-decode tie-break (4 block sizes affected). Still needs a
   Mesa/lavapipe reference-decoder comparison -- no new ideas this session (not
   revisited).
6. **(low priority, pre-existing, unrelated to FeMe, unchanged)**
   `offload-test-suite`'s own `spec_const_32_bits.test`/`WaveActiveMax.test`
   lit-annotation issues, and `array_of_matrices.test`'s unexpected-pass. Not
   reconfirmed this session (not revisited) -- needs upstream fixes, outside
   this project's scope.
7. **(housekeeping, due again in ~4 sessions, last done 1 session ago at
   `d0974dd`)** `check-hlsl-feme-vk`/`offload-test-suite` `feme`-branch-drift
   check -- not checked this session (branch untouched); file away for its next
   routine check.
