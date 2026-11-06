#!/usr/bin/env python3
# check-feme-capability-manifest.py - Detects silent drift in FeMe's two
# committed Vulkan capability manifests.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# WARP roadmap WS3(b) ("byte/image comparisons and unchanged capability
# manifests ... compare named failures, not only pass totals"): every
# prior WARP-roadmap session regenerated feme/docs/Vulkan14FeatureInventory
# .md and feme/docs/VulkanExtensionInventory.md by hand with
# vk_gen_feature_inventory.py/vk_gen_extension_inventory.py, and recorded
# "the generator reproduces this table exactly" as a prose sentence after
# eyeballing the diff. That is exactly the kind of silent, easy-to-miss
# regression a scripted gate should catch instead: this script re-runs both
# generators with the same inputs the committed docs were generated from,
# and diffs their output against the table each doc has checked in,
# reporting the actual added/removed/changed rows -- not just "something
# changed" -- so an unexpected capability drift (a feature/extension/limit
# silently gained or lost by a shared-core change) is named, not just
# counted.
#
# This only covers the "capability manifests" half of WS3(b)'s exit
# evidence. The "byte/image comparisons" half -- named per-case CTS result
# diffing -- is already covered by the existing vk_cts_reconcile.py's
# --baseline-qpa comparison; dEQP-VK itself performs the underlying
# byte/image comparisons.
#
# Usage:
#   check-feme-capability-manifest.py --vk-xml <path/to/vk.xml>
#       [--feme-source-dir <path>] [--python <path>]

import argparse
import difflib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Each committed doc's generated table begins at this exact header line and
# runs to end of file (confirmed by inspecting both docs: neither has any
# content after its table).
FEATURE_TABLE_HEADER = "| Category | Version | Name | Advertised | Note |"
EXTENSION_TABLE_HEADER = "| Extension | Status | Note |"


def extract_committed_table(doc_path, header):
    lines = doc_path.read_text(encoding="utf-8").splitlines()
    try:
        start = lines.index(header)
    except ValueError:
        raise SystemExit(
            "check-feme-capability-manifest: %s: could not find table "
            "header %r -- has the doc's layout changed?"
            % (doc_path, header))
    return "\n".join(lines[start:]) + "\n"


def run_generator(python, script, args, out_path):
    cmd = [python, str(script)] + args + ["-o", str(out_path)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if proc.returncode != 0:
        raise SystemExit(
            "check-feme-capability-manifest: '%s' failed (exit %d):\n%s"
            % (" ".join(cmd), proc.returncode,
               proc.stdout.decode(errors="replace")))
    return out_path.read_text(encoding="utf-8")


def diff_tables(label, committed, generated, doc_path):
    if committed == generated:
        print("check-feme-capability-manifest: %s: unchanged" % label,
             flush=True)
        return None
    diff = "".join(difflib.unified_diff(
        committed.splitlines(keepends=True),
        generated.splitlines(keepends=True),
        fromfile="%s (committed)" % doc_path,
        tofile="%s (regenerated)" % label))
    return (
        "check-feme-capability-manifest: %s: drifted from the committed "
        "table -- regenerate %s and review before committing:\n%s"
        % (label, doc_path, diff))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vk-xml", required=True,
                        help="path to the VK-GL-CTS checkout's vk.xml")
    parser.add_argument("--feme-source-dir", default="feme",
                        help="path to the feme/ subdirectory (default: "
                             "feme, i.e. run from the repository root)")
    parser.add_argument("--python", default=sys.executable,
                        help="interpreter to run the generator scripts "
                             "with (default: this script's own)")
    args = parser.parse_args(argv)

    feme_dir = Path(args.feme_source_dir)
    utils_dir = feme_dir / "utils"
    docs_dir = feme_dir / "docs"
    vulkan_lib_dir = feme_dir / "lib" / "Vulkan"

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)

        feature_doc = docs_dir / "Vulkan14FeatureInventory.md"
        feature_generated = run_generator(
            args.python, utils_dir / "vk_gen_feature_inventory.py",
            [args.vk_xml,
             "--version", "VK_VERSION_1_0:feature,extension",
             "--version", "VK_VERSION_1_1:feature,extension",
             "--version", "VK_VERSION_1_2:feature,extension",
             "--version", "VK_VERSION_1_3", "--version", "VK_VERSION_1_4",
             "--advertised-features",
             str(vulkan_lib_dir / "AdvertisedPromotedFeatures.txt"),
             "--advertised-extensions",
             str(vulkan_lib_dir / "AdvertisedPromotedExtensions.txt")],
            tmp_path / "feature_inventory.md")
        feature_committed = extract_committed_table(
            feature_doc, FEATURE_TABLE_HEADER)
        failure = diff_tables("Vulkan14FeatureInventory", feature_committed,
                              feature_generated, feature_doc)
        if failure:
            failures.append(failure)

        extension_doc = docs_dir / "VulkanExtensionInventory.md"
        extension_generated = run_generator(
            args.python, utils_dir / "vk_gen_extension_inventory.py",
            [args.vk_xml,
             "--advertised", str(vulkan_lib_dir / "AdvertisedExtensions.txt"),
             "--core-promoted",
             str(vulkan_lib_dir / "CorePromotedNotAdvertisedExtensions.txt"),
             "--planned", str(vulkan_lib_dir / "PlannedExtensions.txt")],
            tmp_path / "extension_inventory.md")
        extension_committed = extract_committed_table(
            extension_doc, EXTENSION_TABLE_HEADER)
        failure = diff_tables("VulkanExtensionInventory", extension_committed,
                              extension_generated, extension_doc)
        if failure:
            failures.append(failure)

    if failures:
        sys.stderr.write("\n".join(failures) + "\n")
        sys.stderr.flush()
        return 1
    print("check-feme-capability-manifest: both manifests match their "
         "generators")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
