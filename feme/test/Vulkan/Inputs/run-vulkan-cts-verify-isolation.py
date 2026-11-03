#!/usr/bin/env python3
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, sys.argv.pop())
import run_vulkan_cts


class VerificationIsolationTest(unittest.TestCase):
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
