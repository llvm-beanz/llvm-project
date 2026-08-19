//===- ResourceLowering.cpp - Lower raised resources to AMDGPU -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/AMDGPU/ResourceLowering.h"

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/DXILResource.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;
using namespace feme::amdgpu;

namespace {

/// AMDGPU's global address space, which is where a kernel's buffer arguments
/// point.
constexpr unsigned GlobalAddressSpace = 1;

/// Which of the two parallel raised resource-op families (see
/// feme::dxil::OpRaisingPass and feme::SPIRVToLLVMTranslator, "Per-Format
/// Representation Strategy" in feme/docs/Design.md) a binding is expressed
/// with. The families are parallel in what they let a shader do -- create a
/// handle, then read/write a typed buffer element through it -- but not in
/// how they spell the access itself: DX has a dedicated
/// load/store-typedbuffer intrinsic pair, where the load additionally
/// returns a `{value, checkbit}` pair; SPIR-V instead has a `getpointer`
/// intrinsic addressing an element, which ordinary `load`/`store`
/// instructions then go through (see feme::SPIRVToLLVMTranslator's
/// `ImageReadPattern`/`ImageWritePattern`, which spell a DXIL-raised typed
/// buffer access from the *other* direction the same way -- see
/// feme::spirv::RaisedLoweringPass). This pass therefore handles the two
/// shapes with separate code paths, selected by \p Family, rather than
/// forcing one shape's rewrite logic onto the other's ops.
enum class ResourceFamily { DX, SPIRV, DXTexture, DXCBuffer };

struct ResourceOps {
  ResourceFamily Family;
  Intrinsic::ID HandleFromBinding;
  StringRef HandleTypeName;
};

constexpr ResourceOps DXResourceOps = {ResourceFamily::DX,
                                       Intrinsic::dx_resource_handlefrombinding,
                                       "dx.TypedBuffer"};
constexpr ResourceOps SPIRVResourceOps = {
    ResourceFamily::SPIRV, Intrinsic::spv_resource_handlefrombinding,
    "spirv.Image"};
// `dx.Texture`/`dx.CBuffer` handles are materialized by the exact same
// `llvm.dx.resource.handlefrombinding` intrinsic call as a `dx.TypedBuffer`
// handle -- only the handle's own `target("dx.")` result type differs --
// so `getResourceOps` below has to disambiguate by that result type's name,
// not the intrinsic ID alone.
constexpr ResourceOps DXTextureResourceOps = {
    ResourceFamily::DXTexture, Intrinsic::dx_resource_handlefrombinding,
    "dx.Texture"};
constexpr ResourceOps DXCBufferResourceOps = {
    ResourceFamily::DXCBuffer, Intrinsic::dx_resource_handlefrombinding,
    "dx.CBuffer"};
constexpr const ResourceOps *AllResourceOps[] = {
    &DXResourceOps, &SPIRVResourceOps, &DXTextureResourceOps,
    &DXCBufferResourceOps};

/// One resource binding an entry point uses, together with every
/// `...resource.handlefrombinding` call that materializes a handle for it.
/// Multiple calls are common: a shader that touches the same resource from
/// several places gets one handle each time.
struct Binding {
  const ResourceOps *Ops = nullptr;
  uint32_t Space = 0;
  uint32_t Register = 0;
  /// Whether this is a UAV (`RWBuffer`/`RWTexture*`) rather than an SRV
  /// (`Buffer`/`Texture*`) binding. HLSL's `t`/`u` registers are independent
  /// namespaces, so a `Texture2D` at `t0` and an `RWTexture2D` at `u0` (as
  /// in a typical read/write compute shader pair) share the same (space,
  /// register) pair despite being two entirely different bindings; this
  /// disambiguates them the same way the resource class byte
  /// `dx.op.createHandle`'s legacy encoding carries alongside range ID
  /// does. Only meaningful (and only read) for the `DX`/`DXTexture`
  /// families, whose handle type already carries it as int parameter 0
  /// (`TypedBufferExtType`/`TextureExtType::isWriteable()`) -- SPIR-V's
  /// descriptor `binding` numbers and DX's `b`-register cbuffers are each
  /// already a distinct namespace with no such SRV/UAV ambiguity to begin
  /// with, so it is always `false` (irrelevant) for those two families.
  bool IsUAV = false;
  Type *ElementType = nullptr;
  /// Extra `i32` kernel arguments this binding needs beyond its own data
  /// pointer, appended immediately after it. Always 0 except for a
  /// `DXTexture` binding of more than one coordinate dimension, which needs
  /// one flat-addressing stride per coordinate beyond the first (see
  /// `lowerDXTextureAccess`'s comment) -- a real hardware texture unit gets
  /// this from the bound resource descriptor itself, which a flat AMDGPU
  /// kernel argument list has no equivalent of.
  unsigned NumAuxArgs = 0;
  SmallVector<CallInst *, 2> Handles;

  bool operator<(const Binding &Other) const {
    // `IsUAV` is included so an SRV and a UAV binding sharing one (space,
    // register) pair (see `IsUAV`'s own comment) still sort into a fully
    // deterministic, total order rather than an unspecified one relative to
    // each other.
    return std::tie(Space, Register, IsUAV) <
           std::tie(Other.Space, Other.Register, Other.IsUAV);
  }
};

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// Returns the resource op family whose `...handlefrombinding` intrinsic is
/// \p ID and whose raised handle type is named \p HandleTypeName, or
/// nullptr if no entry of `AllResourceOps` matches both (matching only
/// \p ID would not disambiguate `dx.TypedBuffer`/`dx.Texture`/`dx.CBuffer`,
/// which all share one intrinsic -- see `AllResourceOps`'s comment).
const ResourceOps *getResourceOps(Intrinsic::ID ID, StringRef HandleTypeName) {
  for (const ResourceOps *Ops : AllResourceOps)
    if (Ops->HandleFromBinding == ID && Ops->HandleTypeName == HandleTypeName)
      return Ops;
  return nullptr;
}

/// Returns the number of coordinate components DXIL's texture load/store
/// ops pack for \p Dim (mirroring `feme::dxil::OpRaisingPass::
/// getTextureCoordComponents`'s identically-shaped table in OpRaising.cpp),
/// or 0 for a dimension this pass does not address. Unlike that raising-time
/// table, cube and array forms are deliberately excluded here: this pass
/// flattens a texture into one `ptr addrspace(1)` plus one flat-addressing
/// stride per extra dimension (see `Binding::NumAuxArgs`), which models
/// linear row/slice pitch fine for 1D/2D/3D but not a cube face or array
/// layer's independent (non-strided) indexing.
unsigned getTextureCoordComponents(dxil::ResourceKind Dim) {
  switch (Dim) {
  case dxil::ResourceKind::Texture1D:
    return 1;
  case dxil::ResourceKind::Texture2D:
    return 2;
  case dxil::ResourceKind::Texture3D:
    return 3;
  default:
    return 0;
  }
}

/// Checks that every use of the handle \p HandleCI produces is a typed
/// buffer, texture, or cbuffer access this pass can rewrite.
///
/// A DX typed buffer load's result must additionally only be consumed by
/// `extractvalue`, since the raised intrinsic returns a `{value, checkbit}`
/// pair that has no lowered counterpart of its own. A SPIR-V `getpointer`
/// call's result must be consumed by exactly one ordinary `load`, or by
/// exactly one `store` it is the pointer operand of -- anything else
/// (multiple uses, a store that uses it as the *stored* value, ...) is more
/// than a single element access, which this pass does not attempt to reason
/// about. A `dx.Texture` handle's `llvm.dx.resource.load.level`/
/// `.store.texture` calls need no such per-use check: by the time this pass
/// runs, `feme::dxil::OpRaisingPass` has already reassembled the whole
/// element vector on both sides (`replaceResRetExtracts`/the `insertelement`
/// chain `raiseTextureStore` builds), so the handle's own use is simply
/// "resource operand of one of those two calls". A `dx.CBuffer` handle's
/// `llvm.dx.resource.load.cbufferrow.*` call result, in contrast, is left
/// as a set of per-field `extractvalue`s (see `raiseCBufferLoadLegacy`'s own
/// comment for why), so those need the same "only extractvalue" check a
/// typed buffer load's result does.
bool hasOnlySupportedUses(const CallInst &HandleCI, const ResourceOps &Ops) {
  for (const User *U : HandleCI.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;

    if (Ops.Family == ResourceFamily::DX) {
      switch (getIntrinsicID(CI)) {
      case Intrinsic::dx_resource_store_typedbuffer:
        // The handle must be the resource operand, not the stored value.
        if (CI->getArgOperand(0) != &HandleCI)
          return false;
        break;
      case Intrinsic::dx_resource_load_typedbuffer:
        for (const User *LoadUser : CI->users())
          if (!isa<ExtractValueInst>(LoadUser))
            return false;
        break;
      default:
        return false;
      }
      continue;
    }

    if (Ops.Family == ResourceFamily::DXTexture) {
      Intrinsic::ID ID = getIntrinsicID(CI);
      if ((ID != Intrinsic::dx_resource_load_level &&
           ID != Intrinsic::dx_resource_store_texture) ||
          CI->getArgOperand(0) != &HandleCI)
        return false;
      continue;
    }

    if (Ops.Family == ResourceFamily::DXCBuffer) {
      switch (getIntrinsicID(CI)) {
      case Intrinsic::dx_resource_load_cbufferrow_2:
      case Intrinsic::dx_resource_load_cbufferrow_4:
      case Intrinsic::dx_resource_load_cbufferrow_8:
        if (CI->getArgOperand(0) != &HandleCI)
          return false;
        for (const User *RowUser : CI->users())
          if (!isa<ExtractValueInst>(RowUser))
            return false;
        break;
      default:
        return false;
      }
      continue;
    }

    if (getIntrinsicID(CI) != Intrinsic::spv_resource_getpointer ||
        !CI->hasOneUse())
      return false;
    const User *AccessUser = *CI->user_begin();
    if (const auto *SI = dyn_cast<StoreInst>(AccessUser)) {
      if (SI->getPointerOperand() != CI)
        return false;
    } else if (!isa<LoadInst>(AccessUser)) {
      return false;
    }
  }
  return true;
}

/// Returns the element type \p HandleCI's typed buffer/texture/cbuffer
/// accesses operate on. DX's `target("dx.TypedBuffer"/"dx.Texture", ElemTy,
/// ...)` handle types spell this directly as a type parameter. SPIR-V's
/// `target("spirv.Image", ...)` handle type does not -- its parameters
/// describe the underlying image's dimensionality/sampled type, not the
/// (possibly vector) type a particular access loads or stores -- so it is
/// instead read off the load/store through the first `getpointer` call
/// found. `dx.CBuffer`'s opaque `target("dx.CBuffer", [N x i8])` placeholder
/// handle type (see `getOpaqueSizedType` in OpRaising.cpp) carries no
/// element type at all, so this reads the same way SPIR-V's does: off the
/// first `llvm.dx.resource.load.cbufferrow.*` call found, whose return
/// struct's (uniform, per `getCBufferRowIntrinsic` in OpRaising.cpp) field
/// type is the per-row-element type a cbuffer access this pass rewrites
/// needs. Returns nullptr if \p HandleCI has no accesses to read it from.
Type *getElementType(const CallInst &HandleCI, const ResourceOps &Ops) {
  if (Ops.Family == ResourceFamily::DX || Ops.Family == ResourceFamily::DXTexture)
    return cast<TargetExtType>(HandleCI.getType())->getTypeParameter(0);

  if (Ops.Family == ResourceFamily::DXCBuffer) {
    for (const User *U : HandleCI.users()) {
      const auto *Row = cast<CallInst>(U);
      return cast<StructType>(Row->getType())->getElementType(0);
    }
    return nullptr;
  }

  for (const User *U : HandleCI.users()) {
    const auto *GetPointer = cast<CallInst>(U);
    const User *AccessUser = *GetPointer->user_begin();
    if (const auto *LI = dyn_cast<LoadInst>(AccessUser))
      return LI->getType();
    if (const auto *SI = dyn_cast<StoreInst>(AccessUser))
      return SI->getValueOperand()->getType();
  }
  return nullptr;
}

/// Collects the resource bindings \p F uses, or `std::nullopt` if any of them
/// is one this pass cannot model (see ResourceLoweringPass's class comment).
/// Bindings are returned in a deterministic (space, register) order, which is
/// the order they become kernel arguments in.
std::optional<SmallVector<Binding, 4>> collectBindings(Function &F) {
  SmallVector<Binding, 4> Bindings;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    auto *HandleTy = dyn_cast<TargetExtType>(CI->getType());
    if (!HandleTy)
      continue;
    const ResourceOps *Ops =
        getResourceOps(getIntrinsicID(CI), HandleTy->getName());
    if (!Ops)
      continue;

    auto *Space = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    auto *LowerBound = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    auto *Index = dyn_cast<ConstantInt>(CI->getArgOperand(3));
    if (!Space || !LowerBound || !Index)
      return std::nullopt;
    if (!hasOnlySupportedUses(*CI, *Ops))
      return std::nullopt;

    Type *ElementType = getElementType(*CI, *Ops);
    if (!ElementType)
      return std::nullopt;

    unsigned NumAuxArgs = 0;
    if (Ops->Family == ResourceFamily::DXTexture) {
      unsigned NumCoords = getTextureCoordComponents(
          cast<dxil::TextureExtType>(HandleTy)->getDimension());
      if (NumCoords == 0)
        return std::nullopt;
      NumAuxArgs = NumCoords - 1;
    }
    bool IsUAV = (Ops->Family == ResourceFamily::DX ||
                  Ops->Family == ResourceFamily::DXTexture) &&
                 HandleTy->getIntParameter(0) != 0;

    uint32_t Register = static_cast<uint32_t>(LowerBound->getZExtValue() +
                                              Index->getZExtValue());
    // `Ops` (i.e. resource family: DX typed-buffer/texture, DX cbuffer, or
    // SPIR-V image) is part of the lookup key, not just checked afterwards
    // as a conflict: HLSL's `t`/`b`/`u` registers are independent
    // namespaces, so a `Texture2D` at `t0` and a `cbuffer` at `b0` (as in
    // this shader) legitimately share the numeric pair (space 0, register
    // 0) without being the same binding at all -- unlike an actual same-
    // family, same-(space,register,IsUAV) mismatch below, which *is* a
    // genuine conflict (the same resource reached with two incompatible
    // element types).
    Binding *Existing = llvm::find_if(Bindings, [&](const Binding &B) {
      return B.Space == Space->getZExtValue() && B.Register == Register &&
             B.IsUAV == IsUAV && B.Ops == Ops;
    });
    if (Existing != Bindings.end()) {
      // The same binding reached through two different element types would
      // need two differently-typed pointers for one resource.
      if (Existing->ElementType != ElementType)
        return std::nullopt;
      Existing->Handles.push_back(CI);
      continue;
    }

    Binding NewBinding;
    NewBinding.Ops = Ops;
    NewBinding.Space = static_cast<uint32_t>(Space->getZExtValue());
    NewBinding.Register = Register;
    NewBinding.IsUAV = IsUAV;
    NewBinding.ElementType = ElementType;
    NewBinding.NumAuxArgs = NumAuxArgs;
    NewBinding.Handles.push_back(CI);
    Bindings.push_back(std::move(NewBinding));
  }

  llvm::sort(Bindings);
  return Bindings;
}

/// Builds \p F's replacement: the same function with one trailing
/// `ptr addrspace(1)` parameter per entry of \p Bindings, each immediately
/// followed by that binding's own `Binding::NumAuxArgs` trailing `i32`
/// stride arguments (nonzero only for a multi-dimensional `dx.Texture`
/// binding -- see `Binding::NumAuxArgs`'s comment). The body is moved
/// across rather than cloned, so every instruction (including the handle
/// calls recorded in \p Bindings) stays valid and belongs to the new
/// function afterwards.
Function *addBindingArguments(Function &F, ArrayRef<Binding> Bindings) {
  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  Type *PtrTy = PointerType::get(F.getContext(), GlobalAddressSpace);
  Type *Int32Ty = Type::getInt32Ty(F.getContext());
  for (const Binding &B : Bindings) {
    ParamTypes.push_back(PtrTy);
    ParamTypes.append(B.NumAuxArgs, Int32Ty);
  }

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  NewF->setComdat(F.getComdat());
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : llvm::zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  unsigned ArgIndex = F.arg_size();
  for (const Binding &B : Bindings) {
    Argument &Arg = *(NewF->arg_begin() + ArgIndex++);
    SmallString<32> Name;
    raw_svector_ostream(Name) << "res.space" << B.Space << ".reg" << B.Register;
    Arg.setName(Name);
    for (unsigned I = 0; I != B.NumAuxArgs; ++I) {
      Argument &AuxArg = *(NewF->arg_begin() + ArgIndex++);
      SmallString<32> AuxName;
      raw_svector_ostream(AuxName)
          << Name << ".stride" << I;
      AuxArg.setName(AuxName);
    }
  }

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

/// Rewrites a DX `load`/`store`-typedbuffer call \p Access -- the resource
/// operand of both is always argument 0, the element index argument 1, and
/// the stored value (for a store) argument 2 -- into an ordinary, aligned
/// load/store of \p Ptr.
void lowerDXAccess(CallInst &Access, Value *Ptr, Type *ElementType,
                   Align Alignment) {
  IRBuilder<> Builder(&Access);
  Value *Elem = Builder.CreateGEP(ElementType, Ptr, Access.getArgOperand(1));

  if (getIntrinsicID(&Access) == Intrinsic::dx_resource_store_typedbuffer) {
    Builder.CreateAlignedStore(Access.getArgOperand(2), Elem, Alignment);
    Access.eraseFromParent();
    return;
  }

  Value *Loaded = Builder.CreateAlignedLoad(ElementType, Elem, Alignment);
  // Field 0 is the loaded value; field 1 is the "checkbit" reporting whether
  // the access was in bounds, which plain memory always is.
  for (User *LoadUser : llvm::make_early_inc_range(Access.users())) {
    auto *EV = cast<ExtractValueInst>(LoadUser);
    Value *Replacement =
        EV->getIndices()[0] == 0
            ? Loaded
            : cast<Value>(ConstantInt::getTrue(EV->getContext()));
    EV->replaceAllUsesWith(Replacement);
    EV->eraseFromParent();
  }
  Access.eraseFromParent();
}

/// Rewrites a SPIR-V `llvm.spv.resource.getpointer` call \p Access into the
/// GEP'd element pointer it addresses within \p Ptr, and points the single
/// `load`/`store` already reading or writing through it (see
/// `hasOnlySupportedUses`) at that pointer directly -- it needs no other
/// changes, since it only cares about the pointer value, not which address
/// space computed it. (A plain `replaceAllUsesWith` cannot do this instead:
/// \p Ptr's address space differs from \p Access's generic one, and LLVM's
/// opaque pointer types encode address space, so the two are different
/// types.)
void lowerSPIRVAccess(CallInst &Access, Value *Ptr, Type *ElementType) {
  IRBuilder<> Builder(&Access);
  Value *Elem = Builder.CreateGEP(ElementType, Ptr, Access.getArgOperand(1));

  User *AccessUser = *Access.user_begin();
  if (auto *LI = dyn_cast<LoadInst>(AccessUser))
    LI->setOperand(LoadInst::getPointerOperandIndex(), Elem);
  else
    cast<StoreInst>(AccessUser)
        ->setOperand(StoreInst::getPointerOperandIndex(), Elem);
  Access.eraseFromParent();
}

/// Rewrites a `llvm.dx.resource.load.level`/`.store.texture` call \p Access
/// into an ordinary, aligned load/store of a flat-addressed element of
/// \p Ptr: \p Access's coordinate operand (a vector for a multi-dimensional
/// texture, a scalar for a 1D one, per `packTextureVector` in OpRaising.cpp)
/// is decomposed back into its scalar components, and linearized against
/// \p Strides (one flat-addressing stride per coordinate beyond the first --
/// e.g. a `Texture2D`'s row pitch in texels, a `Texture3D`'s additional
/// slice pitch) the same way an ordinary row-major array index would be:
/// `coord0 + coord1*Strides[0] + coord2*Strides[1] + ...`. The mip level
/// operand `load.level` carries (always 0 for a compute shader's
/// `RWTexture*`/non-mipmapped `Texture*::Load`, which is what this pass is
/// reached for -- see `ResourceLoweringPass`'s class comment) is not
/// otherwise consulted: addressing a specific mip would need that level's
/// own (smaller) strides, which nothing yet supplies.
void lowerDXTextureAccess(CallInst &Access, Value *Ptr, Type *ElementType,
                          ArrayRef<Value *> Strides, Align Alignment) {
  IRBuilder<> Builder(&Access);
  Value *Coord = Access.getArgOperand(1);
  SmallVector<Value *, 3> Components;
  if (auto *VecTy = dyn_cast<FixedVectorType>(Coord->getType())) {
    for (unsigned I = 0, E = VecTy->getNumElements(); I != E; ++I)
      Components.push_back(
          Builder.CreateExtractElement(Coord, Builder.getInt32(I)));
  } else {
    Components.push_back(Coord);
  }

  Value *Index = Components[0];
  for (unsigned I = 1, E = Components.size(); I != E; ++I)
    Index = Builder.CreateAdd(
        Index, Builder.CreateMul(Components[I], Strides[I - 1]));
  Value *Elem = Builder.CreateGEP(ElementType, Ptr, Index);

  if (getIntrinsicID(&Access) == Intrinsic::dx_resource_store_texture) {
    Builder.CreateAlignedStore(Access.getArgOperand(2), Elem, Alignment);
  } else {
    Value *Loaded = Builder.CreateAlignedLoad(ElementType, Elem, Alignment);
    Access.replaceAllUsesWith(Loaded);
  }
  Access.eraseFromParent();
}

/// Rewrites a `llvm.dx.resource.load.cbufferrow.*` call \p Access's
/// per-field `extractvalue`s (its only supported uses, per
/// `hasOnlySupportedUses`) into ordinary, aligned loads of \p Ptr: a
/// cbuffer row is always 16 bytes (128 bits) regardless of how many
/// \p ElementType-sized fields it packs (see `raiseCBufferLoadLegacy`'s own
/// comment), so field \p N of row \p Access's index operand names is a
/// simple `ElementType`-strided load starting at that row's 16-byte-aligned
/// byte offset.
void lowerDXCBufferAccess(CallInst &Access, Value *Ptr, Type *ElementType,
                          Align Alignment) {
  IRBuilder<> Builder(&Access);
  Value *RowByteOffset =
      Builder.CreateMul(Access.getArgOperand(1), Builder.getInt32(16));
  Value *RowPtr =
      Builder.CreateGEP(Builder.getInt8Ty(), Ptr, RowByteOffset);

  for (User *U : llvm::make_early_inc_range(Access.users())) {
    auto *EV = cast<ExtractValueInst>(U);
    Builder.SetInsertPoint(EV);
    Value *FieldPtr = Builder.CreateGEP(
        ElementType, RowPtr, Builder.getInt32(EV->getIndices()[0]));
    Value *Loaded = Builder.CreateAlignedLoad(ElementType, FieldPtr, Alignment);
    EV->replaceAllUsesWith(Loaded);
    EV->eraseFromParent();
  }
  Access.eraseFromParent();
}

/// Rewrites every typed buffer/texture/cbuffer access through \p Handle into
/// ordinary loads/stores of \p Ptr (plus \p Strides, for a `dx.Texture`
/// binding of more than one coordinate dimension -- see
/// `Binding::NumAuxArgs`).
void lowerHandleAccesses(CallInst &Handle, const ResourceOps &Ops, Value *Ptr,
                         Type *ElementType, ArrayRef<Value *> Strides) {
  const DataLayout &DL = Handle.getModule()->getDataLayout();
  Align Alignment = DL.getABITypeAlign(ElementType);

  for (User *U : llvm::make_early_inc_range(Handle.users())) {
    auto *Access = cast<CallInst>(U);
    switch (Ops.Family) {
    case ResourceFamily::DX:
      lowerDXAccess(*Access, Ptr, ElementType, Alignment);
      break;
    case ResourceFamily::SPIRV:
      lowerSPIRVAccess(*Access, Ptr, ElementType);
      break;
    case ResourceFamily::DXTexture:
      lowerDXTextureAccess(*Access, Ptr, ElementType, Strides, Alignment);
      break;
    case ResourceFamily::DXCBuffer:
      lowerDXCBufferAccess(*Access, Ptr, ElementType, Alignment);
      break;
    }
  }
  Handle.eraseFromParent();
}

/// Lowers every resource binding \p F uses, returning the rewritten function
/// (a new one, since its signature grows), or nullptr if \p F has no
/// bindings or uses one this pass cannot model.
Function *lowerFunctionResources(Function &F) {
  if (F.isDeclaration() || !feme::isShaderEntryPoint(F))
    return nullptr;

  std::optional<SmallVector<Binding, 4>> Bindings = collectBindings(F);
  if (!Bindings || Bindings->empty())
    return nullptr;

  Function *NewF = addBindingArguments(F, *Bindings);

  unsigned ArgIndex = NewF->arg_size();
  for (const Binding &B : *Bindings)
    ArgIndex -= 1 + B.NumAuxArgs;
  for (const Binding &B : *Bindings) {
    Value *Ptr = NewF->arg_begin() + ArgIndex++;
    SmallVector<Value *, 2> Strides;
    for (unsigned I = 0; I != B.NumAuxArgs; ++I)
      Strides.push_back(NewF->arg_begin() + ArgIndex++);
    for (CallInst *Handle : B.Handles)
      lowerHandleAccesses(*Handle, *B.Ops, Ptr, B.ElementType, Strides);
  }
  return NewF;
}

} // namespace

PreservedAnalyses ResourceLoweringPass::run(Module &M,
                                            ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    Changed |= lowerFunctionResources(F) != nullptr;

  // The handle-creation intrinsic's declaration is left behind once its last
  // caller is gone; AMDGPU cannot select a call to it, so an unused
  // declaration surviving here would be a silent landmine.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        (F.getName().starts_with("llvm.dx.resource.") ||
         F.getName().starts_with("llvm.spv.resource.")))
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
