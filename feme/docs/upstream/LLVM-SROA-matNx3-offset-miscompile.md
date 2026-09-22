# Draft upstream issue: LLVM `SROA` mis-offsets a `<3 x float>` slice after inlining nested marshaling calls

**Status**: drafted, not yet filed against `llvm/llvm-project` (this *is*
that repo, but filing needs a human to open the actual GitHub issue against
`llvm/llvm-project`'s public tracker, not this local checkout). Recorded
here so a human can file it, and so future FeMe sessions do not re-derive
the same root cause for the `L150`/`L147 ubo.single_basic_type.*.matNx3.*`
cluster. See roadmap `L150` and `feme/docs/VulkanCTSReport.md` for the
original investigation this draft is based on.

## Summary

Running `opt -passes='inline,sroa'` on ordinary, unremarkable LLVM IR --
three levels of small helper functions comparing a `mat3`'s components
column-by-column, each level marshaling its arguments through a local
`alloca` + `store` + pass-pointer-to-callee pattern -- produces a
miscompile: after SROA coalesces several now-inlined, non-overlapping-lifetime
`<3 x float>`-sized allocas into one smaller merged alloca, two of the
merged sub-ranges are read back at the **wrong byte offset** (4 and 20
instead of the correct 0 and 12), silently corrupting the values compared
for matrix columns 1 and 2.

Neither the LLVM inliner alone nor `SROAPass` alone (run on the
*original*, not-yet-inlined functions) reproduces the bug; it requires the
specific alloca-merging opportunity that only exists after inlining flattens
the nested calls into one function with several small, disjoint-lifetime
`<3 x float>`/`float` marshaling allocas.

## Reproduction

See `feme/docs/upstream/sroa_matNx3_offset_miscompile_repro.ll` in this
directory -- a minimally-stubbed but otherwise **real, unmodified** IR
module extracted directly from FeMe's SPIR-V-to-LLVM translation output for
`dEQP-VK.ubo.single_basic_type.std140.highp.mat3.vertex` (only the SPIR-V
resource-handle preamble and two external stage-I/O declarations were
replaced with a plain zero-initialized global buffer and trivial stubs,
respectively, so the file runs standalone under `lli` without any Vulkan
runtime).

```
$ lli sroa_matNx3_offset_miscompile_repro.ll
result=1.000000        # correct

$ opt -passes='inline,sroa' -S sroa_matNx3_offset_miscompile_repro.ll -o repro_opt.ll
$ lli repro_opt.ll
result=0.000000        # WRONG -- same inputs, only inline+sroa applied
```

Bisection notes (see `L150`'s `VulkanCTSReport.md` entry for the full
narrative):

- `opt -passes='sroa'` alone on the *original* (not-yet-inlined) module:
  **correct** (`1.0`) -- SROA on the small, separate, un-inlined functions
  has nothing to miscompile.
- `opt -passes='inline,sroa'` together: **wrong** (`0.0`).
- Manually patching just the two suspect load-offset literals in the
  post-`inline,sroa` IR (`i64 4`/`i64 20` back to `i64 0`/`i64 12`, matching
  the corresponding store offsets) with no other change flips the result
  back to `1.0` -- direct, IR-level, optimization-independent confirmation
  that those two offsets are the exact defect.

Caution for whoever picks this up: **`lli`'s own JIT pipeline applies its
own default optimization level** on load, independent of whatever `opt
-passes=` was fed to it beforehand. Testing `-passes=inline` alone (no
`sroa`) *through `lli`* was observed to be non-deterministic based on
trivial, semantically-inert changes to the IR (e.g., adding `printf`
debug instrumentation flipped the result from wrong back to correct) --
this is very likely `lli`'s own internal optimizer re-applying an
SROA-equivalent transform on top, not evidence that plain `inline` (with
no SROA anywhere in the pipeline) is itself sufficient to trigger the bug.
The `opt -passes='inline,sroa' -S ... ; lli <result>` invocation shown
above is the reliable, reproducible way to observe the miscompile; avoid
drawing conclusions from single-pass `-passes=inline`-only tests executed
through `lli` without also explicitly disabling `lli`'s own optimizer
(`-O0`) first.

## Likely area

`llvm/lib/Transforms/Scalar/SROA.cpp`'s alloca-slicing/merging logic
(`AllocaSlices`, `SROA::runOnAlloca`, and/or the code that widens/rewrites
loads over a vector-typed partition) is the suspected location -- when it
decides multiple originally-distinct, non-overlapping-lifetime allocas can
be represented as byte-ranges within one smaller merged alloca, the
handling for a `<3 x float>` partition appears to compute the read-back
offset using the *wrong* stride (looks like a 16-byte/vector-register
stride is being used for what should be a 12-byte-wide slice, which would
explain the observed +4-byte drift per merged column: `0->4`, `12->20` is
a `+4`, `+8` drift, not a clean stride multiple -- exact arithmetic not
yet traced to a specific line in `SROA.cpp`). A prior, similarly-themed
report exists upstream (`SROA` aggregate-to-vector rewriting, llvm-bugs
#222966) which may be related and worth cross-referencing when filing.

## Suggested filing content

Title: `[SROA] inlining then coalescing multiple <3 x float> marshaling
allocas mis-offsets a merged slice, corrupting the value`

Body: link to `sroa_matNx3_offset_miscompile_repro.ll` in this directory
(attach or paste inline), the two `lli` before/after results above, and
the bisection notes. Note the LLVM version this was found against
(`llvm-project` at the commit checked out for the FeMe `feme` branch --
see `git log -1` at the time of filing).

## FeMe-side mitigation status

Not yet implemented. Candidate approaches for a future session, in
increasing order of invasiveness:

1. Add a small, targeted FeMe-side "widen `<3 x float>` marshaling
   allocas to `<4 x float>`" pre-pass before/instead of relying on
   generic `SROAPass` for this pattern (mirrors the padding workaround
   already used elsewhere in FeMe for 3-wide vector alignment quirks --
   see `getTightMatrixType`/`getTightNestedStructType` in the `L124`
   family of fixes for precedent).
2. Restructure `InlineHelperFunctionsPass` (or add a follow-on pass) to
   avoid producing the specific "several nested small marshaling allocas
   with disjoint lifetimes" shape that gives `SROAPass` the coalescing
   opportunity in the first place -- e.g., by not re-materializing
   already-loaded SSA values back into fresh stack slots when inlining
   flattens a call chain.
3. As a blunt short-term stopgap only if neither of the above lands in
   time: skip/guard `SROAPass` specifically for functions containing
   `<3 x float>`-typed marshaling allocas post-inlining. This would give
   up an optimization opportunity but is not correct as a permanent fix
   and should not be treated as one.
