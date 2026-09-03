#!/usr/bin/env python3
# Generates the FeMe Vulkan ICD's entrypoint table from Vulkan-Headers'
# vk.xml (see "Loader Integration" in feme/docs/FeMeVulkanDesign.md).
#
# Hand-maintaining command names, aliases, and core-version promotions is
# error-prone even when only a subset is implemented, so this reads vk.xml
# itself rather than a checked-in list. It emits one X-macro invocation per
# core Vulkan 1.0/1.1 command:
#
#   FEME_VK_COMMAND(name, dispatch_level)       -- not implemented
#   FEME_VK_COMMAND_IMPL(name, dispatch_level)  -- implemented
#
# `dispatch_level` is one of GLOBAL, INSTANCE, DEVICE, matching how the
# Vulkan loader itself classifies a command (by its first parameter's
# dispatchable handle type, if any). A command name listed in the
# `--implemented` file (one name per line, '#' comments allowed) is emitted
# through `FEME_VK_COMMAND_IMPL` instead of `FEME_VK_COMMAND`, so a caller
# that only defines the latter (mapping every entry to null) still sees
# every command name -- matching "Loader Integration"'s requirement that the
# generated table cover every known command name -- while a caller that also
# defines `FEME_VK_COMMAND_IMPL` (mapping it to a real, distinctly named C++
# symbol) never takes the address of a function that doesn't exist for an
# unimplemented command.
#
# Only the VK_VERSION_1_0 through VK_VERSION_1_4 core features, plus the
# extensions in `SUPPORTED_EXTENSIONS`, are read (V3 needs 1.2's core,
# non-`KHR`-suffixed timeline-semaphore commands --
# `vkWaitSemaphores`/`vkSignalSemaphore`/`vkGetSemaphoreCounterValue` --
# rather than adding extension support for `VK_KHR_timeline_semaphore`;
# roadmap D0 needs 1.3's core, non-`KHR`-suffixed `VK_KHR_copy_commands2`
# family for the same reason (see "Roadmap D0: measured impact" in
# VulkanCTSReport.md); roadmap D1 added VK_VERSION_1_4 itself so the
# generated table covers its 19 new core commands (e.g. `vkMapMemory2`,
# `vkCmdBindDescriptorSets2`) even though none of them are implemented yet
# -- `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` already returned null for
# an unlisted name exactly like they do for a listed, unimplemented one
# (see ProcAddr.cpp's `findEntry`), so this is a coverage fix, not a
# behavior change; every other 1.2/1.3/1.4 command this ICD does not
# implement is simply left unimplemented, exactly like most of 1.1's own
# surface already is). An extension's commands are read only when the
# extension is listed in `SUPPORTED_EXTENSIONS`, which means this driver
# genuinely implements and advertises it -- never merely because
# Vulkan-Headers declares it (see FeMeVulkanDesign.md's "Loader
# Integration").
#
# Newer vk.xml revisions split each `VK_VERSION_1_x` feature into several
# `VK_{BASE,COMPUTE,GRAPHICS}_VERSION_1_x` features linked by a `depends`
# attribute (e.g. `VK_VERSION_1_0` itself now requires no commands directly;
# it just `depends` on `VK_GRAPHICS_VERSION_1_0`, which in turn `depends` on
# `VK_COMPUTE_VERSION_1_0`, and so on), so a command's containing feature must
# be resolved transitively through `depends` rather than assumed to be a
# direct `<feature name="VK_VERSION_1_x">`.

import argparse
import re
import sys
import xml.etree.ElementTree as ET

CORE_FEATURES = (
    "VK_VERSION_1_0",
    "VK_VERSION_1_1",
    "VK_VERSION_1_2",
    "VK_VERSION_1_3",
    "VK_VERSION_1_4",
)

# Extensions this driver implements and advertises, whose commands the
# generated table must therefore carry (V6: `vkCmdBeginRenderingKHR`/
# `vkCmdEndRenderingKHR`, since dynamic rendering is core only in 1.3 while
# this driver advertises 1.2 -- see "Render passes and dynamic rendering" in
# feme/docs/FeMeVulkanDesign.md; roadmap C4c: `VK_EXT_extended_dynamic_
# state`'s 12 `vkCmdSet*EXT`/`vkCmdBindVertexBuffers2EXT` commands, closing
# roadmap C4's "mapDynamicState beyond its six states"; roadmap E29:
# `VK_KHR_maintenance5`'s own commands, for the same reason as dynamic
# rendering above -- the loader's `icd.json` `api_version` (1.1) makes it
# reject a direct, non-`KHR`-suffixed query for a command newer than that,
# so this driver must implement the `KHR` name to be reachable through the
# loader at all, confirmed by `dEQP-VK.api.granularity.
# in_dynamic_render_pass.*` SIGSEGV'ing on exactly this gap). Every name
# here must also appear in `feme::vulkan::getSupportedDeviceExtensions`.
SUPPORTED_EXTENSIONS = (
    "VK_KHR_dynamic_rendering",
    "VK_EXT_extended_dynamic_state",
    "VK_KHR_maintenance5",
    # (roadmap F5) `vkCmdSetLineStippleKHR`, the extension's one command
    # (CommandBuffer.cpp). `dEQP-VK.pipeline.line_rasterization.*` enables
    # this extension by name regardless of the advertised `apiVersion`,
    # the same reason every other post-`maintenance5` entry in this tuple
    # is listed.
    "VK_KHR_line_rasterization",
    # (roadmap F8/F8a) `vkCmdSetRenderingAttachmentLocations`/
    # `vkCmdSetRenderingInputAttachmentIndices` (CommandBuffer.cpp) are both
    # implemented, and a fragment shader's `subpassInput` local read now
    # produces real pixels (SPIRVToLLVMPatterns.cpp's `SubpassLoadPattern`,
    # FragmentWrapper.cpp's `lowerFragmentSubpassLoad`) -- see
    # `dynamicRenderingLocalRead`'s own feature-bit comment in
    # EntryPoints.cpp for this row's single-sample-color-attachment scope.
    "VK_KHR_dynamic_rendering_local_read",
    # (roadmap F14) `vkMapMemory2KHR`/`vkUnmapMemory2KHR` (Memory.cpp), thin
    # wrappers around the core, non-`KHR`-suffixed `vkMapMemory2`/
    # `vkUnmapMemory2` this driver also implements. Like `maintenance5`
    # above, `vktMemoryMappingTests.cpp`'s own `mapMemoryWrapper`/
    # `unmapMemoryWrapper` fall back to the `KHR` name whenever `deqp-vk`'s
    # own negotiated `usedApiVersion` for a test is below 1.4 (confirmed by
    # a real CTS run SIGSEGV'ing in `testMemoryMapping` through a null
    # `m_vk.mapMemory2` exactly the way the granularity functions did), so
    # the core name alone is not reachable through the loader for every
    # caller.
    "VK_KHR_map_memory2",
    # (roadmap H6f) `vkCmdDrawMeshTasksEXT`/`vkCmdDrawMeshTasksIndirectEXT`/
    # `vkCmdDrawMeshTasksIndirectCountEXT` (CommandBuffer.cpp) route through
    # the same prepared-draw code `vkCmdDraw*` already uses
    # (`runPreparedDraw`), closing the milestone alongside mesh pipeline
    # creation (GraphicsPipeline.cpp) and this driver's own
    # `VkPhysicalDeviceMeshShaderPropertiesEXT` limits (PhysicalDeviceInfo.
    # cpp). `dEQP-VK.mesh_shader.*` enables this extension by name, the
    # same reason every other post-`maintenance5` entry in this tuple is
    # listed.
    "VK_EXT_mesh_shader",
    # (roadmap H7u) `vkCmdPushDescriptorSetKHR`/
    # `vkCmdPushDescriptorSetWithTemplateKHR` (CommandBuffer.cpp), thin
    # forwarders to the core, non-`KHR`-suffixed `vkCmdPushDescriptorSet`/
    # `vkCmdPushDescriptorSetWithTemplate` this driver also implements.
    # Like `VK_KHR_map_memory2`/`VK_KHR_maintenance5` above, a real caller
    # that resolves the extension's own name directly (rather than the
    # core-1.4-only name) gets a null function pointer without this,
    # confirmed by a real `dEQP-VK...with_push...storage_buffer.vertex*`
    # case SIGSEGV'ing through exactly such a null pointer, newly reachable
    # only once roadmap H7g's own feature-bit flip let this case clear an
    # earlier gate for the first time.
    "VK_KHR_push_descriptor",
    # (roadmap H10) `VK_KHR_surface`/`VK_EXT_headless_surface`'s object/
    # query/creation commands (Surface.{h,cpp}) and `VK_KHR_swapchain`'s
    # own commands (Swapchain.{h,cpp}) -- see FeMeVulkanDesign.md's
    # "Window-system integration". Unlike every extension above,
    # `VK_KHR_surface`/`VK_EXT_headless_surface` are `type="instance"` in
    # `vk.xml`, not `type="device"`; this tuple does not distinguish
    # instance- from device-level extensions itself (dispatch level is
    # inferred per-command from `INSTANCE_HANDLE_TYPES`/
    # `DEVICE_HANDLE_TYPES` below instead), so listing all three together
    # here is correct.
    "VK_KHR_surface",
    "VK_EXT_headless_surface",
    "VK_KHR_swapchain",
)

# First-parameter handle types that make a command dispatched at the
# instance level rather than the device level. VkPhysicalDevice commands are
# dispatched through the instance's table (the loader has no per-physical-
# device dispatch table), matching the Vulkan loader's own convention.
INSTANCE_HANDLE_TYPES = {"VkInstance", "VkPhysicalDevice"}
DEVICE_HANDLE_TYPES = {"VkDevice", "VkQueue", "VkCommandBuffer"}


def resolve_dependent_features(name, features_by_name, resolved=None):
    """Returns the transitive closure of `name` and every feature it
    (recursively) `depends` on, per `features_by_name` (name -> <feature>).

    A `depends` attribute is a boolean expression combining feature/extension
    names with `+` (AND) and `,` (OR), optionally parenthesized; every name it
    mentions is walked regardless of which operator joins it; because core
    version features never gate a `<command>` requirement behind an OR'd
    extension (see the `depends` note above `CORE_FEATURES`), this closure is
    exactly the set of sub-features a core version is built from.
    """
    if resolved is None:
        resolved = set()
    if name in resolved or name not in features_by_name:
        return resolved
    resolved.add(name)
    depends = features_by_name[name].get("depends")
    if depends:
        for dependency in re.findall(r"[A-Za-z0-9_]+", depends):
            resolve_dependent_features(dependency, features_by_name, resolved)
    return resolved


def parse_commands(vk_xml_path):
    """Returns {command_name: dispatch_level} for every core 1.0/1.1 command,
    resolving `alias` commands to their target's dispatch level."""
    tree = ET.parse(vk_xml_path)
    root = tree.getroot()

    # Map every <command>, keyed by name, to either its first parameter's
    # type (a proto command) or its alias target (an alias command).
    protos = {}
    aliases = {}
    for command in root.findall("./commands/command"):
        name = command.get("name")
        alias = command.get("alias")
        if alias:
            aliases[name] = alias
            continue
        proto = command.find("proto/name")
        name = proto.text
        first_param = command.find("param/type")
        protos[name] = first_param.text if first_param is not None else None

    def dispatch_level(name):
        seen = set()
        while name in aliases:
            if name in seen:
                raise ValueError(f"alias cycle involving {name}")
            seen.add(name)
            name = aliases[name]
        handle_type = protos.get(name)
        if handle_type in INSTANCE_HANDLE_TYPES:
            return "INSTANCE"
        if handle_type in DEVICE_HANDLE_TYPES:
            return "DEVICE"
        return "GLOBAL"

    features_by_name = {
        feature.get("name"): feature for feature in root.findall("./feature")
    }
    core_feature_names = set()
    for feature_name in CORE_FEATURES:
        core_feature_names |= resolve_dependent_features(feature_name, features_by_name)

    extensions_by_name = {
        extension.get("name"): extension
        for extension in root.findall("./extensions/extension")
    }

    commands = {}
    for extension_name in SUPPORTED_EXTENSIONS:
        extension = extensions_by_name.get(extension_name)
        if extension is None:
            raise ValueError(f"{extension_name} is not declared in vk.xml")
        for require in extension.findall("require"):
            for command_ref in require.findall("command"):
                name = command_ref.get("name")
                commands[name] = dispatch_level(name)
    for feature_name in core_feature_names:
        feature = features_by_name[feature_name]
        api = feature.get("api", "")
        if "vulkan" not in api.split(","):
            continue
        for require in feature.findall("require"):
            # A `<require depends="...">` conditions its contents on an
            # optional extension or struct field rather than the core
            # version itself; core versions never gate a `<command>` this
            # way (see the note above `CORE_FEATURES`), but skip such blocks
            # defensively rather than assume that always holds.
            if require.get("depends"):
                continue
            for command_ref in require.findall("command"):
                name = command_ref.get("name")
                commands[name] = dispatch_level(name)
    return commands


def read_implemented(path):
    if not path:
        return set()
    names = set()
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                names.add(line)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vk_xml", help="Path to Vulkan-Headers' vk.xml")
    parser.add_argument("output", help="Path to the generated .inc file")
    parser.add_argument(
        "--implemented",
        help="Path to a file listing implemented command names, one per line",
    )
    args = parser.parse_args()

    commands = parse_commands(args.vk_xml)
    implemented = read_implemented(args.implemented)
    unknown_implemented = implemented - set(commands)
    if unknown_implemented:
        sys.exit(
            "vk_gen_entrypoints.py: --implemented lists commands that are "
            "neither core Vulkan entrypoints nor commands of a supported "
            "extension: "
            + ", ".join(sorted(unknown_implemented))
        )

    with open(args.output, "w") as out:
        out.write(
            "// Generated by feme/utils/vk_gen_entrypoints.py from vk.xml. "
            "Do not edit.\n"
            "#ifndef FEME_VK_COMMAND\n"
            '#error "FEME_VK_COMMAND must be defined before including this '
            'file"\n'
            "#endif\n"
            "#ifndef FEME_VK_COMMAND_IMPL\n"
            "#define FEME_VK_COMMAND_IMPL(name, level) FEME_VK_COMMAND(name, "
            "level)\n"
            "#define FEME_VK_GEN_UNDEF_COMMAND_IMPL\n"
            "#endif\n"
        )
        for name in sorted(commands):
            macro = "FEME_VK_COMMAND_IMPL" if name in implemented else "FEME_VK_COMMAND"
            out.write(f"{macro}({name}, {commands[name]})\n")
        out.write(
            "#undef FEME_VK_COMMAND\n"
            "#ifdef FEME_VK_GEN_UNDEF_COMMAND_IMPL\n"
            "#undef FEME_VK_COMMAND_IMPL\n"
            "#undef FEME_VK_GEN_UNDEF_COMMAND_IMPL\n"
            "#endif\n"
        )


if __name__ == "__main__":
    main()
