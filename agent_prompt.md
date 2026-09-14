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

Can you work on H94 or other blocking work to make progress on the H-series
milestones?

> **`misc.payload_not_accessed`'s bare `SIGSEGV` crash** (1 case, newly
> discovered during a full H70-closing re-triage sweep, confirmed reproducible
> standalone via a fully isolated, shader-cache-disabled `deqp-vk
> --deqp-case=...` invocation, so it is a real, deterministic crash and not a
> shared-shader-cache artifact of the kind this same re-triage session found
> elsewhere): earlier design-doc history (see `L40`'s own closing note) shows
> this exact case previously passed cleanly, so this is a genuine regression,
> not a never-fixed gap, though the regressing commit has not yet been
> identified. A `gdb` backtrace lands in unsymbolized JIT-compiled code (`0x...
> in ?? ()`, "corrupt stack?"), consistent with prior mesh-shader JIT crashes in
> this file's own history, so a real root-cause needs an IR-level reduction
> (`feme-translate`/`feme-opt`, mirroring this file's own repeated technique)
> rather than a native-code debugger session alone. Not yet triaged -- needs its
> own IR reduction to identify which pass/lowering step miscompiles this case's
> specific payload-declared-but-unread shape, and a bisection to identify the
> regressing commit
