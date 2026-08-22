#!/usr/bin/env python3
"""Enumerates every non-disabled `VK_KHR_*`/`VK_EXT_*` extension `vk.xml`
declares and cross-checks it against what this ICD actually advertises (see
feme/docs/VulkanExtensionInventory.md), the same "read the registry directly,
don't re-derive the list by hand" discipline
`vk_gen_feature_inventory.py`/`vk_gen_entrypoints.py` already use.

A `KHR`/`EXT`-suffixed extension is one of three states here, never a bare
yes/no:

- **Advertised**: `PhysicalDeviceInfo.cpp`'s `getSupportedDeviceExtensions`
  lists it by name (`--advertised` file).
- **Implemented (core, not advertised by name)**: its functionality exists
  through a core `VK_VERSION_1_x` entry point this ICD's `apiVersion`
  already promotes it into, but the extension's own name is never listed
  (`--core-promoted` file) -- a real Vulkan-legal state (an app using the
  core name gets the real implementation regardless), distinct from either
  of the other two.
- **Not implemented**: neither of the above.

Usage:
    vk_gen_extension_inventory.py <vk.xml> \
        --advertised <file> --core-promoted <file> [-o <output.md>]

`--advertised`/`--core-promoted` each name a file listing one `name` or
`name = note` per line ('#' comments allowed), the same format
`vk_gen_feature_inventory.py`'s own `--advertised-*` files already use.
"""

import argparse
import xml.etree.ElementTree as ET


def supported_extensions(root, prefix):
    """Returns the sorted list of non-disabled extension names `vk.xml`
    declares starting with \\p prefix ("VK_KHR_" or "VK_EXT_") -- excluding
    anything `supported="disabled"` (withdrawn/never-shipped) or missing
    "vulkan" from its `supported` list (e.g. a VulkanSC-only extension)."""
    names = []
    for extension in root.findall("./extensions/extension"):
        name = extension.get("name")
        if not name.startswith(prefix):
            continue
        supported = extension.get("supported", "").split(",")
        if "vulkan" not in supported:
            continue
        names.append(name)
    return sorted(names)


def read_names(path):
    """Reads a `--advertised`/`--core-promoted` file: one `name` or
    `name = note` per line ('#' comments allowed). Returns {name: note}."""
    if not path:
        return {}
    names = {}
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            name, _, note = line.partition("=")
            names[name.strip()] = note.strip()
    return names


def render(extensions, advertised, core_promoted):
    lines = [
        "| Extension | Status | Note |",
        "|---|---|---|",
    ]
    for name in extensions:
        if name in advertised:
            status = "Advertised"
            note = advertised[name]
        elif name in core_promoted:
            status = "Implemented (core, not advertised by name)"
            note = core_promoted[name]
        else:
            status = "Not implemented"
            note = ""
        lines.append(f"| `{name}` | {status} | {note} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vk_xml", help="Path to Vulkan-Headers' vk.xml")
    parser.add_argument(
        "--advertised",
        required=True,
        help="Path to a file listing extension names "
        "getSupportedDeviceExtensions advertises by name, one per line",
    )
    parser.add_argument(
        "--core-promoted",
        required=True,
        help="Path to a file listing extension names implemented through a "
        "core VK_VERSION_1_x entry point but not advertised by name, one "
        "per line",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Path to write the generated Markdown table to (defaults to "
        "stdout)",
    )
    args = parser.parse_args()

    tree = ET.parse(args.vk_xml)
    root = tree.getroot()
    extensions = sorted(
        supported_extensions(root, "VK_KHR_") + supported_extensions(root, "VK_EXT_")
    )

    advertised = read_names(args.advertised)
    core_promoted = read_names(args.core_promoted)

    unknown = (set(advertised) | set(core_promoted)) - set(extensions)
    if unknown:
        raise SystemExit(
            "vk_gen_extension_inventory.py: --advertised/--core-promoted "
            "list names that are not a non-disabled VK_KHR_*/VK_EXT_* "
            "extension in vk.xml: " + ", ".join(sorted(unknown))
        )
    overlap = set(advertised) & set(core_promoted)
    if overlap:
        raise SystemExit(
            "vk_gen_extension_inventory.py: --advertised and "
            "--core-promoted both list: " + ", ".join(sorted(overlap))
        )

    output = render(extensions, advertised, core_promoted)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
