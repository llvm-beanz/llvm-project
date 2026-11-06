# Fixture: a Vulkan14FeatureInventory.md whose table exactly matches what
# Inputs/.../vk_gen_feature_inventory.py (the fake generator) always
# produces, so run-check-feme-capability-manifest.test's feature-inventory
# half exercises the "unchanged" path.

Fake preamble prose, as a real doc would have before its generated table.

| Category | Version | Name | Advertised | Note |
|---|---|---|---|---|
| feature | VK_VERSION_1_0 | `fakeFeatureOne` | yes |  |
| feature | VK_VERSION_1_0 | `fakeFeatureTwo` | no |  |
