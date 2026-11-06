# Fixture: a VulkanExtensionInventory.md whose table deliberately differs
# from what Inputs/.../vk_gen_extension_inventory.py (the fake generator)
# always produces (second row: "Not implemented" here vs. "Advertised"
# there), so run-check-feme-capability-manifest.test's extension-inventory
# half exercises the "drifted" path and names the changed row.

Fake preamble prose, as a real doc would have before its generated table.

| Extension | Status | Note |
|---|---|---|
| `VK_FAKE_extension_one` | Advertised |  |
| `VK_FAKE_extension_two` | Not implemented |  |
