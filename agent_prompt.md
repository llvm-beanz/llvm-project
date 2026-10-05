---
model: claude-sonnet-5
resume: 9d06b2b9-f3c8-4fbb-9416-30f8a58d062b
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

1. **`L377(a)`** (the real fix, ~1 day): make the `CheckBlock`-not-sole-entry
   shape actually linearize instead of just diagnosing it. This is what
   would flip most of the 38,478 cases above from Fail to Pass. Design
   sketch: generalize `ExitBlockRelayValues`/`RelaySource` to treat the
   bypass edge as a second relay source, computing `CheckBlock`'s own
   derived values per-predecessor and merging them at the consumption point
   instead of assuming one source. **Worth checking first: does this share
   a root cause with `L295`'s still-open "two divergent decisions sharing
   one dispatch block" design problem?** Both are about a value computed in
   one block needing to dominate a use reachable via a second, bypassing
   path. If so, one fix might close both.
2. **`L378(a)`** (~1-2 hours once picked up): `max_sampler_lod_bias.shader_bias`
   wrong-pixel bug. `shader_bias_compute` (same LOD-bias logic, compute
   pipeline) passes, so this is likely graphics-fragment-path-specific.
   Next step: `--deqp-log-images=enable` and diff against the golden image.
3. **Stability triage (46 pipeline SIGSEGV + 328 renderpasses DeviceLost) --
   not started this session.** No case names are recorded anywhere from the
   original full-suite run that produced these counts, only aggregate
   numbers. Finding them requires either (a) a full `pipeline.*`/
   `renderpasses.*` rerun (the `pipeline` group alone is ~1.1M cases; at
   this session's ~150K-cases-per-35-minutes rate, that's several hours,
   too long for one session) or (b) narrowing by sub-family first the way
   this session did for `L378` (check `renderpasses` sub-group names for
   a `DeviceLost` cluster, probably "dynamic rendering" or "suballocation"
   per the existing `VulkanCTSReport.md` ordinary-failure breakdown, then
   scope a targeted rerun to just that). Recommend (b) next session --
   start with `renderpasses.dynamic_rendering.*` and
   `renderpasses.suballocation.*` specifically, since those are already the
   two largest ordinary-failure families in that group.
4. **`LLVM_EXTERNAL_PROJECTS=OffloadTest` cache-persistence gap** -- still
   completely unaddressed, carried over from multiple prior sessions.
5. **`check-hlsl-feme-vk` rerun** -- not reverified this session (no
   `Linearize.cpp` behavior change should affect it beyond possibly fixing
   or not-affecting the 3 `WaveActiveBit{And,Or,Xor}.convergence` cases
   `L296` flagged as plausibly related to this same reconvergence territory
   -- worth specifically re-checking those 3 cases next session given this
   session's fix, even though the shape doesn't look identical).
