#!/usr/bin/env python3
# feme-run-differential.py - The CFG restructurization differential harness.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Roadmap step R1 (see feme/docs/Roadmap.md's "Suggested sequencing" table,
# and prerequisite 4 in its §2.4): this is the "%feme-run-differential"
# helper feme/test/Tools/feme-run/differential-harness.test used to spell
# out, as five copy-pasted five-line `RUN:` blocks, one per seed. That
# harness diffs a `feme-cfg-gen`-generated shader's normal (widened)
# `feme-run` output against `--reference`'s ground truth -- see the "CFG
# restructurization test suite" section of feme/docs/FeMeCPUDesign.md for
# why a mismatch here means Phase 4 (widening) computed something different
# from what the shader's own control flow says it should have. This script
# takes a seed list, a `feme-cfg-gen` flag set, and a wave-size list (the
# §2.4.1 wave-size sweep this milestone also adds), and diffs every
# (seed, wave size) pair against that seed's one `--reference` run, so a
# test opts into a larger shape/wave-size space by editing a `RUN:` line's
# arguments instead of growing the file.
#
# Usage:
#   feme-run-differential.py --feme-cfg-gen=<path> --feme-run=<path>
#       --work-dir=<dir> --heap=<heap.yaml> --seeds=1,2,3
#       --wave-sizes=4,8,16,32 [--groups=X,Y,Z] [--max-depth=N]
#       [--max-constructs=N] [--divergent|--no-divergent]
#       [--loops|--no-loops] [--unstructured|--no-unstructured]

import argparse
import subprocess
import sys


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--feme-cfg-gen", required=True,
                        help="path to the feme-cfg-gen executable")
    parser.add_argument("--feme-run", required=True,
                        help="path to the feme-run executable")
    parser.add_argument("--work-dir", required=True,
                        help="scratch directory for generated shaders")
    parser.add_argument("--heap", required=True,
                        help="the resource-heap YAML file (see feme-run)")
    parser.add_argument("--seeds", required=True,
                        help="comma-separated feme-cfg-gen --seed values")
    parser.add_argument("--wave-sizes", default="4",
                        help="comma-separated feme-run --wave-size values "
                             "(default: 4)")
    parser.add_argument("--groups", default="1,1,1",
                        help="the dispatch's group count, 'X,Y,Z' "
                             "(default: 1,1,1)")
    parser.add_argument("--max-depth", type=int, default=3)
    parser.add_argument("--max-constructs", type=int, default=12)
    parser.add_argument("--divergent", dest="divergent",
                        action="store_true", default=True)
    parser.add_argument("--no-divergent", dest="divergent",
                        action="store_false")
    parser.add_argument("--loops", dest="loops", action="store_true",
                        default=True)
    parser.add_argument("--no-loops", dest="loops", action="store_false")
    parser.add_argument("--unstructured", dest="unstructured",
                        action="store_true", default=False)
    parser.add_argument("--no-unstructured", dest="unstructured",
                        action="store_false")
    return parser.parse_args(argv)


def run(cmd):
    """Runs `cmd`, returning (stdout, stderr) and raising on a non-zero
    exit so a caller only has to handle the "tool itself failed" case in
    one place."""
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        raise RuntimeError(
            "'%s' failed (exit %d):\n%s"
            % (" ".join(cmd), proc.returncode, proc.stderr.decode()))
    return proc.stdout, proc.stderr


def generate_shader(args, seed, out_path):
    cmd = [
        args.feme_cfg_gen,
        "--seed=%d" % seed,
        "--max-depth=%d" % args.max_depth,
        "--max-constructs=%d" % args.max_constructs,
        "--divergent=%s" % ("true" if args.divergent else "false"),
        "--loops=%s" % ("true" if args.loops else "false"),
        "--unstructured=%s" % ("true" if args.unstructured else "false"),
        "-o", out_path,
    ]
    run(cmd)


def dispatch(args, shader_path, wave_size=None, reference=False):
    cmd = [
        args.feme_run,
        "--groups=%s" % args.groups,
        "--heap=%s" % args.heap,
    ]
    if reference:
        cmd.append("--reference")
    else:
        cmd.append("--wave-size=%d" % wave_size)
    cmd.append(shader_path)
    stdout, stderr = run(cmd)
    # `feme-run` diagnoses an unsupported shape (e.g. an as-yet-unlinearized
    # divergent branch) on stderr without necessarily failing its own exit
    # code (see feme::cpu::LinearizePass's "diagnosed and left completely
    # untouched" shapes) -- treating any such diagnostic as a hard failure
    # keeps this harness from ever silently accepting a shape the pipeline
    # only coincidentally computed the right answer for.
    if stderr:
        raise RuntimeError(
            "'%s' printed diagnostics for a shape this harness expects to "
            "run cleanly:\n%s" % (" ".join(cmd), stderr.decode()))
    return stdout


def main(argv):
    args = parse_args(argv)
    seeds = [int(s) for s in args.seeds.split(",") if s]
    wave_sizes = [int(w) for w in args.wave_sizes.split(",") if w]

    failures = []
    for seed in seeds:
        shader_path = "%s/seed%d.ll" % (args.work_dir, seed)
        try:
            generate_shader(args, seed, shader_path)
            reference = dispatch(args, shader_path, reference=True)
        except RuntimeError as e:
            failures.append("seed %d: %s" % (seed, e))
            continue

        for wave_size in wave_sizes:
            try:
                normal = dispatch(args, shader_path, wave_size=wave_size)
            except RuntimeError as e:
                failures.append(
                    "seed %d, wave-size %d: %s" % (seed, wave_size, e))
                continue
            if normal != reference:
                failures.append(
                    "seed %d, wave-size %d: mismatch against --reference\n"
                    "  normal:    %s\n"
                    "  reference: %s"
                    % (seed, wave_size, normal.decode().strip(),
                       reference.decode().strip()))

    if failures:
        sys.stderr.write("feme-run-differential: %d failure(s):\n%s\n"
                         % (len(failures), "\n".join(failures)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
