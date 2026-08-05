---
model: claude-opus-5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled.

When you deviate from the design document please update the design document.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

Please continue with the work to remove the .ref files. Your previous statements
are:

Forty-nine fixtures, in five groups:

1. **The `_s` feedback variants and `check_access_fully_mapped`** (~8
   fixtures: `sample3`, `sample_b1`, `sample_l1`, `sample_grad1`,
   `sample_cmp1`, `sample_cmp2`, `gather*`). The sampling operations
   themselves are done; what these need is the extra destination that
   takes the `ResRet`'s fifth field, and the `check_access_fully_mapped`
   that consumes it -- which the importer currently leaves as a generic
   `dxsa.instruction` with `dxsa.operand` values rather than a typed op,
   so it needs modelling first. This is the cheapest remaining group and
   the one I would do next.
2. **Buffers and UAVs** (~15): `ld_raw`, `ld_structured`, `store_*`, the
   typed UAV loads and stores, the atomics, `bufinfo`, `resinfo`, and
   group-shared memory as an `addrspace(3)` global. The handle machinery
   is in place; what is left is the per-operation argument shapes.
3. **Doubles** (~6). `ddiv`/`dfma`/`dtof`/`dmov` and the pairing of two
   32-bit components into one `double`.
4. **Subroutines** (~5): `label`/`call`/`fcall`. dxilconv inlines them,
   which is why its block names carry a `label0.callc0.` prefix --
   visible in `loop5` even though that shader has no subroutine.
5. **Stage-specific declarations** (~6): the hull shader phases and the
   geometry shader's `emit`/`cut`.

There is also one thing I found and did not do. `indexableinput1` and
`indexableinput2` read a signature register at a run-time row, which now
works, but their *element numbering* still differs: dxilconv collapses
the registers a `dcl_indexrange` spans into a single signature element
with `Rows` set to the range's length, where this translation keeps one
element per register. Getting those two fixtures exact means merging the
elements a range covers and computing the row of every read within a
merged element, not just the indexed ones -- which changes the numbering
of every element after it, so it is a change worth measuring against all
the signature-carrying fixtures at once rather than bolting on.
