# FeMe development container

Use the LLVM checkout's `build/` for FeMe and offload-test-suite. Keep the
independent CTS build in `/home/dev/dev/VK-GL-CTS/build/`. The commands below
assume the auxiliary checkouts already exist under `/home/dev/dev/`.

## Prerequisites

The container needs Clang, LLD, CMake, Ninja, ccache, Python with PyYAML and
psutil, DXC with SPIR-V support, SPIRV-Tools, Vulkan headers and loader,
`vulkaninfo`, libxcb development files, and Xvfb. Xvfb enables the optional
xcb smoke tests; without it those tests are skipped, even if libxcb was
found at configure time.

Set a repository-local Git identity before cherry-picking auxiliary changes
or committing. Do not overwrite a pre-existing identity. Increase ccache's
default 5 GB limit if necessary; this container uses 40 GB in
`/home/dev/.cache/ccache`.

## Main build and offload-test-suite

The auxiliary checkout may start at `origin/main` without the FeMe targets.
Fetch the integration branch and inspect its unapplied commits:

```shell
git -C /home/dev/dev/offload-test-suite fetch \
  https://github.com/llvm-beanz/offload-test-suite.git feme
git -C /home/dev/dev/offload-test-suite log --oneline HEAD..FETCH_HEAD
```

Apply the FeMe integration change if its content is absent. At container
setup the change was `0cc94c2`, cherry-picked locally as `35b9d54`.
Compare patch IDs rather than assuming different commit IDs mean drift.
Do not reset or overwrite unrelated auxiliary-checkout changes.

From the LLVM checkout:

```shell
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DLLVM_USE_LINKER=lld -DLLVM_PARALLEL_LINK_JOBS=2 \
  -DLLVM_ENABLE_PROJECTS='clang;feme;mlir' \
  -DLLVM_TARGETS_TO_BUILD='AArch64;AMDGPU;DirectX;NVPTX;SPIRV;X86' \
  -DLLVM_EXTERNAL_PROJECTS=OffloadTest \
  -DLLVM_EXTERNAL_OFFLOADTEST_SOURCE_DIR=/home/dev/dev/offload-test-suite \
  -DDXC_DIR=/usr/local/bin \
  -DGOLDENIMAGE_DIR=/home/dev/dev/offload-golden-images \
  -DLLVM_LIT_ARGS='-sv -j 6'
ninja -C build -j 8 check-feme
```

The target list matters: omitting DirectX or AMDGPU silently skips
translation tests. `check-feme` must build its own tools, unit tests, Vulkan
ICD and smoke clients before running lit; do not mask missing dependencies
by building `all` first.

## Vulkan CTS

Fetch the dependencies pinned by CTS's `external/fetch_sources.py` before
configuring. `python3 external/fetch_sources.py` is the upstream route.
On a clean container, a shallow fetch of each required `GitRepo` revision
avoids downloading entire upstream histories. Do not substitute dependency
tips for those pins. The non-video build needs SPIRV-Tools, SPIRV-Headers,
glslang, Vulkan-Docs, Amber, jsoncpp, VulkanSC-pcutil, zlib, libpng, and the
RenderDoc header. The fetch script also verifies the archive/header hashes.

```shell
cmake -S /home/dev/dev/VK-GL-CTS -B /home/dev/dev/VK-GL-CTS/build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
  -DCMAKE_C_FLAGS_RELEASE='-O3 -UNDEBUG' \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -UNDEBUG' \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
  -DDEQP_TARGET=default -DDEQP_DISABLE_VK_VIDEO_TESTS=ON
ninja -C /home/dev/dev/VK-GL-CTS/build -j 4 deqp-vk
```

`-UNDEBUG` keeps CTS assertions enabled in an optimized build. Disabling
video avoids its large, out-of-scope dependencies; video results must not be
presented as tested implementation coverage.

## Select the driver and run tests

Create `~/.config/feme/env.sh` with the following contents (adjust checkout
paths when using another container):

```shell
export FEME_BUILD_DIR=/home/dev/dev/llvm-project/build
export VK_DRIVER_FILES="$FEME_BUILD_DIR/tools/feme/tools/feme-vulkan/feme_icd.json"
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export FEME_VULKAN_CTS_DEQP_VK=/home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan/deqp-vk
export FEME_VULKAN_SAFETY_NET_TIMEOUT_MS=30000
export XDG_RUNTIME_DIR=/home/dev/.cache/feme-runtime
export PATH="$FEME_BUILD_DIR/bin:$PATH"
```

Create the runtime directory with mode 700. This container's login profile
sources `~/.config/feme/env.sh`. Explicitly source it in non-login shells
too:

```shell
. /home/dev/.config/feme/env.sh
vulkaninfo --summary | grep deviceName
```

Require **FeMe CPU Vulkan Device**. The environment sets both
`VK_DRIVER_FILES` and `VK_ICD_FILENAMES` to the build-tree manifest, supplies
a private `XDG_RUNTIME_DIR`, and sets `FEME_VULKAN_CTS_DEQP_VK` so
`check-feme` discovers CTS. It sets
`FEME_VULKAN_SAFETY_NET_TIMEOUT_MS=30000`; offload lit must preserve this
variable, or the driver's five-second default can expire during Mandelbrot.
The initial system environment selected llvmpipe; do not trust counts
collected with that default.

Run offload tests without a simultaneous CTS sweep or build:

```shell
ninja -C build check-hlsl-feme-vk
ninja -C build check-hlsl-clang-feme-vk
```

Run CTS from its own module directory so Amber assets resolve correctly.
For a full generated list, run `deqp-vk --deqp-runmode=txt-caselist` there
and strip the `TEST: ` prefix from test entries (discard `GROUP: ` entries).
Feed the resulting bare names to `feme/utils/run_vulkan_cts.py` with
`--cwd` set to that same module directory. Keep case lists, QPA files,
process logs and `report.txt` under `/home/dev/dev/VK-GL-CTS/run/`, not
`/tmp`. Verify each QPA session identifies vendor `0x10000`.

The tested auxiliary tips are offload `aae728a` and CTS `0c4f09a1f`.
They include independently committed test/harness corrections, not driver
changes. See [VulkanCTSReport.md](VulkanCTSReport.md) for the isolated
reproducers and final offload counts. A clean auxiliary checkout may not
contain these local commits; inspect/reapply the corrections before
comparing results.

The CTS runner parallelizes initial and recovery batches but must verify
Fail/DeviceLost outcomes with **one worker**. One case per process alone
does not prevent contention. Inspect `report.txt`, not just the runner's
exit code: successful reconciliation can still contain failures or unrun
cases. Preserve assertion-crash logs separately; do not turn missing QPA
completion records into invented Pass results.

Record the exact source revisions, registry and tested case list alongside
results. A sample or partial run is not a replacement for a complete
full-list baseline. Use the inventory generators documented in
[Vulkan14FeatureInventory.md](Vulkan14FeatureInventory.md) and
[VulkanExtensionInventory.md](VulkanExtensionInventory.md); update their
input manifests as well as the rendered tables when advertisements change.
