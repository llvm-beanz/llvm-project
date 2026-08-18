# -*- Python -*-

import os
import shutil

import lit.formats
from lit.llvm import llvm_config

# Configuration file for the 'lit' test runner.

config.name = "FEME"
config.test_format = lit.formats.ShTest()

# suffixes: A list of file extensions to treat as test files.
config.suffixes = [".test", ".mlir", ".ll", ".dxasm", ".hlsl"]

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.feme_obj_root, "test")

config.excludes = ["Inputs", "CMakeLists.txt", "README.txt", "LICENSE.txt"]

# Targets: expose a `<arch>-registered-target` feature per LLVM target
# configured into this build, mirroring llvm/test/lit.cfg.py, so tests
# needing a specific codegen target (e.g. the SPIR-V "null pipeline", see
# feme/docs/Design.md) can `REQUIRES:` it rather than failing on builds that
# don't configure that target.
for arch in config.targets_to_build.split():
    config.available_features.add(arch.lower() + "-registered-target")

llvm_config.use_default_substitutions()

# The host's own default target triple, for tests exercising `feme
# --target=<host-triple>` (the FeMe CPU target, see
# feme/docs/FeMeCPUDesign.md) without hard-coding an architecture.
config.substitutions.append(("%feme_host_triple", config.target_triple))

# The CFG restructurization differential harness helper (see
# feme/utils/feme-run-differential.py and roadmap step R1 in
# feme/docs/Roadmap.md): lets a test diff a `feme-cfg-gen` seed's normal
# (widened) `feme-run` output against `--reference` across a seed list and
# a wave-size list with one `RUN:` line instead of one per (seed, wave
# size) pair.
feme_run_differential = os.path.join(
    config.test_source_root, "..", "utils", "feme-run-differential.py"
)
config.substitutions.append(
    (
        "%feme-run-differential",
        "'%s' %s" % (config.python_executable, feme_run_differential),
    )
)

# The wave-size sweep helper (see feme/utils/feme-wave-size-sweep.py and
# roadmap step R1's §2.4.1 prerequisite in feme/docs/Roadmap.md): runs
# `feme-run` once per `--wave-sizes` entry, `FileCheck`ing each run against
# the same input, so a wave-size-independent end-to-end HLSL test opts into
# running at every wave size in one substitution instead of one `feme-run |
# FileCheck` pipeline per wave size.
feme_wave_size_sweep = os.path.join(
    config.test_source_root, "..", "utils", "feme-wave-size-sweep.py"
)
config.substitutions.append(
    (
        "%feme-wave-size-sweep",
        "'%s' %s" % (config.python_executable, feme_wave_size_sweep),
    )
)

# The Vulkan-CTS case-list filter (see feme/utils/filter_vulkan_cts_cases.py
# and the file comment above's "system-vulkan-cts" note).
filter_vulkan_cts_cases = os.path.join(
    config.test_source_root, "..", "utils", "filter_vulkan_cts_cases.py"
)
config.substitutions.append(
    (
        "%filter_vulkan_cts_cases",
        "'%s' %s" % (config.python_executable, filter_vulkan_cts_cases),
    )
)

tool_dirs = [config.feme_tools_dir, config.llvm_tools_dir]
tools = [
    "feme",
    "feme-opt",
    "feme-run",
    "feme-render",
    "feme-cfg-gen",
    "feme-translate",
    # See the "dxbc-as" section of feme/docs/Design.md: a standalone DXBC
    # assembler with no MLIR/feme::Context dependency, used to build
    # human-readable DXBC test fixtures at test time (see "Avoiding binary
    # test fixtures" below).
    "dxbc-as",
    # DXIL (see feme/docs/Design.md's DXIL section) is plain LLVM bitcode,
    # optionally wrapped in a DXContainer; tests build fixtures at test time
    # with these existing, upstream LLVM tools instead of checking in
    # binary fixtures (see "Avoiding binary test fixtures" in
    # feme/docs/Design.md).
    "llc",
    "llvm-as",
    # Used by dxil-raise-ops-roundtrip.ll to produce a fixture with LLVM's
    # own `-dxil-op-lower` pass, so that test validates feme's op-raising
    # pass against real DXIL-lowering output rather than only hand-written
    # `dx.op.*` calls.
    "opt",
    # Used to assemble complete legacy DXBC containers at test time: yaml2obj
    # builds the parts LLVM's ObjectYAML models structurally or that a test
    # authors verbatim (e.g. ISGN/OSGN), llvm-objcopy merges in the raw
    # bytecode part dxbc-as produces, obj2yaml inspects the result, and
    # split-file keeps the YAML and the .dxasm it is paired with in one
    # self-contained test file (see feme/docs/Design.md's "dxbc-as" section).
    "split-file",
    "yaml2obj",
    "obj2yaml",
    "llvm-objcopy",
    "llvm-readobj",
    "llvm-nm",
    # Compiles libFeMeRuntimeCPU's C source (see
    # feme/runtime/CPU/FeMeRuntimeCPU.c and "Runtime Support Library" in
    # feme/docs/FeMeCPUDesign.md) to bitcode at test time, the same way the
    # build itself does (feme/runtime/CPU/CMakeLists.txt).
    "clang",
]

llvm_config.add_tool_substitutions(tools, tool_dirs)

# `dxc` (the Microsoft DirectX Shader Compiler) is an external tool this tree
# does not build, unlike every other tool above -- it is the only way this
# tree can turn real HLSL into a SPIR-V binary carrying a *structured*
# selection/loop construct with genuine merge instructions (the shape real
# Vulkan shaders take; `spirv-registered-target`'s own `llc`-built fixtures
# never carry those, since LLVM's SPIRV backend does not target the
# `Shader`/structured-CF profile). Tests validating roadmap milestone V0.5's
# "a glslang/DXC/Clang corpus" (feme/docs/Roadmap.md) against real compiler
# output use `REQUIRES: system-dxc` and `%dxc` so they skip cleanly wherever
# `dxc` is not installed, the same way `system-vulkan-loader` gates the
# Vulkan loader below.
_dxc = shutil.which("dxc")
if _dxc:
    config.available_features.add("system-dxc")
    config.substitutions.append(("%dxc", _dxc))

# "V0: Loader-visible skeleton" (feme/docs/FeMeVulkanDesign.md): only
# available when configured with Vulkan-Headers and a real Vulkan loader to
# link the smoke-test client against (see feme/CMakeLists.txt's
# `FEME_HAVE_VULKAN_LOADER`); tests needing either the ICD or a loader to
# run through use `REQUIRES: system-vulkan-loader`.
if config.feme_have_vulkan_loader == "ON":
    config.available_features.add("system-vulkan-loader")
    llvm_config.add_tool_substitutions(
        ["feme-vulkan-loader-smoke", "feme-vulkan-storage-buffer-diff",
         "feme-vulkan-image-loader-smoke",
         "feme-vulkan-sampled-image-smoke",
         "feme-vulkan-graphics-smoke"],
        tool_dirs,
    )
    config.substitutions.append(
        ("%feme_vulkan_icd_manifest", config.feme_vulkan_icd_manifest)
    )

    # The two-ICD coexistence test ("Process Coexistence and Symbol
    # Visibility": "verified by a test that runs a client with both the
    # FeMe manifest and a system driver manifest visible") needs a second,
    # genuinely LLVM-based ICD manifest already installed on the test host.
    # Mesa's lavapipe (`lvp_icd*.json`) is the common case on Linux CI
    # images and developer machines alike; this looks for it in the
    # standard system ICD directory rather than assuming a fixed name,
    # since distributions suffix it differently (e.g. an architecture
    # multiarch tag).
    _icd_dir = "/usr/share/vulkan/icd.d"
    _lavapipe_manifest = None
    if os.path.isdir(_icd_dir):
        for _name in sorted(os.listdir(_icd_dir)):
            if _name.startswith("lvp_icd") and _name.endswith(".json"):
                _lavapipe_manifest = os.path.join(_icd_dir, _name)
                break
    if _lavapipe_manifest:
        config.available_features.add("system-second-vulkan-icd")
        config.substitutions.append(
            ("%system_second_icd_manifest", _lavapipe_manifest)
        )

    # V4 ("first CTS runs over the advertised subset", see
    # feme/docs/FeMeVulkanDesign.md's V4 status note): Vulkan-CTS's
    # `deqp-vk` binary is a separate, large upstream project this tree does
    # not build or vendor -- tests that run it use `REQUIRES:
    # system-vulkan-cts` so they skip cleanly (rather than fail) on a host
    # that has not built/installed it, the same way `system-dxc` gates the
    # DXC-corpus tests above. `FEME_VULKAN_CTS_DEQP_VK` overrides the
    # default `deqp-vk`-on-`PATH` lookup for a build that vendors it
    # elsewhere.
    _deqp_vk = os.environ.get("FEME_VULKAN_CTS_DEQP_VK") or shutil.which("deqp-vk")
    if _deqp_vk:
        config.available_features.add("system-vulkan-cts")
        config.substitutions.append(("%deqp_vk", _deqp_vk))
