# offload-test-suite patches

`/home/dev/dev/offload-test-suite`'s local `feme` branch is wiped back to a
pure `origin/main` mirror (plus the upstream `llvm-beanz/feme` tip-commit FeMe-enabling
cherry-pick) at the start of every session by a root-owned setup script
outside this project's control (see `feme/.instructions.md`). This repo has
no push access to `llvm-beanz/offload-test-suite`, so any local commits made
there during a session do not persist to the next one.

These patches fix genuine, isolated, tested bugs in `offload-test-suite`
discovered while validating the Clang Mandelbrot fix (FeMe Roadmap `ENV3(b)`,
`L464`) and the 2026-11-03 clean-container setup. Re-apply them after the
standard `llvm-beanz/feme` cherry-pick each session,
in order, until they are upstreamed to `llvm-beanz/offload-test-suite`:

```sh
cd /home/dev/dev/offload-test-suite
git remote add llvm-beanz https://github.com/llvm-beanz/offload-test-suite.git  # no-op if present
git fetch llvm-beanz feme && git cherry-pick llvm-beanz/feme
git am --keep-cr /home/dev/dev/llvm-project/feme/offload-test-suite-patches/*.patch
```

Use `--keep-cr`, not plain `git am`: `test/WaveOps/WaveActiveMax.test` has
upstream CRLF line endings, and plain `git am` strips the trailing `\r` from
unchanged context lines while reading the patch, which makes patch 0004 fail
to apply with a misleading "patch does not apply" even though its content is
correct.

Then re-run `cmake .` in the `llvm-project` build directory so the new
`unittests/Image/ImageTranslateTests.cpp` source file and the `ImageTests`/
`check-hlsl-*-vk` targets pick up the change.

## Patch summary

1. `0001-...-channel-fill-skip-...patch` -- fixes an unsigned-integer
   underflow in `lib/Image/Image.cpp`'s `translatePixelData` that crashed
   (eventually, via out-of-bounds walk) whenever source and destination
   channel counts differ (e.g. a 4-channel `float4` render target copied to
   a 3-channel 8-bit PNG).
2. `0002-...-convertColor-...patch` -- fixes `include/Image/Color.h`'s
   `convertColor` to map `NaN` to `0.0` and tolerate small floating-point
   overshoot instead of hard-asserting strict `[0,1]` normalization;
   unclamped shader math can legitimately produce both for severely
   out-of-range inputs (confirmed via Mandelbrot's own unclamped
   `log2`/`sqrt` math), and real GPU hardware saturates on UNORM writes
   rather than aborting.
3. `0003-...-lit-cfg-...patch` -- adds `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`
   to `test/lit.cfg.py`'s `with_system_environment` passthrough list, so a
   shell's override of FeMe's wait-safety-net bound is no longer silently
   dropped for lit-driven runs (lit's default `config.environment` is a
   curated whitelist, not a copy of the ambient environment).
4. `0004-...-WaveActiveMax-...patch` -- uses four unique, in-bounds
   invocations for four-element buffers, eliminating out-of-bounds accesses
   and output write races. Checks the specified negative-infinity reduction
   on Vulkan and removes the stale arm64 lavapipe expected failure. The
   isolated test passes with both DXC and Clang on FeMe and lavapipe.
5. `0005-...-DXC-push-constant-...patch` -- excludes FeMe from the stale
   DXC matrix-array expected failure, without changing expectations for
   Clang or other drivers. The isolated FeMe DXC test passes; the Clang
   configuration remains an expected failure.
