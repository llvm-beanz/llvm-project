# Switch Lowering Engineering Notes

This file records the high-level engineering decisions and validation results
for the switch-lowering change. It is a concise decision log rather than a
transcript of private chain-of-thought reasoning.

## Requirements

- Add a Clang code-generation option that prevents source `switch` statements
  from becoming LLVM `switch` instructions.
- Enable the option by default for HLSL targeting logical SPIR-V.
- Allow users of other languages and targets to opt in, and allow HLSL SPIR-V
  users to opt back out.
- Preserve source-language switch behavior, including fallthrough, a `default`
  label in any lexical position, case ranges, nested switches, side-entry case
  labels, `break`, cleanup scopes, debug locations, HLSL control-flow hints,
  and profile instrumentation.
- Produce structured SPIR-V conditional control flow without `OpSwitch`.

## Design

The public spelling is `-fno-switch`, with `-fswitch` as its inverse. The
marshalled default is true only when the language is HLSL and the target is a
logical SPIR-V triple.

A simple comparison tree was rejected because a fallthrough edge from one case
body directly into another case body can enter the middle of a structured
selection. Instead, Clang emits lexical case regions as a sequence of guarded
regions:

1. The switch condition is evaluated once.
2. Each case compares that saved value with its case value or range.
3. A PHI records whether the preceding case fell through.
4. The case body executes when either the comparison matches or the
   fallthrough state is true.
5. Both paths join at the next case's dispatch block.

The `default` predicate is the conjunction of all non-default case mismatches.
This allows `default` to appear anywhere while preserving later fallthrough.

A dedicated initial dispatch block supports valid C/C++ side entries such as a
case label nested in a loop. Consecutive labels are emitted iteratively to avoid
stack exhaustion on large switches. Instrumented builds retain separate direct
and fallthrough edges so case counters continue to exclude fallthrough
executions, matching existing Clang profiling behavior.

The existing switch condition scope and break destination remain in use.
Normal and unmatched completion paths join before forcing condition-scope
cleanups, so destructors execute on every exit path. HLSL `[branch]` and
`[flatten]` metadata is copied to generated conditional branches.

## Test Coverage

Focused tests cover:

- Driver forwarding and last-option-wins behavior.
- Explicit `-fno-switch` and `-fswitch` LLVM IR generation.
- HLSL SPIR-V default behavior and HLSL DXIL unchanged behavior.
- Fallthrough through a lexically placed `default`.
- Consecutive cases, GNU case ranges, nested switches, empty switch bodies,
  and case labels nested in loops.
- Profile counters on direct case entry versus fallthrough.
- C++ init-statement destructor cleanup.
- HLSL control-flow hint metadata.
- Final SPIR-V assembly containing structured conditional branches and no
  `OpSwitch`.
- A generated 20,000-label stress case.

The four focused lit tests pass.

## Aggregate Validation

The build was configured with:

```text
cmake -S llvm -B build-agent -G Ninja -C /opt/llvm-tooling/Config.cmake
```

The requested aggregate targets were run. Their failures are unrelated to the
switch changes:

- `check-llvm`: two filesystem-sensitive failures
  (`ThinLTO/X86/cache.ll` left an extra cache entry and
  `strip-preserve-atime.test` observed a current timestamp).
- `check-clang`: the module-cache pruning lit test and two related unit tests
  failed due to filesystem timestamp/pruning behavior.
- `check-hlsl-vk`: eight floating-point edge-case runtime failures. This suite
  invokes the external DXC compiler, and the mismatches concern NaNs, signed
  zero, infinities, and ULP tolerances.
- `check-hlsl-clang-vk`: the same eight floating-point edge-case failures plus
  `mad.32.test`, also involving floating-point result differences.

The failures do not exercise switch statements. Focused Clang IR and SPIR-V
tests for this change pass.

## SPIR-V CFG Structurizer Follow-up

The frontend tests established that `-fno-switch` emits conditional branch
chains, but the backend had no IR-level coverage for the distinct fallthrough,
break-only, and mixed shapes. Testing those shapes with `spirv-val` found that
fallthrough and mixed control flow were accepted, while three break-only cases
produced invalid structured SPIR-V.

The failure came from using `PartialOrderingVisitor::partialOrderVisit` to
collect construct blocks. Returning `false` at one merge boundary truncates the
entire partial-order traversal at that rank; it does not merely stop following
that CFG path. The incomplete block set caused an outer selection merge to
route control into a nested selection's merge block.

The structurizer now uses the existing path-sensitive CFG visitor for both loop
and selection construct collection. Encountering a merge or outside block
prunes only that path, so all other dominated construct blocks remain visible
to exit-edge analysis. This also removes unnecessary proxy selections from two
existing short-circuit loop tests.

The new IR regression test models all three `-fno-switch` shapes, checks their
selection merges, rejects any generated `OpSwitch`, and validates the binary
with `spirv-val`. The full `llvm/test/CodeGen/SPIRV` suite passes.

The aggregate targets were rerun after configuring with
`cmake -C /opt/llvm-tooling/Config.cmake`. They retained the unrelated
filesystem-sensitive and floating-point failures listed above; no failure
exercises switch lowering or SPIR-V CFG structurization.
