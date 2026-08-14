# `feme-dxbc-import-fuzzer` seed corpus

This directory holds a small seed corpus for `feme-dxbc-import-fuzzer`:
valid DXBC tokenized bytecode the fuzzer can start mutating from, instead
of an empty corpus. This is one of the intentional places in the FeMe tree
where checked-in binary fixtures are expected (see "Avoiding binary test
fixtures" in [../../../docs/Design.md](../../../docs/Design.md)) — fuzzer
corpora are required to contain real binary samples.

Each `.dxasm` file is the human-readable source the matching `.dxbc`
binary fixture was assembled from, so the corpus stays diffable/reviewable
like the rest of the tree:

- `pixel-shader.dxasm`/`.dxbc`: a small pixel shader covering resource and
  sampler declarations, a texture `sample`, and a plain `mov` (matching
  `dxbc-as-fuzzer`'s own `sample-pixel-shader.dxasm` seed).
- `alu-ops.dxasm`/`.dxbc`: a temp-register-only program covering saturate
  (`_sat`), negate (`-`), absolute value (`| |`), and `movc`.
- `compute-shader.dxasm`/`.dxbc`: a compute shader with a program header,
  a UAV declaration, thread-group size, and structured control flow
  (`if`/`loop`/`breakc_nz`/`endloop`/`endif`), seeding the mutator with
  the header and control-flow-token decoding paths the other two seeds
  don't reach.

Regenerate a binary fixture (e.g. after a breaking change to the DXBC
token format) with `dxbc-as` (see
[../../../docs/CommandGuide/dxbc-as.md](../../../docs/CommandGuide/dxbc-as.md)):

```shell
dxbc-as pixel-shader.dxasm -o pixel-shader.dxbc
```

See [../../../docs/CommandGuide/feme-dxbc-import-fuzzer.md](../../../docs/CommandGuide/feme-dxbc-import-fuzzer.md)
for how to run the fuzzer against this corpus.
