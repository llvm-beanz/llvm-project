#!/usr/bin/env python3
"""Generate a CMake command from a JSON settings file.

Reads `scripts/Settings.json`, prompts the user to choose one value
from each entry's "Values" list, and prints a `cmake` command with
-D options for the selected values.
"""
import json
import os
import shlex
import subprocess
import shutil
import re
import sys
import argparse


def prompt_choice(name, info):
    values = info.get("Values") or []
    option = info.get("Option")
    default = info.get("Default")
    desc = info.get("Description", "")
    multiple = bool(info.get("MultipleValues", False))

    print(f"\n{name}: {desc}")
    for i, v in enumerate(values, start=1):
        mark = " (default)" if v == default else ""
        print(f"  {i}) {v}{mark}")

    if multiple:
        print("Enter one or more values by number or name, separated by commas. Ranges like 1-3 are supported.")

    while True:
        resp = input(f"Choose value{'s' if multiple else ''} for {name} [default: {default}]: ").strip()
        if resp == "":
            # default may be a single value; for multiple return that single default
            choice = default
            break

        if multiple:
            picks = []
            tokens = [t.strip() for t in resp.split(",") if t.strip()]
            valid = True
            for tok in tokens:
                if tok.isdigit():
                    idx = int(tok)
                    if 1 <= idx <= len(values):
                        picks.append(values[idx - 1])
                    else:
                        valid = False
                        break
                elif "-" in tok:
                    # range
                    try:
                        a, b = tok.split("-", 1)
                        ai = int(a); bi = int(b)
                        if ai < 1 or bi > len(values) or ai > bi:
                            valid = False
                            break
                        for ii in range(ai, bi + 1):
                            picks.append(values[ii - 1])
                    except Exception:
                        valid = False
                        break
                elif tok in values:
                    picks.append(tok)
                else:
                    valid = False
                    break
            if not valid or not picks:
                print("Invalid selection — enter numbers, ranges, or names from the list.")
                continue
            # remove duplicates while preserving order
            seen = set(); uniq = []
            for p in picks:
                if p not in seen:
                    seen.add(p); uniq.append(p)
            # Join with semicolon for CMake lists
            choice = ";".join(uniq)
            break

        # single selection mode
        if resp.isdigit():
            idx = int(resp)
            if 1 <= idx <= len(values):
                choice = values[idx - 1]
                break
        if resp in values:
            choice = resp
            break
        print("Invalid choice — enter a number or one of the listed values.")

    return option, choice


def detect_cmake_generators():
    """Return a list of available CMake generator names detected from `cmake --help`.

    Falls back to a small heuristic using common generator tools when `cmake`
    is not available or parsing fails.
    """
    gens = []
    try:
        out = subprocess.check_output(["cmake", "--help"], text=True, stderr=subprocess.DEVNULL)
        # find the 'Generators' section
        lines = out.splitlines()
        start = None
        for i, l in enumerate(lines):
            if l.strip().lower().startswith("generators"):
                start = i + 1
                break
        if start is not None:
            # collect subsequent indented lines
            for l in lines[start:]:
                if not l.strip():
                    break
                if l.startswith("  ") or l.startswith("\t"):
                    # typical line: "  Ninja = Generates buildfiles for the Ninja build system"
                    m = re.match(r"\s{2}([^=\-]+?)\s*(=|-)", l)
                    if m:
                        gens.append(m.group(1).strip())
                    else:
                        # fallback: take the trimmed line up to first comma
                        gens.append(l.strip().split(",")[0])
                else:
                    break
    except Exception:
        pass

    # Fallback heuristics based on available tools
    if not gens:
        mapping = [
            ("Ninja", "ninja"),
            ("Unix Makefiles", "make"),
            ("NMake Makefiles", "nmake"),
            ("Xcode", "xcodebuild"),
        ]
        for gen_name, tool in mapping:
            if shutil.which(tool):
                gens.append(gen_name)

    # Deduplicate while preserving order
    seen = set()
    out_gens = []
    for g in gens:
        if g and g not in seen:
            seen.add(g)
            out_gens.append(g)
    return out_gens


def prompt_generator_choice(generators):
    if not generators:
        return None
    # prefer Ninja if present
    default = None
    for g in generators:
        if g.lower().startswith("ninja"):
            default = g
            break
    if default is None:
        default = generators[0]

    print("\nAvailable CMake generators:")
    for i, g in enumerate(generators, start=1):
        mark = " (default)" if g == default else ""
        print(f"  {i}) {g}{mark}")

    while True:
        resp = input(f"Choose generator [default: {default}]: ").strip()
        if resp == "":
            chosen = default
            break
        if resp.isdigit():
            idx = int(resp)
            if 1 <= idx <= len(generators):
                chosen = generators[idx - 1]
                break
        if resp in generators:
            chosen = resp
            break
        print("Invalid choice — enter a number or one of the listed generators.")

    # If the Visual Studio generator was chosen, prompt for architecture
    arch = None
    if chosen and "visual studio" in chosen.lower():
        arch_options = ["x86", "x64", "ARM", "ARM64", "Win32"]
        arch_default = "x64"
        print("\nVisual Studio generator selected — choose architecture:")
        for i, a in enumerate(arch_options, start=1):
            mark = " (default)" if a == arch_default else ""
            print(f"  {i}) {a}{mark}")
        while True:
            aresp = input(f"Choose architecture [default: {arch_default}]: ").strip()
            if aresp == "":
                arch = arch_default
                break
            if aresp.isdigit():
                ai = int(aresp)
                if 1 <= ai <= len(arch_options):
                    arch = arch_options[ai - 1]
                    break
            if aresp in arch_options:
                arch = aresp
                break
            print("Invalid choice — enter a number or one of the listed architectures.")

    return chosen, arch


def detect_linkers():
    """Detect available linkers on PATH and the default `ld` target.

    Resolves symlinks for all candidates and deduplicates by real binary.
    Returns a tuple (available_list, default_ld_name).
    """
    candidates = ["ld.bfd", "ld.lld", "ld.mold", "ld-classic", "lld", "gold"]
    found = []
    real_paths = {}
    for c in candidates:
        path = shutil.which(c)
        if path:
            try:
                real = os.path.realpath(path)
            except Exception:
                real = path
            # Deduplicate by real path
            if real not in real_paths.values():
                found.append(c)
            real_paths[c] = real

    # Detect default `ld` symlink target if possible
    default_ld = None
    ld_path = shutil.which("ld")
    # On Linux, prefer ld.lld if available
    is_linux = sys.platform.startswith("linux")
    if is_linux and "ld.lld" in found:
        default_ld = "ld.lld"
    elif ld_path:
        try:
            real = os.path.realpath(ld_path)
        except Exception:
            real = ld_path
        # Try to match real path to a candidate
        for c, c_real in real_paths.items():
            if real == c_real:
                default_ld = c
                break
        else:
            # If not matching a candidate, just use the basename
            default_ld = os.path.basename(real)

    return found, default_ld


def prompt_linker_choice(found, default_ld):
    """Prompt user to choose linker for LLVM_USE_LINKER from detected list.
    If only one linker is found, do not prompt and do not set any CMake options.
    """
    options = list(found)
    if default_ld and default_ld not in options:
        options.insert(0, default_ld)

    if not options:
        print("\nNo specific linkers detected on PATH; skipping LLVM_USE_LINKER prompt.")
        return None

    # If only one linker, do not prompt and do not set any CMake options
    if len(options) == 1:
        print(f"\nOnly one linker detected ({options[0]}), not setting LLVM_USE_LINKER.")
        return None

    print("\nDetected linkers:")
    is_linux = sys.platform.startswith("linux")
    for i, o in enumerate(options, start=1):
        mark = " (default)" if o == default_ld else ""
        warn = " (not recommended)" if is_linux and o == "ld.bfd" else ""
        print(f"  {i}) {o}{mark}{warn}")
    print(f"  {len(options)+1}) (none) -- do not set LLVM_USE_LINKER")

    while True:
        resp = input(f"Choose linker to set LLVM_USE_LINKER [default: {default_ld or 'none'}]: ").strip()
        if resp == "":
            return default_ld
        if resp.isdigit():
            idx = int(resp)
            if 1 <= idx <= len(options):
                return options[idx - 1]
            if idx == len(options) + 1:
                return None
        if resp in options:
            return resp
        if resp.lower() in ("none", "no", "n"):
            return None
        print("Invalid choice — enter a number, a detected linker name, or 'none'.")


def read_settings(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)



def ask_with_default(prompt, default):
    resp = input(f"{prompt} [default: {default}]: ").strip()
    return resp if resp else default

def detect_package_manager():
    """Detect a single package manager and return (name, path) or (None, None).

    Checks in preferred order: brew (`brew`), apt/apt-get, then pkg.
    Returns a 2-tuple `(name, path)` where `name` is one of "brew", "apt",
    or "pkg", and `path` is the discovered executable path. Returns `(None,
    None)` if none were found.
    """
    checks = [
        ("brew", ("brew",)),
        ("apt", ("apt", "apt-get")),
        ("pkg", ("pkg",)),
    ]
    for name, cmds in checks:
        for cmd in cmds:
            path = shutil.which(cmd)
            if path:
                return name, path
    return None, None


# Guided experience entry point (placeholder)
def configure_guided_env():
    deps_path = os.path.join(os.path.dirname(__file__), "Dependencies.json")
    if not os.path.exists(deps_path):
        print("\nNo dependency manifest found (Dependencies.json).")
        return

    try:
        with open(deps_path, "r", encoding="utf-8") as f:
            deps = json.load(f)
    except Exception as e:
        print(f"\nFailed to read Dependencies.json: {e}")
        return

    name, path = detect_package_manager()
    if name:
        print(f"\nDetected package manager: {name} ({path})")
    else:
        print("\nNo package manager found")

    required = []
    recommended = []
    install_cmd = "Manual installation required"
    if name:
        if name == "brew":
            install_cmd = f"{name} install"
        else:
            install_cmd = f"sudo {name} install"

    for dep_name, meta in deps.items():
        exe = meta.get("file")
        installed = shutil.which(exe) is not None if exe else False
        if installed:
            print(f"{dep_name}: installed ({exe})")
            continue

        pkgname = None
        if name:
            pkgname = meta.get(name)

        entry = {"name": dep_name, "pkg": pkgname, "url": meta.get("url"), "message": meta.get("message")}
        if bool(meta.get("required", False)):
            required.append(entry)
        else:
            recommended.append(entry)

    def process_group(title, items):
        print(f"\n{title}")
        if not items:
            print("  (none)")
            return

        if name:
            pkgnames = [i["pkg"] for i in items if i.get("pkg")]
            if pkgnames:
                print(f"  Install with: {install_cmd} {' '.join(pkgnames)}")
                do_install = input("Install these packages now? [y/N]: ").strip().lower()
                if do_install in ("y", "yes"):
                    if name == "Homebrew":
                        cmd = ["brew", "install"] + pkgnames
                    elif name == "apt":
                        cmd = ["sudo", "apt", "install", "-y"] + pkgnames
                    elif name == "pkg":
                        cmd = ["sudo", "pkg", "install", "-y"] + pkgnames
                    else:
                        # Generic fallback
                        cmd = [name, "install"] + pkgnames
                    print(f"Running: {' '.join(cmd)}")
                    try:
                        rc = subprocess.call(cmd)
                        if rc == 0:
                            print("Installation completed successfully.")
                        else:
                            print(f"Installation finished with exit code {rc}.")
                    except FileNotFoundError:
                        print("Package manager command not found; cannot install.")
                    except Exception as e:
                        print(f"Installation failed: {e}")

            for i in items:
                if not i.get("pkg"):
                    print(f"  - {i['name']}: {i.get('url') or 'no package name available'}")
                else:
                    print(f"  - {i['name']}: {i['pkg']}{' - ' + i['message'] if i.get('message') else ''}")
        else:
            for i in items:
                print(f"  - {i['name']}: {i.get('url') or i.get('pkg') or 'manual installation required'}")

    process_group("Required packages to install:", required)
    process_group("Recommended packages to install:", recommended)


def main():
    here = os.path.dirname(__file__)
    default_path = os.path.join(here, "Settings.json")

    resp = input("Do you want a guided experience configuring the recommended development environment? [y/N]: ").strip().lower()
    if resp in ("y", "yes"):
        configure_guided_env()
    parser = argparse.ArgumentParser(description="Generate a cmake command from a JSON settings file")
    parser.add_argument("-s", "--settings", help="Path to settings JSON file", default=None)
    args = parser.parse_args()

    # Use provided settings path when it exists; otherwise only prompt if the
    # chosen default (or provided) file doesn't exist.
    if args.settings:
        if os.path.exists(args.settings):
            settings_path = args.settings
        else:
            settings_path = ask_with_default("Path to settings JSON", args.settings)
    else:
        if os.path.exists(default_path):
            settings_path = default_path
        else:
            settings_path = ask_with_default("Path to settings JSON", default_path)

    if not os.path.exists(settings_path):
        print(f"Settings file not found: {settings_path}")
        return

    data = read_settings(settings_path)
    defines = []


    build_type = None
    for key, info in data.items():
        if not isinstance(info, dict):
            continue
        if "Values" not in info or "Option" not in info:
            continue
        option, choice = prompt_choice(key, info)
        defines.append(f"-D{option}={choice}")
        # Track build type for later warning
        if option == "CMAKE_BUILD_TYPE":
            build_type = choice

    src_dir = ask_with_default("Source directory for CMake (-S)", ".")
    build_dir = ask_with_default("Build directory for CMake (-B)", "build")

    # Detect and prompt for generator
    generators = detect_cmake_generators()
    gen_choice, gen_arch = prompt_generator_choice(generators) if generators else (None, None)

    # Detect linkers and prompt for LLVM_USE_LINKER
    found_linkers, default_ld = detect_linkers()

    # Prompt for linker

    linker_choice = prompt_linker_choice(found_linkers, default_ld)
    # If the user chose the system default linker (or pressed enter for default),
    # we should not set LLVM_USE_LINKER explicitly.
    if linker_choice and default_ld and linker_choice == default_ld:
        linker_choice = None

    # If ld.bfd is selected or is the only linker, and build type is not Release, warn and require explicit confirmation
    only_one_linker = len(found_linkers) <= 1
    if linker_choice == "ld.bfd" and build_type and build_type.lower() != "release":
        print("\nWARNING: Building LLVM with debug information using the GNU linker (ld.bfd) is not recommended.\n"
              "You may encounter build failures due to the linker running out of system memory in this configuration.\n")
        resp = input("Do you want to continue anyway? [y/N]: ").strip().lower()
        if resp not in ("y", "yes"):
            print("Aborting at user request.")
            sys.exit(1)
    # If there is only one linker don't set the linker option.
    if only_one_linker:
      linker_choice = None

    # Quote each argument safely
    parts = ["cmake", "-S", shlex.quote(src_dir), "-B", shlex.quote(build_dir)]
    if gen_choice:
        parts += ["-G", shlex.quote(gen_choice)]
        if gen_arch:
            parts += ["-A", shlex.quote(gen_arch)]
    # Only set LLVM_USE_LINKER if the user explicitly selected a linker.
    if linker_choice:
        # map common names to cmake LLVM_USE_LINKER expected values
        # keep the value as-is; CMake expects names like "lld" or full linker name
        parts += [shlex.quote(f"-DLLVM_USE_LINKER={linker_choice}")]
    parts += [shlex.quote(d) for d in defines]

    cmd = " ".join(parts)
    print("\nGenerated CMake command:")
    print(cmd)


if __name__ == "__main__":
    main()
