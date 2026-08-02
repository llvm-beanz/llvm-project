# Agent thoughts: FeMe roadmap step 1 (Scaffolding)

This records the reasoning behind the changes in this branch, which implement
roadmap step 1 from `feme/docs/Design.md`:

> **Scaffolding**: directory layout, `CMakeLists.txt` wiring into the
> monorepo build, empty `feme::Context`, `feme` skeleton with `--help` only,
> plus `feme-opt` and `feme-translate` testing-tool skeletons ... Should
> include setting up the lit testing environment and adding the `check-feme`
> target to the build.

## Approach

I read the full design doc (particularly the "Core Architectural Principle:
No Global State", `feme::Context`, "Command Line Tool(s)", "Testing Tools",
"Directory / Library Layout", "Build System Integration", and "Testing
Strategy" sections) and `feme/.instructions.md` before writing any code, then
modeled the build scaffolding on the closest existing precedent:
`mlir/examples/standalone` (a template MLIR-dependent sub-project buildable
both standalone and in-tree) for the CMake/lit/gtest wiring shape, and
`flang/CMakeLists.txt` for how an in-tree, MLIR-dependent LLVM sub-project
(`LLVM_ENABLE_PROJECTS=<name>`) integrates with the monorepo build. Since the
design doc explicitly puts standalone (out-of-tree) builds out of scope for
now, `feme/CMakeLists.txt` only supports the in-tree case and fails fast with
a clear message if built standalone.

## What I built

- **`llvm/CMakeLists.txt` / `llvm/tools/CMakeLists.txt`**: registered `feme`
  as a known project (in `LLVM_EXTRA_PROJECTS`, alongside `flang`, since it's
  not ready to be part of `"all"` yet) and made `mlir` an implicit dependency
  when `feme` is enabled, mirroring the existing `flang` block. I initially
  assumed `add_llvm_implicit_projects()` would auto-discover `feme` as a
  sibling top-level project (since it *looks* like a generic "scan the tree"
  mechanism), but testing showed it only globs `llvm/tools/*` for tools
  living inside that directory — top-level sibling projects (`mlir`, `clang`,
  `flang`, `bolt`, ...) are always added via explicit
  `add_llvm_external_project(name)` calls. So I added
  `add_llvm_external_project(feme)` explicitly, ordered after `mlir` since
  feme depends on it. This is the kind of thing that's easy to get wrong
  without actually configuring a build, which is why I verified by actually
  running `cmake -DLLVM_ENABLE_PROJECTS=feme` and checking `ninja -t targets`
  for feme targets before trusting the wiring.
- **`feme::Context`** (`feme/include/feme/Core/Context.h`,
  `feme/lib/Core/Context.cpp`): deliberately minimal per the roadmap's "empty
  feme::Context" wording — it owns a per-session `llvm::LLVMContext` and
  `mlir::MLIRContext`, is movable-but-not-copyable, and does not yet carry
  diagnostics/format-registry members (those are introduced by later roadmap
  steps once there's an actual pipeline to diagnose or register). Covered by
  gtest unit tests asserting construction succeeds and that independent
  `Context` instances never share the underlying LLVM/MLIR contexts, per the
  "No Global State" principle.
- **`feme` CLI skeleton** (`feme/tools/feme/feme.cpp`): `--help`-only, with
  hand-rolled argument scanning rather than `llvm::cl::opt`, since the design
  doc is explicit that `feme` itself must never use `cl::opt` (only
  narrowly-scoped testing tools may). Real option parsing arrives with
  `llvm::opt`/`OptTable` in a later roadmap step once there's something to
  configure.
- **`feme-opt`/`feme-translate` skeletons**: thin `MlirOptMain`/
  `mlirTranslateMain` wrappers with no FeMe-specific dialects, passes, or
  translations registered yet (there's nothing to register until later
  roadmap steps land the `dxsa` dialect, DXIL op-raising pass, etc.). These
  are explicitly allowed to use `cl::opt`-based MLIR tooling conventions per
  the design doc's testing-tool exception.
- **lit environment + `check-feme`**: `feme/test/{lit.cfg.py,
  lit.site.cfg.py.in, CMakeLists.txt}` follow the
  `mlir/examples/standalone/test` pattern, adapted for an in-tree build
  (tools already land in the shared top-level `bin/` via
  `LLVM_RUNTIME_OUTPUT_INTDIR`, so no separate install step is needed for
  lit to find them). `add_lit_testsuite(check-feme ...)` is enough to make
  `check-feme` roll up into the top-level `check-all`, since that's
  implemented as a global "record every `add_lit_testsuite` call between
  `umbrella_lit_testsuite_begin`/`_end`" mechanism in `llvm/CMakeLists.txt`,
  not something each project has to wire up itself. Smoke tests exercise
  `--help` on all three tools (plus a "no args" failure case for `feme`) —
  deliberately shallow, since there's no real functionality yet to test more
  deeply.
- **gtest unittests** for `feme::Context`, using the same
  `add_unittest(FeMeUnitTests ...)` pattern MLIR's own `unittests/`
  directory uses (defining a local `add_feme_unittest` wrapper rather than
  depending on `add_llvm_unittest`, since `llvm/unittests/` is processed
  *after* `llvm/tools/` in `llvm/CMakeLists.txt` and so `add_llvm_unittest`
  isn't defined yet when `feme`'s subdirectory is processed).

## Validation

I did not just write code and assume it works: I actually configured a build
(`cmake -DLLVM_ENABLE_PROJECTS=feme -DLLVM_TARGETS_TO_BUILD=X86
-DLLVM_BUILD_TESTS=ON`) and iterated until:

- `ninja check-feme` builds `feme`/`feme-opt`/`feme-translate` and runs all
  4 lit tests (100% pass).
- `ninja FeMeCoreTests && ./.../FeMeCoreTests` builds and runs the 3
  `feme::Context` gtest unit tests (100% pass).

Along the way this caught two real bugs I wouldn't have found by inspection
alone: the missing explicit `add_llvm_external_project(feme)` registration
described above, a missing `LINK_COMPONENTS Core` on the `FeMeCore` library
(it uses `llvm::LLVMContext`, which lives in `LLVMCore`, not the `Support`
component `add_mlir_library` pulls in by default), and a lit config
incompatibility (`lit.formats.ShTest(not llvm_config.use_lit_shell)` — the
external-shell mode is deprecated as of this LLVM version and errors out;
current in-tree convention is just `lit.formats.ShTest()`).

Finally, I ran `clang-format` (LLVM style) over every new `.cpp`/`.h` file
per `feme/.instructions.md`; it made no changes, meaning the files as
authored already conformed.

## Deliberately deferred to later roadmap steps

- `Options.td`/`llvm::opt`-based argument parsing for `feme` itself (step
  2+, once there's something to configure).
- Diagnostics (`DiagnosticHandler`) and `FormatRegistry` on `Context` (no
  formats exist yet to register or diagnose about).
- Any MLIR dialect registration in `Context`/`feme-opt` (the design doc's
  eager-registration list — `spirv`, `llvm`, `func`, `gpu`, `dxsa`, etc. —
  only makes sense once those pipeline stages exist).
- `Importer`/`Exporter`/`Translator`/`Backend`/`Driver` interfaces (roadmap
  steps 2+).
- Fuzzing harnesses (introduced alongside each importer, per the Testing
  Strategy section — there is no importer yet).

# Agent thoughts: CMake cache script + lib/Frontend stubs

This records the reasoning behind a follow-up set of changes, prompted by a
request to (1) add a CMake cache script for building feme in-tree, and
(2) start a `lib/Frontend` library, built on `llvm::opt`'s TableGen
approach, so `feme` itself stops hand-rolling argument parsing.

## CMake cache script

`feme/cmake/caches/feme.cmake` follows the shape of existing single-purpose
cache files I found across the monorepo (`offload/cmake/caches/Offload.cmake`
being the closest precedent: a short, flat list of `set(... CACHE ...)`
calls, no macros/functions). Since `llvm/CMakeLists.txt` already adds `mlir`
as an implicit dependency whenever `"feme"` is in `LLVM_ENABLE_PROJECTS`
(added in roadmap step 1), the cache file only needs to set
`LLVM_ENABLE_PROJECTS=feme` itself, plus `LLVM_TARGETS_TO_BUILD=Native`
(feme doesn't retarget to native ISA yet) and
`LLVM_INCLUDE_TESTS`/`LLVM_BUILD_TESTS`/`LLVM_ENABLE_ASSERTIONS=ON` for a
development-shaped build. I verified this by actually running
`cmake -G Ninja -C feme/cmake/caches/feme.cmake -S llvm -B <dir>` against a
scratch build directory and confirming it configures cleanly, rather than
just eyeballing the syntax.

## `lib/Frontend`: options parsing without `cl::opt`

The request explicitly asked for a `lib/Frontend` library, which is a
deliberate deviation from Design.md's original `include/feme/Options/` /
`lib/Options/` naming (roadmap step 1 predates this work and never
implemented that directory anyway — it was aspirational). I chose to keep
the user's requested name and update Design.md to match, rather than
silently doing `Options/` instead, per the "when you deviate from the
design document, update it" instruction. I picked `Frontend` (not
`Options`) as a deliberate parallel to Flang's own
`include/flang/Frontend`/`lib/Frontend`, which plays the same role there
(the "argv into an explicit options struct, independent of the CLI binary"
component) — this also leaves room for `FrontendOptions.h`'s
`DriverOptions` struct and `parseArgs` entry point to live alongside the
raw `OptTable`, which a directory named merely `Options/` would have made
feel out of place.

Implementation-wise, I modeled `Options.td`/`Options.h`/`Options.cpp` on
two existing precedents: `clang/include/clang/Options/Options.h` (for the
public `enum ID { ... #include "Options.inc" ... }` pattern, so consumers
can write `Args.hasArg(OPT_help)`) and `llvm/tools/llvm-objcopy`'s
`ObjcopyOptions.cpp` (for the simpler `GenericOptTable`-based
implementation — it computes prefix tables at construction time rather
than requiring the newer `PrecomputedOptTable` machinery, which is
overkill for feme's handful of options). I did consider whether the
`static const FeMeOptTable Table` inside `getOptTable()` violates
Design.md's "No function-local static mutable state, no Meyer's-singleton
managers" principle; I concluded it doesn't, because that principle (per
its own surrounding text about "the same statically-linked component
instance can be safely invoked concurrently... built once and shared
read-only") is specifically about *mutable* state and manager objects, not
about a `static const` table of immutable, TableGen-generated data — and
this is exactly the pattern already used by `clang/lib/Options`.

`Options.td` declares the CLI shape sketched in Design.md's "Command Line
Tool(s)" section (`--from=`, `--to=`, `--target=`, `-o`, `--help`/`-h`,
`--version`) using `OptParser.td`'s `Joined`/`JoinedOrSeparate`/`Flag`
classes; I initially added an explicit `def INPUT : Option<[], "<input>",
KIND_INPUT>;` before realizing `OptParser.td` itself already declares
`INPUT` and `UNKNOWN` (TableGen's "def already exists" error caught this
immediately when I tried to build).

`FrontendOptions.h`/`.cpp` add the layer above the raw `OptTable`: a plain
`DriverOptions` struct (deliberately not `cl::opt` globals, per the "No
Global State" principle) and `parseArgs`, which validates argument counts,
flags unknown options, and populates the struct — except it doesn't yet
feed into anything, since `feme::Driver` doesn't exist yet (that's a later
roadmap step). This is intentionally still "stub"-shaped: it recognizes
the CLI surface and does basic validation, but there's no import/
translate/export pipeline behind it.

Finally, `feme.cpp` itself was updated to call `parseArgs`/`getOptTable`
instead of its previous hand-rolled `-h`/`--help` scan, since the request
was explicit that `feme` itself shouldn't handle argument parsing. The
existing `feme-help.test`/`feme-noargs.test` lit tests kept passing
unmodified, because `OptTable::printHelp`'s
`"OVERVIEW: <title>\n\nUSAGE: <usage>..."` output shape happens to satisfy
their existing `CHECK: OVERVIEW: FeMe...` / `CHECK: USAGE: feme` lines —
I verified this by actually running `ninja check-feme` rather than assuming
the CHECK lines would still match.

## Validation

- Configured a fresh build with `feme/cmake/caches/feme.cmake` to confirm
  it works standalone, then reconfigured the main dev build (assertions
  on, ccache on) to pick up the new `feme/lib/Frontend`,
  `feme/include/feme/Frontend`, and `feme/unittests/Frontend`
  `CMakeLists.txt` files.
- Built and ran `FeMeFrontendTests` (`OptionsTest` + `FrontendOptionsTest`,
  8 gtest cases) and `FeMeCoreTests` (unaffected, still passing) after each
  change.
- Ran `ninja check-feme` (4/4 lit tests) after wiring `feme.cpp` to the new
  library.
- Before committing each of the three `lib/Frontend`-related commits, I
  temporarily removed the not-yet-committed files from the working tree
  (LLVM's build system errors out — "Found erroneous configuration for
  source file..." — if a `.cpp`/`.h` exists in a directory but isn't listed
  in that directory's `CMakeLists.txt`) and rebuilt, to confirm each commit
  is independently buildable and testable, not just that the final state
  works.
- Ran `clang-format` (LLVM style) over every new/modified `.cpp`/`.h` file;
  no changes were made, meaning the files as authored already conformed.

## Deliberately deferred to later roadmap steps

- `feme::Driver` to actually consume a parsed `DriverOptions` and run an
  import → translate → retarget/export chain (roadmap steps 2+, once
  there's a pipeline to drive).
- Marshalling `DriverOptions` fields into typed enums (e.g. validating
  `--from=dxil` against a known set of formats) — today `From`/`To`/
  `Target` are plain strings; that validation belongs with the importers/
  backends that will actually interpret them.
