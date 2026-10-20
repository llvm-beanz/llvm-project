# offload-test-suite patches

`/home/dev/dev/offload-test-suite`'s local `feme` branch is wiped back to a
pure `origin/main` mirror (plus the upstream `854cc3f` FeMe-enabling
cherry-pick) at the start of every session by a root-owned setup script
outside this project's control (see `feme/.instructions.md`). This repo has
no push access to `llvm-beanz/offload-test-suite`, so any local commits made
there during a session do not persist to the next one.

These patches fix genuine, isolated, unit-tested bugs in `offload-test-suite`
discovered while validating the Clang Mandelbrot fix (FeMe Roadmap `ENV3(b)`,
`L464`). Re-apply them after the standard `854cc3f` cherry-pick each session,
in order, until they are upstreamed to `llvm-beanz/offload-test-suite`:

```sh
cd /home/dev/dev/offload-test-suite
git fetch llvm-beanz feme && git cherry-pick 854cc3f
git am /home/dev/dev/llvm-project/feme/offload-test-suite-patches/*.patch
```

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
