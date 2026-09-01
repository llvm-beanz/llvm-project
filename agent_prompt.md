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

Please investigate and fix the issues tracked by milestone L21:

> **`feme::cpu::SIMDizePass`'s `checkVectorDecompositionSupported`-adjacent
> preflight check (the loop over `instructions(*OldF)` in `SIMDize.cpp`, ~line
> 650) rejects any divergent value of aggregate (struct or array) type outright,
> with no component-decomposition path at all** -- found as an L20
> milestone-description correction: with L20's own fix landed,
> `Feature/StructuredBuffer/packed.test`'s own `Doggo Fido = Buf[GI]; ...;
> Buf[GI] = Fido;` whole-struct-copy idiom now converts and lowers cleanly
> through both the SPIR-V-to-LLVM layer (L19) and the CPU resource-lowering pass
> (L20), producing a whole-`Doggo`-struct value that is itself divergent (loaded
> through `GI`, `SV_GroupIndex`, a per-invocation, per-lane-varying index) --
> but `feme-cpu-simdize` (the pass that widens a divergent scalar/vector value
> into its own per-lane-packed `W`-wide form) has never had any aggregate-typed
> case at all: its own preflight loop bails immediately with `'function \'main\'
> has a divergent value ... of aggregate type; component decomposition is not
> yet supported (roadmap milestone 7 deviation)'` the moment it sees one,
> confirmed directly via `FEME_VULKAN_LOG_CREATION_ERRORS=1 offloader`. Distinct
> from, and blocking end-to-end pass independently of, L20's own scope (the CPU
> resource-lowering pass's own raw-load/store mangling) and L15's own scope (a
> `feme.cpu.masked.load/store.*` call producing a *vector* result, not a
> struct/array): this is a generic, long-standing scope limit in the
> wave-widening pass itself, for *any* divergent aggregate value, not specific
> to a resource load. Needs its own scoping pass: likely giving the
> divergent-aggregate value its own per-field/per-element decomposition
> mirroring `widenScalarizedFallback`'s existing per-lane clone-and-reassemble
> (each leaf field/element widened independently, exactly as if it were its own
> separate divergent scalar/vector value), reassembled back into the widened
> aggregate's own per-lane structure with `insertvalue`, and checking whether
> any further consumer of a divergent aggregate (e.g. an
> `extractvalue`/`insertvalue`-chain into an individual field) needs its own new
> recognized shape once the preflight bail is lifted
