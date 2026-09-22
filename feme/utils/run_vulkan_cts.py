#!/usr/bin/env python3
# run_vulkan_cts.py - Crash-tolerant Vulkan-CTS batch/worker driver, with a
# mandatory solo re-verification pass for every case a parallel run reports
# as `Fail` (roadmap L145; see also D4/G2(b) in feme/docs/Roadmap.md).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# A prior session's ad hoc, uncommitted full-run harness (see the "Full
# Vulkan CTS re-triage" and "L141-L144 investigated" sessions in
# agent_thoughts.md) split a 3.2M-case run into 1,800-case batches across
# six concurrent workers, recovered incomplete batches at progressively
# finer granularity (200, 20, then 1 case per process), and trusted every
# resulting `Fail` record as a real regression. That trust was misplaced:
# every one of roadmap L141-L144's several thousand "regressions" turned out
# not to reproduce at all when the exact same case was re-run alone, on an
# otherwise-idle host. This script folds that lesson in directly: after the
# ordinary batch/recovery passes below (which already handle a process that
# crashes or hangs mid-batch, `vk_cts_reconcile.py`'s own original job),
# every case whose merged result is `Fail` is re-run one more time, entirely
# alone, before being trusted. A case that flips to anything else on that
# solo re-run is reported separately as "flaky" (contention-only, not a real
# regression) rather than folded into the final failure list at all.

"""Runs a Vulkan-CTS case list against `deqp-vk` in bounded, crash-tolerant
batches, then re-verifies every resulting `Fail` case in complete isolation
before trusting it.

Usage:
  run_vulkan_cts.py --deqp-vk /path/to/deqp-vk --case-list cases.txt \
      --cwd /path/to/external/vulkancts/modules/vulkan \
      --output-dir /path/to/run/output \
      [--batch-size 1800] [--workers 6] \
      [--recovery-batch-sizes 200,20,1] [--final-timeout 60] \
      [--deqp-vk-arg=--deqp-log-images=disable ...] \
      [--baseline-qpa baseline.qpa ...] [--expected-failures failures.txt] \
      [--write-verified-failures verified-failures.txt]

`--cwd` matters: `deqp-vk` resolves its own data files (shaders, Amber
scripts, ...) relative to its own module directory, not the caller's
working directory (see feme/docs/VulkanCTSReport.md's "Method" section).

The run proceeds in rounds:

1. **Initial round**: `--case-list` split into `--batch-size`-case batches,
   up to `--workers` run concurrently. `vk_cts_reconcile.py`'s own
   `read_qpa` reconstructs each batch's *completed* per-case results (a
   process that dies mid-case leaves that case, and everything after it in
   that batch, with no completed record at all -- ordinary nonzero exit
   status from ordinary test failures is not evidence of this).
2. **Recovery rounds**: whatever cases still have no completed record are
   re-split at each of `--recovery-batch-sizes`' progressively finer sizes
   in turn (200, then 20, then 1 case per process, by default) and re-run,
   until either every case completes or the finest configured size is
   exhausted. The finest round uses `--final-timeout` as a hard per-process
   ceiling (a single case that still will not complete alone is genuinely
   stuck, not merely contending for a shared resource).
3. **Verification round** (roadmap L145, the reason this script exists):
   every case whose merged result after rounds 1-2 is `Fail` is re-run
   again, alone, under the same `--final-timeout` ceiling. A case whose
   solo result differs is reported as *flaky* and that solo result --
   not the contended one -- is what is trusted in the final report. A
   case still `Fail` when run alone is a verified, real failure.

Every round's case lists, QPA results, and process logs are retained under
`--output-dir` (`lists/`, `qpa/`, `logs/`) for later inspection, the same
directory layout the prior ad hoc harness used.

The final report is produced by `vk_cts_reconcile.py`'s own
`format_report`/`write_failures`, reusing its already-tested reconciliation
logic rather than a second, parallel implementation -- `--baseline-qpa` and
`--expected-failures` behave exactly as they do for that script.
`--write-verified-failures` writes the solo-verified `Fail` case list (the
trustworthy G2(a)-style baseline this script's own verification round makes
possible), distinct from a plain `--write-failures` of the *contended*
result, which is exactly the number roadmap L141-L144 showed cannot be
trusted on its own.
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vk_cts_reconcile as reconcile


def chunk(items, size):
    """Splits `items` into consecutive sublists of at most `size` items
    each, preserving order."""
    return [items[i:i + size] for i in range(0, len(items), size)]


def run_one_batch(deqp_vk, case_list_path, qpa_path, log_path, extra_args,
                  cwd, timeout):
    """Runs one `deqp-vk` process over one case-list file. The process's own
    exit status is deliberately ignored: `deqp-vk` exits nonzero for
    ordinary test failures too, so it carries no information a completed
    QPA record does not already carry more precisely. A process that
    exceeds `timeout` (only ever passed for the finest, single-case
    recovery/verification rounds -- see the module docstring) is killed;
    either way, whatever the process managed to write to `qpa_path` before
    stopping is left for the caller to reconcile."""
    args = [deqp_vk,
           "--deqp-caselist-file=" + case_list_path,
           "--deqp-shadercache=disable",
           "--deqp-log-filename=" + qpa_path] + list(extra_args)
    with open(log_path, "w", encoding="utf-8") as log_file:
        try:
            subprocess.run(args, cwd=cwd, stdout=log_file,
                           stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            pass
        except OSError as error:
            log_file.write("\nrun_vulkan_cts: failed to start %s: %s\n"
                           % (deqp_vk, error))


def run_round(deqp_vk, batches, output_dir, tag, workers, extra_args, cwd,
              timeout=None):
    """Runs every case list in `batches` (each its own list of case names)
    as its own `deqp-vk` process, up to `workers` concurrently, tagging
    every artifact `tag-<index>`. Returns `(results, unrun)`: `results` is
    the `{case_name: status}` map of every case across every batch that got
    a *completed* QPA record (`vk_cts_reconcile.read_qpa`'s own definition);
    `unrun` is every requested case that did not, in the same relative
    order it was requested."""
    lists_dir = os.path.join(output_dir, "lists")
    qpa_dir = os.path.join(output_dir, "qpa")
    logs_dir = os.path.join(output_dir, "logs")
    for directory in (lists_dir, qpa_dir, logs_dir):
        os.makedirs(directory, exist_ok=True)

    case_list_paths = []
    qpa_paths = []
    for index, batch in enumerate(batches):
        name = "%s-%05d" % (tag, index)
        case_list_path = os.path.join(lists_dir, name + ".txt")
        with open(case_list_path, "w", encoding="utf-8") as file:
            for case_name in batch:
                file.write(case_name + "\n")
        case_list_paths.append(case_list_path)
        qpa_paths.append(os.path.join(qpa_dir, name + ".qpa"))

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = []
        for index, (case_list_path, qpa_path) in enumerate(
                zip(case_list_paths, qpa_paths)):
            name = "%s-%05d" % (tag, index)
            log_path = os.path.join(logs_dir, name + ".log")
            futures.append(pool.submit(run_one_batch, deqp_vk,
                                       case_list_path, qpa_path, log_path,
                                       extra_args, cwd, timeout))
        for future in futures:
            future.result()

    results = reconcile.merge_results(qpa_paths, "round %s" % tag)
    requested = [case_name for batch in batches for case_name in batch]
    unrun = [case_name for case_name in requested if case_name not in results]
    return results, unrun


def parse_recovery_sizes(raw):
    sizes = [int(part) for part in raw.split(",") if part.strip()]
    if not sizes:
        raise ValueError("--recovery-batch-sizes must list at least one size")
    if any(size <= 0 for size in sizes):
        raise ValueError("--recovery-batch-sizes entries must be positive")
    return sizes


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--deqp-vk", required=True,
                        help="path to the deqp-vk executable")
    parser.add_argument("--case-list", required=True,
                        help="plain-text list of requested dEQP case names")
    parser.add_argument("--cwd", required=True,
                        help="working directory to run deqp-vk from (its "
                             "own module directory, for relative data "
                             "files)")
    parser.add_argument("--output-dir", required=True,
                        help="directory to write lists/qpa/logs and the "
                             "final report under")
    parser.add_argument("--batch-size", type=int, default=1800,
                        help="cases per initial-round process (default: "
                             "%(default)s)")
    parser.add_argument("--workers", type=int, default=6,
                        help="concurrent deqp-vk processes (default: "
                             "%(default)s)")
    parser.add_argument("--recovery-batch-sizes", default="200,20,1",
                        help="comma-separated, decreasing batch sizes to "
                             "retry incomplete cases at (default: "
                             "%(default)s)")
    parser.add_argument("--final-timeout", type=float, default=60.0,
                        help="per-process timeout in seconds, applied only "
                             "at the finest recovery size and the "
                             "verification round (default: %(default)s)")
    parser.add_argument("--deqp-vk-arg", action="append", default=[],
                        help="extra argument to pass through to every "
                             "deqp-vk invocation (may repeat)")
    parser.add_argument("--baseline-qpa", action="append", default=[],
                        help="QPA result file from a baseline run, "
                             "forwarded to vk_cts_reconcile's own report")
    parser.add_argument("--expected-failures",
                        help="case names whose Fail result is expected, "
                             "forwarded to vk_cts_reconcile's own report")
    parser.add_argument("--write-verified-failures",
                        help="write the solo-verified Fail case list here, "
                             "in case-list order")
    parser.add_argument("--report",
                        help="write the final reconciliation report here "
                             "(default: <output-dir>/report.txt)")
    args = parser.parse_args(argv)

    try:
        recovery_sizes = parse_recovery_sizes(args.recovery_batch_sizes)
    except ValueError as error:
        sys.stderr.write("run_vulkan_cts: %s\n" % error)
        return 1

    cases = reconcile.read_case_list(args.case_list)
    if len(cases) != len(set(cases)):
        sys.stderr.write("run_vulkan_cts: --case-list contains duplicate "
                         "case names\n")
        return 1

    os.makedirs(args.output_dir, exist_ok=True)

    merged, unrun = run_round(args.deqp_vk, chunk(cases, args.batch_size),
                              args.output_dir, "initial", args.workers,
                              args.deqp_vk_arg, args.cwd)

    for round_index, size in enumerate(recovery_sizes, start=1):
        if not unrun:
            break
        timeout = args.final_timeout if size == 1 else None
        round_results, unrun = run_round(
            args.deqp_vk, chunk(unrun, size), args.output_dir,
            "recovery-%d" % round_index, args.workers, args.deqp_vk_arg,
            args.cwd, timeout=timeout)
        merged.update(round_results)

    # Roadmap L145: every case reporting `Fail` after the batch/recovery
    # rounds above is re-run once more, entirely alone, before being
    # trusted. See the module docstring for why -- roadmap L141-L144 were
    # exactly this failure mode.
    fail_cases = sorted(case for case, status in merged.items()
                        if status == "Fail")
    flaky = {}
    if fail_cases:
        verify_results, verify_unrun = run_round(
            args.deqp_vk, chunk(fail_cases, 1), args.output_dir, "verify",
            args.workers, args.deqp_vk_arg, args.cwd,
            timeout=args.final_timeout)
        for case_name in fail_cases:
            if case_name in verify_unrun:
                # Did not even complete alone -- not a confirmed Fail
                # either; drop back to Unrun rather than silently keeping
                # the untrusted contended result.
                del merged[case_name]
                unrun.append(case_name)
                continue
            solo_status = verify_results[case_name]
            if solo_status != "Fail":
                flaky[case_name] = solo_status
                merged[case_name] = solo_status

    baseline = reconcile.merge_results(args.baseline_qpa, "baseline QPA input")
    expected_failures = set(
        reconcile.read_case_list(args.expected_failures)
        if args.expected_failures else [])
    try:
        report = reconcile.format_report(cases, merged, baseline,
                                         expected_failures)
    except ValueError as error:
        sys.stderr.write("run_vulkan_cts: %s\n" % error)
        return 1

    if flaky:
        report += "Flaky (Fail under contention, different result alone):\n"
        report += "".join(
            "  %s: Fail -> %s\n" % (case_name, flaky[case_name])
            for case_name in sorted(flaky))

    report_path = args.report or os.path.join(args.output_dir, "report.txt")
    with open(report_path, "w", encoding="utf-8") as file:
        file.write(report)

    if args.write_verified_failures:
        reconcile.write_failures(args.write_verified_failures, cases, merged)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
