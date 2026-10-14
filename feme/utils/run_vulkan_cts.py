#!/usr/bin/env python3
# run_vulkan_cts.py - Crash-tolerant Vulkan-CTS batch/worker driver, with a
# mandatory solo re-verification pass for every case a parallel run reports
# as `Fail` or `DeviceLost` (roadmap L145/L420; see also D4/G2(b) in
# feme/docs/Roadmap.md), and a first-class `TimedOut` outcome for any case
# that still will not complete even when run alone.
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
#
# Roadmap L420: the 2026-10-12 full re-triage (`VulkanCTSReport.md`'s
# "Method" section) needed two more manual reconciliation passes this
# original design did not automate: every `DeviceLost` outcome had to be
# rerun alone by hand (442 of 466 turned out to be contention, not a real
# device loss), and the cases that still would not complete even alone had
# to be bumped to a much longer ad hoc timeout and re-run again, by hand, to
# tell a genuinely stuck case apart from one that just needed more time.
# Both are now automated: `DeviceLost` gets exactly the same solo
# re-verification `Fail` already got, and any case that is killed for
# exceeding `--final-timeout` while running completely alone (the finest
# recovery round, or the verification round) is recorded directly as a
# first-class `TimedOut` outcome instead of silently falling back to
# `Unrun` (which the final report cannot otherwise tell apart from a case
# that was never attempted at all).

"""Runs a Vulkan-CTS case list against `deqp-vk` in bounded, crash-tolerant
batches, then re-verifies every resulting `Fail`/`DeviceLost` case in
complete isolation before trusting it, recording any case that still will
not complete alone as a first-class `TimedOut` outcome.

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
   ceiling; because that round runs exactly one case per process, a
   process killed for exceeding it unambiguously timed out on that one
   case, which is recorded directly as a `TimedOut` outcome (roadmap L420)
   rather than retried further or left `Unrun`.
3. **Verification round** (roadmap L145/L420, the reason this script
   exists): every case whose merged result after rounds 1-2 is `Fail` or
   `DeviceLost` is re-run again, alone, under the same `--final-timeout`
   ceiling. A case whose solo result differs is reported as *flaky* and
   that solo result -- not the contended one -- is what is trusted in the
   final report. A case still `Fail`/`DeviceLost` when run alone is a
   verified, real outcome; a case killed for exceeding the timeout while
   being verified alone is likewise recorded as `TimedOut`, not reverted to
   the untrusted contended result.

Every round's case lists, QPA results, and process logs are retained under
`--output-dir` (`lists/`, `qpa/`, `logs/`) for later inspection, the same
directory layout the prior ad hoc harness used.

The final report is produced by `vk_cts_reconcile.py`'s own
`format_report`/`write_failures`, reusing its already-tested reconciliation
logic rather than a second, parallel implementation -- `--baseline-qpa` and
`--expected-failures` behave exactly as they do for that script; `TimedOut`
and verified `DeviceLost` outcomes need no special handling there, since
`format_report` already counts and reports whatever status each completed
case carries, generically. `--write-verified-failures` writes the
solo-verified `Fail` case list (the trustworthy G2(a)-style baseline this
script's own verification round makes possible), distinct from a plain
`--write-failures` of the *contended* result, which is exactly the number
roadmap L141-L144 showed cannot be trusted on its own.
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
    stopping is left for the caller to reconcile. Returns `True` if the
    process was killed for exceeding `timeout`, `False` otherwise (including
    when no `timeout` was given at all)."""
    args = [deqp_vk,
           "--deqp-caselist-file=" + case_list_path,
           "--deqp-shadercache=disable",
           "--deqp-log-filename=" + qpa_path] + list(extra_args)
    if os.name == "nt" and deqp_vk.lower().endswith(".py"):
        args.insert(0, sys.executable)
    with open(log_path, "w", encoding="utf-8") as log_file:
        try:
            subprocess.run(args, cwd=cwd, stdout=log_file,
                           stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            return True
        except OSError as error:
            log_file.write("\nrun_vulkan_cts: failed to start %s: %s\n"
                           % (deqp_vk, error))
    return False


def run_round(deqp_vk, batches, output_dir, tag, workers, extra_args, cwd,
              timeout=None):
    """Runs every case list in `batches` (each its own list of case names)
    as its own `deqp-vk` process, up to `workers` concurrently, tagging
    every artifact `tag-<index>`. Returns `(results, unrun, timed_out)`:
    `results` is the `{case_name: status}` map of every case across every
    batch that got a *completed* QPA record (`vk_cts_reconcile.read_qpa`'s
    own definition); `unrun` is every requested case that did not, in the
    same relative order it was requested; `timed_out` is the subset of
    `unrun` that is *known* to have timed out rather than merely not having
    run yet or having crashed for some other reason -- only a single-case
    batch (`len(batch) == 1`) unambiguously attributes a process timeout to
    the one case that was running when it was killed, so `timed_out` is
    always empty for any batch of more than one case."""
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
        batch_timed_out = [future.result() for future in futures]

    results = reconcile.merge_results(qpa_paths, "round %s" % tag)
    requested = [case_name for batch in batches for case_name in batch]
    unrun = [case_name for case_name in requested if case_name not in results]
    timed_out = [
        batch[0] for batch, did_time_out in zip(batches, batch_timed_out)
        if did_time_out and len(batch) == 1 and batch[0] not in results]
    return results, unrun, timed_out


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

    # Roadmap L420: a case that times out even when run completely alone,
    # at the finest configured granularity, is genuinely stuck -- not
    # merely unlucky to still be `Unrun` -- so it is recorded directly as
    # a first-class `TimedOut` outcome rather than bouncing through every
    # later recovery/verification round only to still end up `Unrun`
    # (which is indistinguishable, in the final report, from a case that
    # was never attempted at all).
    timed_out = {}

    merged, unrun, round_timed_out = run_round(
        args.deqp_vk, chunk(cases, args.batch_size), args.output_dir,
        "initial", args.workers, args.deqp_vk_arg, args.cwd)
    for case_name in round_timed_out:
        timed_out[case_name] = "TimedOut"

    for round_index, size in enumerate(recovery_sizes, start=1):
        if not unrun:
            break
        timeout = args.final_timeout if size == 1 else None
        round_results, unrun, round_timed_out = run_round(
            args.deqp_vk, chunk(unrun, size), args.output_dir,
            "recovery-%d" % round_index, args.workers, args.deqp_vk_arg,
            args.cwd, timeout=timeout)
        merged.update(round_results)
        for case_name in round_timed_out:
            timed_out[case_name] = "TimedOut"
        unrun = [case_name for case_name in unrun
                if case_name not in timed_out]

    # Roadmap L145/L420: every case reporting `Fail` or `DeviceLost` after
    # the batch/recovery rounds above is re-run once more, entirely alone,
    # before being trusted. See the module docstring for why -- roadmap
    # L141-L144 were exactly this failure mode for `Fail`; `DeviceLost` is
    # just as susceptible to shared-GPU/shared-host contention and had
    # previously only ever received this treatment by hand (see
    # `VulkanCTSReport.md`'s "Method" section on the 2026-10-12 full run).
    verify_cases = sorted(case for case, status in merged.items()
                          if status in ("Fail", "DeviceLost"))
    reconciled = {}
    if verify_cases:
        verify_results, verify_unrun, verify_timed_out = run_round(
            args.deqp_vk, chunk(verify_cases, 1), args.output_dir, "verify",
            args.workers, args.deqp_vk_arg, args.cwd,
            timeout=args.final_timeout)
        for case_name in verify_cases:
            original_status = merged[case_name]
            if case_name in verify_timed_out:
                timed_out[case_name] = "TimedOut"
                del merged[case_name]
                continue
            if case_name in verify_unrun:
                # Did not even complete alone, and did not time out either
                # (an outright crash) -- not a confirmed Fail/DeviceLost;
                # drop back to Unrun rather than silently keeping the
                # untrusted contended result.
                del merged[case_name]
                unrun.append(case_name)
                continue
            solo_status = verify_results[case_name]
            if solo_status != original_status:
                reconciled[case_name] = (original_status, solo_status)
                merged[case_name] = solo_status

    merged.update(timed_out)

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

    if reconciled:
        report += ("Flaky (Fail/DeviceLost under contention, different "
                   "result alone):\n")
        report += "".join(
            "  %s: %s -> %s\n" % (
                case_name, reconciled[case_name][0], reconciled[case_name][1])
            for case_name in sorted(reconciled))

    report_path = args.report or os.path.join(args.output_dir, "report.txt")
    with open(report_path, "w", encoding="utf-8") as file:
        file.write(report)

    if args.write_verified_failures:
        reconcile.write_failures(args.write_verified_failures, cases, merged)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
