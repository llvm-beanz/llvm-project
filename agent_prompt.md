---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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
letter deep going forward.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Please investigate and fix the issues tracked by milestone L14:

> **Re-audit which of this document's own "confirmed via a real
> `check-hlsl-feme-vk`/`feme-vk` rerun" claims (L2 onward) actually ran against
> a correctly-selected `feme_vulkan` ICD**, discovered as an L6
> milestone-description correction: this container's system-wide
> `VK_ICD_FILENAMES` silently defaults to Mesa's `lavapipe` software driver
> rather than `feme`'s own (`feme/.instructions.md`'s new "Running `feme-vk` /
> offload-test-suite against the real driver" section, added this same session,
> documents the required override and a `vulkaninfo`-based sanity check), so any
> session's own `llvm-lit`/`check-hlsl-feme-vk` invocation that omitted it would
> have silently measured `lavapipe`'s own (real, mature, and entirely unaffected
> by any `feme` source change) pass/fail counts instead -- a risk this row's own
> investigation fell into mid-session before catching and correcting it (see
> L6's own row above). A coarse cross-check this session found L5's own recorded
> 133/224 baseline plausible (consistent, modulo the L6-L13 fixes landed since,
> with this row's own freshly-measured, correctly-ICD-selected 137/220
> pre-L6-fix count), but a real per-row audit -- rerunning each of
> L2/L9/L10/L11/L12(a/b/c)/L13's own named "before"/"after" cases individually
> against a correctly-selected `feme_vulkan` ICD and diffing against each row's
> own recorded numbers -- has not been done, and is the actual scope of this
> milestone
