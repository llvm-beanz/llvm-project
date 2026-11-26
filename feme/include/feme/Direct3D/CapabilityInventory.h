#ifndef FEME_DIRECT3D_CAPABILITYINVENTORY_H
#define FEME_DIRECT3D_CAPABILITYINVENTORY_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

struct CapabilityInventory {
  uint64_t NegotiatedDdiVersion = 0;
  bool Compute = false;
  bool Graphics = false;
  bool Mesh = false;
  bool RayTracing = false;
  bool Presentation = false;
  bool FL122 = false;
};

llvm::Error validateCapabilityInventory(const CapabilityInventory &Inventory);

} // namespace feme::direct3d

#endif
