#!/usr/bin/env python3
"""Filters a Vulkan-CTS (`deqp-vk`) case list down to the V0-V4 object-model
subset FeMe's Vulkan ICD advertised support for at the time this script was
written (see "V4: ... first CTS runs over the advertised subset" in
feme/docs/FeMeVulkanDesign.md).

This is *not* a description of FeMe's overall planned scope, which is full
Vulkan 1.4 conformance including graphics and ray tracing (see
FeMeVulkanDesign.md's "Conformance Target"); it is a fixed, historical
snapshot of V0-V4's object-model scope (instance/device/queue,
memory/buffers/(V4) buffer views, descriptor sets, command buffers,
synchronization, pipeline caches), predating V5's images/samplers and V6's
graphics pipeline. Running the CTS's *entire* mustpass list against that
early revision would have reported an overwhelming majority of failures for
functionality not implemented at all yet -- of no use for regression
detection -- rather than the "intentionally advertised subset" the design
called for at the time. This script keeps exactly the dEQP-VK test groups
relevant to that early subset and drops everything else (graphics, sampling,
ray tracing, sparse binding, ...), so a CTS run's pass/fail signal was
actually meaningful against what FeMe claimed to support back then.

The full-scope, up-to-date measurement is the genuine 54-group `deqp-vk` run
described in feme/docs/VulkanCTSReport.md, which this script's narrower,
in-tree/`lit`-gated counterpart (`test/Vulkan/cts-compute-subset.test`) does
not attempt to replace.

Usage:
    filter_vulkan_cts_cases.py <full-case-list.txt> [-o <filtered.txt>]

The input is a plain-text case list in `deqp-vk`'s own format (one
`dEQP-VK.<group>.<...>.<case>` per line, as `deqp-vk --deqp-runmode=xml
--deqp-caselist-export` or a checked-in mustpass `.txt` file produces).
"""

import argparse
import sys

# Prefixes naming the dEQP-VK top-level groups this early, V0-V4 subset
# covers. A case is kept if its name starts with any of these.
# Ordered and commented to match the object-model rows this covers.
ADVERTISED_GROUP_PREFIXES = (
    # V0-V1: instance/device/queue/memory/buffer creation and queries.
    "dEQP-VK.api.info.",
    "dEQP-VK.api.device_init.",
    "dEQP-VK.api.object_management.",
    "dEQP-VK.memory.",
    "dEQP-VK.api.buffer.",
    # V4: buffer views (texel buffers).
    "dEQP-VK.api.buffer_view.",
    # V1-V3: descriptor sets, pipeline layouts.
    "dEQP-VK.binding_model.",
    # V1: compute dispatch itself.
    "dEQP-VK.compute.basic.",
    "dEQP-VK.compute.indirect_dispatch.",
    "dEQP-VK.compute.pipeline.",
    # V3: synchronization primitives (fences, semaphores, events, queries).
    "dEQP-VK.synchronization.",
    "dEQP-VK.query_pool.",
    # V4: robustness (bounds checking) and the persistent pipeline cache.
    "dEQP-VK.robustness.",
    "dEQP-VK.api.pipeline_cache.",
    # SPIR-V assembly compute-shader coverage: the same shader-compilation
    # path a real HLSL/GLSL compute shader takes.
    "dEQP-VK.spirv_assembly.instruction.compute.",
)

# Groups within an otherwise-kept prefix that still need excluding because
# they exercise something FeMe does not implement (e.g. sparse buffer
# binding, which "Initial Non-Goals" rules out alongside graphics).
EXCLUDED_SUBSTRINGS = (
    "sparse",
    "external_memory",
    "protected",
)


def is_advertised(case_name: str) -> bool:
    if not any(case_name.startswith(p) for p in ADVERTISED_GROUP_PREFIXES):
        return False
    return not any(s in case_name for s in EXCLUDED_SUBSTRINGS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case_list", help="Full dEQP-VK case list (one case per line)")
    parser.add_argument(
        "-o",
        "--output",
        default="-",
        help="Where to write the filtered list ('-' for stdout, the default)",
    )
    args = parser.parse_args()

    with open(args.case_list, encoding="utf-8") as f:
        cases = [line.strip() for line in f if line.strip()]

    kept = [c for c in cases if is_advertised(c)]

    out = sys.stdout if args.output == "-" else open(args.output, "w", encoding="utf-8")
    try:
        for c in kept:
            print(c, file=out)
    finally:
        if out is not sys.stdout:
            out.close()

    print(f"filter_vulkan_cts_cases: kept {len(kept)} of {len(cases)} cases", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
