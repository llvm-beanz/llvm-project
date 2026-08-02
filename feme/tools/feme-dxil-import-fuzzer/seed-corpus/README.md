# `feme-dxil-import-fuzzer` seed corpus

This directory holds a small seed corpus for `feme-dxil-import-fuzzer`:
valid DXIL inputs the fuzzer can start mutating from, instead of an empty
corpus. This is one of the intentional places in the FeMe tree where
checked-in binary fixtures are expected (see "Avoiding binary test
fixtures" in [../../../docs/Design.md](../../../docs/Design.md)) — fuzzer
corpora are required to contain real binary samples.

`minimal.ll` is the textual LLVM IR source both binary fixtures were
generated from, so the corpus stays diffable/reviewable like the rest of
the tree. `minimal.bc` covers `feme::DXILImporter`'s raw-bitcode input path
and `minimal.dxcontainer` covers its `DXContainer`-wrapped input path (see
feme/docs/Design.md's DXIL section for why both encodings are accepted).
Regenerate them (e.g. after a breaking change to either binary format)
with LLVM's own `llvm-as`/`llc`:

```shell
llvm-as minimal.ll -o minimal.bc
llc minimal.ll --filetype=obj -o minimal.dxcontainer
```

See [../../../docs/CommandGuide/feme-dxil-import-fuzzer.md](../../../docs/CommandGuide/feme-dxil-import-fuzzer.md)
for how to run the fuzzer against this corpus.
