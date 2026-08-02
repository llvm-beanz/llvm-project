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

# Agent thoughts: FeMe roadmap step 2 (SPIR-V import)

This records the reasoning behind the follow-on changes implementing
roadmap step 2 from `feme/docs/Design.md`:

> **SPIR-V import**: wrap MLIR's existing `spirv` deserializer behind
> FeMe's `Importer` interface; round-trip test (SPIR-V in → `spirv` dialect
> text out); add a fuzzing harness for the SPIR-V importer.

## Approach

I re-read the whole design doc (particularly "Pipeline Abstraction:
Importers, Translators, Exporters, Backends", "`feme::Module`", "SPIR-V →
MLIR `spirv` dialect (reuse, do not reinvent)", "Testing Tools", "Avoiding
binary test fixtures", and "Testing Strategy") plus `feme/.instructions.md`
before writing anything, then worked bottom-up through the primitives the
roadmap item actually needs: `feme::Module` (the currency type `Importer`
hands back), `feme::Importer` (the interface), `feme::SPIRVImporter` (the
concrete wrapper around `mlir::spirv::deserialize`), wiring into
`feme-translate`, then the fuzzer. I looked at MLIR's own
`mlir/lib/Target/SPIRV/TranslateRegistration.cpp` as the reference
implementation for the deserialize-and-report-errors shape, and
`mlir/tools/mlir-parser-fuzzer` as the reference for the fuzzer harness
shape.

## `feme::Module`: three deviations from the design sketch, each documented

1. **`fromMLIR` is a function template, not `OwningOpRef<mlir::ModuleOp>`.**
   The design sketch has `fromMLIR` take the builtin `mlir::ModuleOp`
   specifically. But `mlir::spirv::deserialize` returns an
   `mlir::spirv::ModuleOp` — SPIR-V's own top-level op, not wrapped in a
   builtin module — and future formats (DXBC's `dxsa` dialect) will have
   their own top-level ops too. Making `fromMLIR` a template accepting any
   op type, type-erasing internally to `OwningOpRef<mlir::Operation *>`,
   avoids forcing every format through a builtin `ModuleOp` it doesn't
   actually produce. `getMLIRModule()` becomes `getMLIROperation()`
   returning `mlir::Operation *`; callers that know the concrete format
   `mlir::cast`/`dyn_cast` it back. Documented in Module.h's class comment
   and in Design.md.
2. **`takeMLIROperation()` was added** (not in the original sketch) once I
   started wiring `feme-translate`: MLIR's `TranslateToMLIRRegistration`
   expects the translation function to return an `OwningOpRef<Operation *>`
   it will own from then on, but `feme::Module` (and the `Context`/
   `Importer` it was constructed from) go out of scope at the end of the
   registration lambda. Without a way to release ownership out of `Module`,
   the temporary `Module`'s destructor would `erase()` the very operation
   just handed back to the caller (a use-after-free). `takeMLIROperation()`
   moves the `OwningOpRef` out, mirroring `OwningOpRef::release()`'s own
   semantics. Documented in Module.h and Design.md, with a dedicated gtest
   case (`TakeMLIROperationTransfersOwnership`).
3. **A latent header bug, found and fixed before it could bite `SPIRVImporter`:**
   `Module`'s implicitly-defaulted move constructor/assignment (`= default`
   inline in the header) instantiated `std::unique_ptr<llvm::Module>`'s move
   operations wherever `Module.h` was included — which only happened to
   compile in TUs that also (transitively) included `llvm/IR/Module.h`.
   `SPIRVImporter.cpp` doesn't need `llvm/IR/Module.h` directly and hit a
   "sizeof application to incomplete type" error building `FeMeImportSPIRV`.
   Fixed by declaring the move operations in the header and defining them
   `= default` out-of-line in `Module.cpp` (same pattern already used for
   the destructor). I then proactively applied the identical fix to
   `feme::Context` in the same batch of work, since it had the exact same
   shape of bug (unique_ptr<LLVMContext>/<MLIRContext> members with an
   inline-defaulted move assignment) even though nothing currently
   triggered it — better to fix it now while making an unrelated,
   API-compatible `Context` change (see next) than leave a landmine for
   whoever moves a `Context` from a TU that hasn't pulled in the full
   LLVMContext/MLIRContext headers.

## `feme::Importer`/`ImportOptions`: one deviation, forced by the no-RTTI rule

The design sketch has `Importer::import` take a single `const ImportOptions
&Opts`, implying (though not stating outright) that different formats might
want their own options subtype. `feme/.instructions.md` bans RTTI, though,
so `Importer::import` implementations cannot safely
`static_cast`/`dynamic_cast` a base `ImportOptions&` down to a
format-specific subtype without either RTTI or a hand-rolled type tag (which
would just be RTTI by another name). Rather than fight this, I made
`ImportOptions` a single plain, non-polymorphic struct shared by all
formats, holding one field per format-specific knob (currently just SPIR-V's
control-flow-structurization toggle, prefixed `SPIRV*` to make the
provenance obvious). This is explicitly flagged as "expected to grow" in
both `Importer.h` and Design.md, so future format authors know the pattern
to follow (add a field, don't add a subtype).

## `feme::Context`: added a wrapping constructor (not strictly a deviation — Design.md already anticipated this)

Design.md's "feme::Context" section already says Context "Owns (or wraps
caller-provided) LLVMContext and MLIRContext instances", but the step-1
implementation only ever constructed its own. Wiring `SPIRVImporter` into
`feme-translate` needed this: `mlir::TranslateToMLIRRegistration`'s callback
is handed an `MLIRContext *` that `MlirTranslateMain` already configured
(dialect registry from `dialectRegistration`, `-mlir-print-op-generic`/
threading/etc. command-line flags) — constructing a private, disconnected
`MLIRContext` inside the callback (as I first considered, to avoid touching
`Context` at all) would silently ignore all of that tool-level
configuration and produce a `spirv.module` op belonging to the wrong
context entirely. Added
`Context(mlir::MLIRContext &ExternalMLIRCtx)`, which wraps the caller's
`MLIRContext` (non-owning) but still owns its own fresh `LLVMContext` (SPIR-V
import never touches the LLVM side yet, so a private one is fine there).
Internally this meant switching `Context`'s `MLIRCtx` member from
`unique_ptr<MLIRContext>` to a raw `MLIRContext *` plus a separate
`OwnedMLIRCtx` that's null when wrapping. Covered by a new
`WrapsExternallyOwnedMLIRContext` gtest case.

## `feme-translate --import-spirv`: the round-trip test

`feme/lib/Import/SPIRV/TranslateRegistration.cpp` registers `import-spirv`
with MLIR's translation registry (same registry `mlir::registerAllTranslations()`
already populated with `deserialize-spirv`/`serialize-spirv`/etc.), wrapping
the tool's `MLIRContext` in a `feme::Context`, running `feme::SPIRVImporter`
through it, and using `takeMLIROperation()` to hand the result back. This
goes through FeMe's own `Importer`/`Module`/`Context` primitives end to
end — not just coincidentally reusing MLIR's generic `deserialize-spirv`
registration — which is the actual point of this roadmap step ("wrap MLIR's
existing spirv deserializer behind FeMe's Importer interface").

For the round-trip lit test (`spirv-import.mlir`), I followed "Avoiding
binary test fixtures" in Design.md: the test file's own `spirv` dialect text
is serialized to a real SPIR-V binary using `feme-translate`'s own,
generically-registered `--serialize-spirv` (no need for a separate
`mlir-translate` invocation, since `feme-translate` already registers every
MLIR translation), then that binary is piped through `--import-spirv` and
the resulting text is `FileCheck`ed — no binary blob is checked into the
repo. A second lit test (`spirv-import-invalid.test`) checks that malformed
input produces a diagnostic and a non-zero exit rather than a crash, per
"Diagnostics and Error Handling" in Design.md.

## Fuzzing harness

`feme-spirv-import-fuzzer` fuzzes `feme::SPIRVImporter::import` directly
(constructing a fresh `feme::Context` per input, mirroring how
`llvm-dis-fuzzer` uses a fresh `LLVMContext` per input — Importers must not
rely on state surviving across calls, per the "No Global State" principle).
I chose to follow `mlir/tools/mlir-parser-fuzzer`'s `DUMMY_MAIN` pattern
(`llvm::runFuzzerOnInputs`) rather than `llvm-dis-fuzzer`'s
libFuzzer-only-build pattern, specifically so the harness is buildable and
runnable as a plain CLI tool in this dev environment (no
`LLVM_USE_SANITIZE_COVERAGE`/`LLVM_LIB_FUZZING_ENGINE` configured) — this
let me actually exercise it rather than merely getting it to compile.

## Validation

- Enabled `ccache` (`CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER`)
  on the existing dev build directory, which already had
  `LLVM_ENABLE_ASSERTIONS=ON` and `LLVM_ENABLE_PROJECTS=feme` from the
  step-1 work; confirmed `ninja check-feme` passed (4/4) before making any
  changes, to establish a clean baseline.
- Built and ran every affected target after each incremental change, not
  just at the end: `FeMeCore`/`FeMeCoreTests`, `FeMeImportSPIRV`/
  `FeMeImportSPIRVTests`, `feme-translate` (manually round-tripping a
  hand-written `spirv.module` through `--serialize-spirv`/`--import-spirv`,
  and separately checking a malformed-input error path), `check-feme`
  (6/6 lit tests after adding the two new ones), and
  `feme-spirv-import-fuzzer` (run over a hand-built valid module, hand-built
  invalid inputs, and 200 randomly-generated byte strings — zero crashes).
- Ran `clang-format` (LLVM style) over every new/modified `.cpp`/`.h` file
  before each commit.
- Split the work into ten small, independently-buildable-and-testable
  commits (Module wrapper → Module move-op fix → Importer interface →
  SPIRVImporter+gtests → Module::takeMLIROperation → Context wrapping
  fix+ctor → feme-translate wiring+lit tests → fuzzer → Design.md updates),
  rebuilding and rerunning the relevant tests after each one, matching the
  granularity precedent set by roadmap step 1's commits.

## Deliberately deferred to later roadmap steps

- `feme::Diagnostics`/`Context::setDiagnosticHandler`/`diagnose()`: SPIR-V
  import errors currently surface as `llvm::Expected<Module>` failures (and,
  via `mlir::spirv::deserialize`'s own internal diagnostics, on stderr
  through MLIR's default handler) rather than through a FeMe-level
  `DiagnosticHandler`. Design.md's "feme::Context" section lists this as a
  `Context` responsibility, but nothing in the "SPIR-V import" roadmap item
  itself requires it, and step 1 already deferred it ("empty feme::Context").
  Left as a TODO rather than building speculative infrastructure with no
  current consumer.
- `feme::Context::getFormatRegistry()` / a registry of statically-linked
  Importers: `feme-translate`'s registration wires `SPIRVImporter` directly
  rather than through a registry, since there's exactly one `Importer` so
  far and no `Driver` yet to consult such a registry. This belongs with
  `feme::Driver` (still not implemented) in a later roadmap step.
- SPIR-V *export* (the `Exporter` direction) and retargeting to LLVM IR via
  `SPIRVToLLVM` — that's roadmap step 3 ("SPIR-V retargeting"), not this one.
