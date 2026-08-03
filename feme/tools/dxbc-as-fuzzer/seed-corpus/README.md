# `dxbc-as-fuzzer` seed corpus

This directory holds a small seed corpus for `dxbc-as-fuzzer`: valid DXBC
assembly text the fuzzer can start mutating from, instead of an empty
corpus. This is one of the intentional places in the FeMe tree where
checked-in binary fixtures are expected (see "Avoiding binary test
fixtures" in [../../../docs/Design.md](../../../docs/Design.md)) -- fuzzer
corpora are required to contain real samples of the format under test, and
unlike the *binary*-format fuzzers (`feme-dxil-import-fuzzer`,
`feme-spirv-import-fuzzer`), `dxbc-as-fuzzer`'s input format is itself
text, so these seeds are plain, readable `.dxasm` files rather than binary
blobs.

- `sample-pixel-shader.dxasm`: a small pixel shader covering
  declarations, `sample`, and a plain `mov`, matching the example in
  feme/docs/Design.md's "dxbc-as" section.
- `alu-ops.dxasm`: a temp-register-only program covering saturate (`_sat`),
  negate (`-`), absolute value (`| |`), and `movc`, to seed the mutator
  with the basic operand-modifier shapes.
- `relative-addressing.dxasm`: seeds the mutator with the constructs the
  bare register grammar does not reach -- multi-dimensional and relative
  indices, indexable temps, 64-bit immediates, instruction-level
  modifiers, and both directives (`.shader_model`, `.dword`).

Run the fuzzer against this corpus with:

```shell
dxbc-as-fuzzer feme/tools/dxbc-as-fuzzer/seed-corpus/
```
