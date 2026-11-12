#!/usr/bin/env python3
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, sys.argv.pop())
import run_vulkan_cts


class VerificationIsolationTest(unittest.TestCase):
    def test_timeout_before_qpa_creation_is_recovered(self):
        case = "dEQP-VK.x.hang"
        with tempfile.TemporaryDirectory() as directory:
            case_list = Path(directory) / "cases.txt"
            case_list.write_text(case, encoding="utf-8")
            with mock.patch.object(
                run_vulkan_cts.subprocess, "run",
                side_effect=subprocess.TimeoutExpired("deqp-vk", 10),
            ) as processes:
                self.assertEqual(run_vulkan_cts.main([
                    "--deqp-vk", "unused", "--case-list", str(case_list),
                    "--cwd", directory, "--output-dir", directory,
                    "--batch-size", "1", "--recovery-batch-sizes", "1",
                    "--batch-timeout", "10", "--final-timeout", "10",
                ]), 0)
            self.assertEqual(processes.call_count, 2)
            report = (Path(directory) / "report.txt").read_text(encoding="utf-8")
            self.assertIn("Unrun cases: 0\n", report)
            self.assertIn("  TimedOut: 1\n", report)
            log = Path(directory) / "logs/recovery-1-00000.log"
            self.assertIn("timed out", log.read_text(encoding="utf-8"))

    def test_timeout_preserves_completed_qpa_records(self):
        cases = ["dEQP-VK.x.pass", "dEQP-VK.x.hang"]

        def partial_result(args, **kwargs):
            qpa = next(arg.split("=", 1)[1] for arg in args
                       if arg.startswith("--deqp-log-filename="))
            Path(qpa).write_text(
                "#beginTestCaseResult " + cases[0] + "\n"
                '<TestCaseResult><Result StatusCode="Pass"/></TestCaseResult>\n'
                "#endTestCaseResult\n", encoding="utf-8")
            raise subprocess.TimeoutExpired(args, kwargs["timeout"])

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(
                run_vulkan_cts.subprocess, "run", side_effect=partial_result,
            ):
                results, unrun, timed_out = run_vulkan_cts.run_round(
                    "unused", [cases], directory, "initial", 1, [], directory,
                    timeout=10)
            self.assertEqual(results, {cases[0]: "Pass"})
            self.assertEqual(unrun, [cases[1]])
            self.assertEqual(timed_out, [])

    def test_missing_qpa_without_timeout_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(run_vulkan_cts.subprocess, "run"):
                with self.assertRaises(FileNotFoundError):
                    run_vulkan_cts.run_round(
                        "unused", [["dEQP-VK.x.pass"]], directory, "initial",
                        1, [], directory, timeout=10)

    def test_initial_single_case_timeout_is_retried_with_final_budget(self):
        case = "dEQP-VK.x.slow"
        with tempfile.TemporaryDirectory() as directory:
            case_list = Path(directory) / "cases.txt"
            case_list.write_text(case, encoding="utf-8")
            with mock.patch.object(
                run_vulkan_cts, "run_round",
                side_effect=[({}, [case], [case]), ({case: "Pass"}, [], [])],
            ) as rounds:
                self.assertEqual(run_vulkan_cts.main([
                    "--deqp-vk", "unused", "--case-list", str(case_list),
                    "--cwd", directory, "--output-dir", directory,
                    "--batch-size", "1", "--recovery-batch-sizes", "1",
                    "--batch-timeout", "1", "--final-timeout", "600",
                ]), 0)
            self.assertEqual(
                [call.kwargs["timeout"] for call in rounds.call_args_list],
                [1, 600],
            )
            report = (Path(directory) / "report.txt").read_text(encoding="utf-8")
            self.assertIn("  Pass: 1\n", report)
            self.assertNotIn("TimedOut", report)

    def test_batch_timeouts_preserve_single_case_recovery(self):
        cases = ["dEQP-VK.x.hang", "dEQP-VK.x.pass"]
        with tempfile.TemporaryDirectory() as directory:
            case_list = Path(directory) / "cases.txt"
            case_list.write_text("\n".join(cases), encoding="utf-8")
            with mock.patch.object(
                run_vulkan_cts, "run_round",
                side_effect=[
                    ({}, cases, []),
                    ({}, cases, []),
                    ({cases[1]: "Pass"}, [cases[0]], [cases[0]]),
                ],
            ) as rounds:
                self.assertEqual(run_vulkan_cts.main([
                    "--deqp-vk", "unused", "--case-list", str(case_list),
                    "--cwd", directory, "--output-dir", directory,
                    "--batch-size", "2", "--recovery-batch-sizes", "2,1",
                    "--batch-timeout", "7200", "--final-timeout", "600",
                ]), 0)
            self.assertEqual(rounds.call_count, 3)
            self.assertEqual(
                [call.kwargs["timeout"] for call in rounds.call_args_list],
                [7200, 7200, 600],
            )
            report = (Path(directory) / "report.txt").read_text(encoding="utf-8")
            self.assertIn("Unrun cases: 0\n", report)
            self.assertIn("  Pass: 1\n", report)
            self.assertIn("  TimedOut: 1\n", report)

    def test_parallel_failures_are_verified_sequentially(self):
        initial = {"dEQP-VK.x.fail": "Fail", "dEQP-VK.x.lost": "DeviceLost"}
        verified = {case: "Pass" for case in initial}
        with tempfile.TemporaryDirectory() as directory:
            case_list = Path(directory) / "cases.txt"
            case_list.write_text("\n".join(initial), encoding="utf-8")
            with mock.patch.object(
                run_vulkan_cts, "run_round",
                side_effect=[(initial, [], []), (verified, [], [])],
            ) as rounds:
                self.assertEqual(run_vulkan_cts.main([
                    "--deqp-vk", "unused", "--case-list", str(case_list),
                    "--cwd", directory, "--output-dir", directory,
                    "--workers", "6",
                ]), 0)
            self.assertEqual(rounds.call_count, 2)
            self.assertEqual(rounds.call_args_list[0].args[4], 6)
            self.assertIsNone(rounds.call_args_list[0].kwargs["timeout"])
            verification = rounds.call_args_list[1].args
            self.assertEqual(verification[3:5], ("verify", 1))
            self.assertEqual(
                verification[1], [[case] for case in sorted(initial)])
            report = (Path(directory) / "report.txt").read_text(encoding="utf-8")
            self.assertIn("  Pass: 2\n", report)
            self.assertNotIn("  Fail:", report)
            self.assertNotIn("  DeviceLost:", report)


if __name__ == "__main__":
    unittest.main()
