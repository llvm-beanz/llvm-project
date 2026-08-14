#!/usr/bin/env python3
# check-feme-fuzz.py - Bounded, seed-corpus-only runs of every fuzz target.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Roadmap step R6 (see feme/docs/Roadmap.md's §1.7 P0 "the fuzzers do not
# run anywhere"): every FeMe fuzz target (feme-dxil-import-fuzzer,
# feme-dxbc-import-fuzzer, feme-spirv-import-fuzzer, dxbc-as-fuzzer,
# feme-cpu-restructure-fuzzer) already ships a checked-in seed corpus, but
# nothing runs any of them, so a fuzzer that stops compiling -- or
# regresses to crashing on one of its own seeds -- goes unnoticed. This
# script is the `check-feme-fuzz` CMake target's driver: for each
# (fuzzer, corpus directory) pair, it runs the fuzzer over every file in
# that corpus for a bounded number of iterations, so the crash-freedom
# claim in Design.md's "Testing Strategy" is checked for a few seconds of
# test time on every build, without running an actual, unbounded fuzzing
# campaign.
#
# `-runs=N` and any other libFuzzer-style flag are harmless no-ops on a
# `DummyMain`-based build (see FuzzerCLI.cpp's runFuzzerOnInputs, which
# skips any argument starting with `-`): either way, this script's real
# job -- running each fuzz target once per corpus file -- happens
# identically whether or not the build was configured with
# LLVM_USE_SANITIZE_COVERAGE.
#
# Usage:
#   check-feme-fuzz.py --runs=200 --max-total-time=20 \
#       --fuzzer=<binary1>=<corpus-dir1> \
#       --fuzzer=<binary2>=<corpus-dir2> ...

import argparse
import glob
import os
import subprocess
import sys


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=200,
                        help="libFuzzer -runs=N value, ignored by a "
                             "DummyMain build (default: 200)")
    parser.add_argument("--max-total-time", type=int, default=20,
                        help="libFuzzer -max_total_time=N value in "
                             "seconds, ignored by a DummyMain build "
                             "(default: 20)")
    parser.add_argument("--fuzzer", action="append", default=[],
                        dest="fuzzers", metavar="BINARY=CORPUS_DIR",
                        help="a fuzzer binary and its seed corpus "
                             "directory, may be repeated")
    return parser.parse_args(argv)


def corpus_files(corpus_dir):
    """Every regular file directly under `corpus_dir`, sorted for
    reproducible output. Passing individual files (rather than the
    directory itself) works for both a real libFuzzer binary and a
    DummyMain build, the latter of which cannot open a directory as an
    input file (see FuzzerCLI.cpp's runFuzzerOnInputs)."""
    return sorted(p for p in glob.glob(os.path.join(corpus_dir, "*"))
                  if os.path.isfile(p))


def run_fuzzer(binary, corpus_dir, runs, max_total_time):
    files = corpus_files(corpus_dir)
    if not files:
        return "%s: no seed corpus files found under %s" % (binary,
                                                             corpus_dir)

    cmd = [binary, "-runs=%d" % runs,
          "-max_total_time=%d" % max_total_time] + files
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return ("'%s' failed (exit %d):\n%s\n%s"
               % (" ".join(cmd), proc.returncode,
                  proc.stdout.decode(errors="replace"),
                  proc.stderr.decode(errors="replace")))
    return None


def main(argv):
    args = parse_args(argv)
    if not args.fuzzers:
        sys.stderr.write("check-feme-fuzz: no --fuzzer=BINARY=CORPUS_DIR "
                         "arguments given\n")
        return 1

    failures = []
    for spec in args.fuzzers:
        binary, sep, corpus_dir = spec.partition("=")
        if not sep:
            sys.stderr.write(
                "check-feme-fuzz: malformed --fuzzer spec %r, expected "
                "BINARY=CORPUS_DIR\n" % spec)
            return 1
        print("check-feme-fuzz: running %s over %s" % (binary, corpus_dir))
        failure = run_fuzzer(binary, corpus_dir, args.runs,
                             args.max_total_time)
        if failure:
            failures.append(failure)

    if failures:
        sys.stderr.write("check-feme-fuzz: %d failure(s):\n%s\n"
                         % (len(failures), "\n".join(failures)))
        return 1
    print("check-feme-fuzz: all %d fuzzer(s) completed cleanly"
         % len(args.fuzzers))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
