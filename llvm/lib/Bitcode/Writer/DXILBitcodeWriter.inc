//===- Bitcode/Writer/DXILBitcodeWriter.inc - DXIL Bitcode Writer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bitcode writer implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ErrorHandling.h"
class DXILBitcodeWriter : public ModuleBitcodeWriterBase {

  enum class dxilc {
    // FUNCTION_BLOCK abbrev id's.
    FUNCTION_INST_LOAD_ABBREV = bitc::FIRST_APPLICATION_ABBREV,
    FUNCTION_INST_BINOP_ABBREV,
    FUNCTION_INST_BINOP_FLAGS_ABBREV,
    FUNCTION_INST_CAST_ABBREV,
    FUNCTION_INST_RET_VOID_ABBREV,
    FUNCTION_INST_RET_VAL_ABBREV,
    FUNCTION_INST_UNREACHABLE_ABBREV,
    FUNCTION_INST_GEP_ABBREV,
  };

  /// Pointer to the buffer allocated by caller for bitcode writing.
  const SmallVectorImpl<char> &Buffer;

  /// True if a module hash record should be written.
  bool GenerateHash;

  /// If non-null, when GenerateHash is true, the resulting hash is written
  /// into ModHash.
  ModuleHash *ModHash;

  SHA1 Hasher;

  /// The start bit of the identification block.
  uint64_t BitcodeStartBit;

public:
  /// Constructs a ModuleBitcodeWriter object for the given Module,
  /// writing to the provided \p Buffer.
  DXILBitcodeWriter(const Module &M, SmallVectorImpl<char> &Buffer,
                    StringTableBuilder &StrtabBuilder, BitstreamWriter &Stream,
                    bool ShouldPreserveUseListOrder,
                    const ModuleSummaryIndex *Index, bool GenerateHash,
                    ModuleHash *ModHash = nullptr)
      : ModuleBitcodeWriterBase(M, StrtabBuilder, Stream,
                                ShouldPreserveUseListOrder, Index),
        Buffer(Buffer), GenerateHash(GenerateHash), ModHash(ModHash),
        BitcodeStartBit(Stream.GetCurrentBitNo()) {}

  /// Emit the current module to the bitstream.
  void write();

private:
  void writeModuleVersion();

  uint64_t bitcodeStartBit() { return BitcodeStartBit; }

  size_t addToStrtab(StringRef Str);

  unsigned createDILocationAbbrev();
  unsigned createGenericDINodeAbbrev();

  void writeAttributeGroupTable();
  void writeAttributeTable();
  void writeTypeTable();
  void writeComdats();
  void writeValueSymbolTableForwardDecl();
  void writeModuleInfo();
  void writeValueAsMetadata(const ValueAsMetadata *MD,
                            SmallVectorImpl<uint64_t> &Record);
  void writeMDTuple(const MDTuple *N, SmallVectorImpl<uint64_t> &Record,
                    unsigned Abbrev);
  void writeDILocation(const DILocation *N, SmallVectorImpl<uint64_t> &Record,
                       unsigned &Abbrev);
  void writeGenericDINode(const GenericDINode *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned &Abbrev) {
    llvm_unreachable("DXIL cannot contain GenericDI Nodes");
  }
  void writeDISubrange(const DISubrange *N, SmallVectorImpl<uint64_t> &Record,
                       unsigned Abbrev);
  void writeDIGenericSubrange(const DIGenericSubrange *N,
                              SmallVectorImpl<uint64_t> &Record,
                              unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DIGenericSubrange Nodes");
  }
  void writeDIEnumerator(const DIEnumerator *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIBasicType(const DIBasicType *N, SmallVectorImpl<uint64_t> &Record,
                        unsigned Abbrev);
  void writeDIStringType(const DIStringType *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DIStringType Nodes");
  }
  void writeDIDerivedType(const DIDerivedType *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDICompositeType(const DICompositeType *N,
                            SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDISubroutineType(const DISubroutineType *N,
                             SmallVectorImpl<uint64_t> &Record,
                             unsigned Abbrev);
  void writeDIFile(const DIFile *N, SmallVectorImpl<uint64_t> &Record,
                   unsigned Abbrev);
  void writeDICompileUnit(const DICompileUnit *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDISubprogram(const DISubprogram *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDILexicalBlock(const DILexicalBlock *N,
                           SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDILexicalBlockFile(const DILexicalBlockFile *N,
                               SmallVectorImpl<uint64_t> &Record,
                               unsigned Abbrev);
  void writeDICommonBlock(const DICommonBlock *N,
                          SmallVectorImpl<uint64_t> &Record, unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DICommonBlock Nodes");
  }
  void writeDINamespace(const DINamespace *N, SmallVectorImpl<uint64_t> &Record,
                        unsigned Abbrev);
  void writeDIMacro(const DIMacro *N, SmallVectorImpl<uint64_t> &Record,
                    unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DIMacro Nodes");
  }
  void writeDIMacroFile(const DIMacroFile *N, SmallVectorImpl<uint64_t> &Record,
                        unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DIMacroFile Nodes");
  }
  void writeDIArgList(const DIArgList *N, SmallVectorImpl<uint64_t> &Record,
                      unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DIArgList Nodes");
  }
  void writeDIModule(const DIModule *N, SmallVectorImpl<uint64_t> &Record,
                     unsigned Abbrev);
  void writeDITemplateTypeParameter(const DITemplateTypeParameter *N,
                                    SmallVectorImpl<uint64_t> &Record,
                                    unsigned Abbrev);
  void writeDITemplateValueParameter(const DITemplateValueParameter *N,
                                     SmallVectorImpl<uint64_t> &Record,
                                     unsigned Abbrev);
  void writeDIGlobalVariable(const DIGlobalVariable *N,
                             SmallVectorImpl<uint64_t> &Record,
                             unsigned Abbrev);
  void writeDILocalVariable(const DILocalVariable *N,
                            SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDILabel(const DILabel *N, SmallVectorImpl<uint64_t> &Record,
                    unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain DILabel Nodes");
  }
  void writeDIExpression(const DIExpression *N,
                         SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIGlobalVariableExpression(const DIGlobalVariableExpression *N,
                                       SmallVectorImpl<uint64_t> &Record,
                                       unsigned Abbrev) {
    llvm_unreachable("DXIL cannot contain GlobalVariableExpression Nodes");
  }
  void writeDIObjCProperty(const DIObjCProperty *N,
                           SmallVectorImpl<uint64_t> &Record, unsigned Abbrev);
  void writeDIImportedEntity(const DIImportedEntity *N,
                             SmallVectorImpl<uint64_t> &Record,
                             unsigned Abbrev);
  unsigned createNamedMetadataAbbrev();
  void writeNamedMetadata(SmallVectorImpl<uint64_t> &Record);
  unsigned createMetadataStringsAbbrev();
  void writeMetadataStrings(ArrayRef<const Metadata *> Strings,
                            SmallVectorImpl<uint64_t> &Record);
  void writeMetadataRecords(ArrayRef<const Metadata *> MDs,
                            SmallVectorImpl<uint64_t> &Record,
                            std::vector<unsigned> *MDAbbrevs = nullptr,
                            std::vector<uint64_t> *IndexPos = nullptr);
  void writeModuleMetadata();
  void writeFunctionMetadata(const Function &F);
  void writeFunctionMetadataAttachment(const Function &F);
  void pushGlobalMetadataAttachment(SmallVectorImpl<uint64_t> &Record,
                                    const GlobalObject &GO);
  void writeModuleMetadataKinds();
  void writeOperandBundleTags();
  void writeSyncScopeNames();
  void writeConstants(unsigned FirstVal, unsigned LastVal, bool isGlobal);
  void writeModuleConstants();
  bool pushValueAndType(const Value *V, unsigned InstID,
                        SmallVectorImpl<unsigned> &Vals);
  void writeOperandBundles(const CallBase &CB, unsigned InstID);
  void pushValue(const Value *V, unsigned InstID,
                 SmallVectorImpl<unsigned> &Vals);
  void pushValueSigned(const Value *V, unsigned InstID,
                       SmallVectorImpl<uint64_t> &Vals);
  void writeInstruction(const Instruction &I, unsigned InstID,
                        SmallVectorImpl<unsigned> &Vals);
  void writeFunctionLevelValueSymbolTable(const ValueSymbolTable &VST);
  void writeGlobalValueSymbolTable(
      DenseMap<const Function *, uint64_t> &FunctionToBitcodeIndex);
  void writeUseList(UseListOrder &&Order);
  void writeUseListBlock(const Function *F);
  void writeFunction(const Function &F);
  void writeBlockInfo();
  void writeModuleHash(size_t BlockStartPos);

  unsigned getEncodedSyncScopeID(SyncScope::ID SSID) { return unsigned(SSID); }

  unsigned getEncodedAlign(MaybeAlign Alignment) { return encode(Alignment); }
};

void DXILBitcodeWriter::writeAttributeGroupTable() {
  const std::vector<ValueEnumerator::IndexAndAttrSet> &AttrGrps =
      VE.getAttributeGroups();
  if (AttrGrps.empty())
    return;

  Stream.EnterSubblock(bitc::PARAMATTR_GROUP_BLOCK_ID, 3);

  SmallVector<uint64_t, 64> Record;
  for (ValueEnumerator::IndexAndAttrSet Pair : AttrGrps) {
    unsigned AttrListIndex = Pair.first;
    AttributeSet AS = Pair.second;
    Record.push_back(VE.getAttributeGroupID(Pair));
    Record.push_back(AttrListIndex);

    for (Attribute Attr : AS) {
      if (Attr.isEnumAttribute()) {
        uint64_t Val = getAttrKindEncoding(Attr.getKindAsEnum());
        assert(Val <= bitc::ATTR_KIND_ARGMEMONLY &&
               "DXIL does not support attributes above ATTR_KIND_ARGMEMONLY");
        Record.push_back(0);
        Record.push_back(Val);
      } else if (Attr.isIntAttribute()) {
        uint64_t Val = getAttrKindEncoding(Attr.getKindAsEnum());
        assert(Val <= bitc::ATTR_KIND_ARGMEMONLY &&
               "DXIL does not support attributes above ATTR_KIND_ARGMEMONLY");
        Record.push_back(1);
        Record.push_back(Val);
        Record.push_back(Attr.getValueAsInt());
      } else {
        StringRef Kind = Attr.getKindAsString();
        StringRef Val = Attr.getValueAsString();

        Record.push_back(Val.empty() ? 3 : 4);
        Record.append(Kind.begin(), Kind.end());
        Record.push_back(0);
        if (!Val.empty()) {
          Record.append(Val.begin(), Val.end());
          Record.push_back(0);
        }
      }
    }

    Stream.EmitRecord(bitc::PARAMATTR_GRP_CODE_ENTRY, Record);
    Record.clear();
  }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeAttributeTable() {
  const std::vector<AttributeList> &Attrs = VE.getAttributeLists();
  if (Attrs.empty())
    return;

  Stream.EnterSubblock(bitc::PARAMATTR_BLOCK_ID, 3);

  SmallVector<uint64_t, 64> Record;
  for (unsigned i = 0, e = Attrs.size(); i != e; ++i) {
    AttributeList AL = Attrs[i];
    for (unsigned i : AL.indexes()) {
      AttributeSet AS = AL.getAttributes(i);
      if (AS.hasAttributes())
        Record.push_back(VE.getAttributeGroupID({i, AS}));
    }

    Stream.EmitRecord(bitc::PARAMATTR_CODE_ENTRY, Record);
    Record.clear();
  }

  Stream.ExitBlock();
}

/// WriteTypeTable - Write out the type table for a module.
void DXILBitcodeWriter::writeTypeTable() {
  const ValueEnumerator::TypeList &TypeList = VE.getTypes();

  Stream.EnterSubblock(bitc::TYPE_BLOCK_ID_NEW, 4 /*count from # abbrevs */);
  SmallVector<uint64_t, 64> TypeVals;

  uint64_t NumBits = VE.computeBitsRequiredForTypeIndicies();

  // Abbrev for TYPE_CODE_POINTER.
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_POINTER));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  Abbv->Add(BitCodeAbbrevOp(0)); // Addrspace = 0
  unsigned PtrAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_FUNCTION.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_FUNCTION));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isvararg
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned FunctionAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_STRUCT_ANON.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_STRUCT_ANON));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ispacked
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned StructAnonAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_STRUCT_NAME.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_STRUCT_NAME));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
  unsigned StructNameAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_STRUCT_NAMED.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_STRUCT_NAMED));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ispacked
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned StructNamedAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Abbrev for TYPE_CODE_ARRAY.
  Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::TYPE_CODE_ARRAY));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // size
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, NumBits));
  unsigned ArrayAbbrev = Stream.EmitAbbrev(std::move(Abbv));

  // Emit an entry count so the reader can reserve space.
  TypeVals.push_back(TypeList.size());
  Stream.EmitRecord(bitc::TYPE_CODE_NUMENTRY, TypeVals);
  TypeVals.clear();

  // Loop over all of the types, emitting each in turn.
  for (unsigned i = 0, e = TypeList.size(); i != e; ++i) {
    Type *T = TypeList[i];
    int AbbrevToUse = 0;
    unsigned Code = 0;

    switch (T->getTypeID()) {
    case Type::BFloatTyID:
    case Type::X86_AMXTyID:
    case Type::TokenTyID:
      llvm_unreachable("These should never be used!!!");
      break;
    case Type::VoidTyID:
      Code = bitc::TYPE_CODE_VOID;
      break;
    case Type::HalfTyID:
      Code = bitc::TYPE_CODE_HALF;
      break;
    case Type::FloatTyID:
      Code = bitc::TYPE_CODE_FLOAT;
      break;
    case Type::DoubleTyID:
      Code = bitc::TYPE_CODE_DOUBLE;
      break;
    case Type::X86_FP80TyID:
      Code = bitc::TYPE_CODE_X86_FP80;
      break;
    case Type::FP128TyID:
      Code = bitc::TYPE_CODE_FP128;
      break;
    case Type::PPC_FP128TyID:
      Code = bitc::TYPE_CODE_PPC_FP128;
      break;
    case Type::LabelTyID:
      Code = bitc::TYPE_CODE_LABEL;
      break;
    case Type::MetadataTyID:
      Code = bitc::TYPE_CODE_METADATA;
      break;
    case Type::X86_MMXTyID:
      Code = bitc::TYPE_CODE_X86_MMX;
      break;
    case Type::IntegerTyID:
      // INTEGER: [width]
      Code = bitc::TYPE_CODE_INTEGER;
      TypeVals.push_back(cast<IntegerType>(T)->getBitWidth());
      break;
    case Type::PointerTyID: {
      PointerType *PTy = cast<PointerType>(T);
      // POINTER: [pointee type, address space]
      Code = bitc::TYPE_CODE_POINTER;
      TypeVals.push_back(VE.getTypeID(PTy->getNonOpaquePointerElementType()));
      unsigned AddressSpace = PTy->getAddressSpace();
      TypeVals.push_back(AddressSpace);
      if (AddressSpace == 0)
        AbbrevToUse = PtrAbbrev;
      break;
    }
    case Type::FunctionTyID: {
      FunctionType *FT = cast<FunctionType>(T);
      // FUNCTION: [isvararg, retty, paramty x N]
      Code = bitc::TYPE_CODE_FUNCTION;
      TypeVals.push_back(FT->isVarArg());
      TypeVals.push_back(VE.getTypeID(FT->getReturnType()));
      for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i)
        TypeVals.push_back(VE.getTypeID(FT->getParamType(i)));
      AbbrevToUse = FunctionAbbrev;
      break;
    }
    case Type::StructTyID: {
      StructType *ST = cast<StructType>(T);
      // STRUCT: [ispacked, eltty x N]
      TypeVals.push_back(ST->isPacked());
      // Output all of the element types.
      for (StructType::element_iterator I = ST->element_begin(),
                                        E = ST->element_end();
           I != E; ++I)
        TypeVals.push_back(VE.getTypeID(*I));

      if (ST->isLiteral()) {
        Code = bitc::TYPE_CODE_STRUCT_ANON;
        AbbrevToUse = StructAnonAbbrev;
      } else {
        if (ST->isOpaque()) {
          Code = bitc::TYPE_CODE_OPAQUE;
        } else {
          Code = bitc::TYPE_CODE_STRUCT_NAMED;
          AbbrevToUse = StructNamedAbbrev;
        }

        // Emit the name if it is present.
        if (!ST->getName().empty())
          writeStringRecord(Stream, bitc::TYPE_CODE_STRUCT_NAME, ST->getName(),
                            StructNameAbbrev);
      }
      break;
    }
    case Type::ArrayTyID: {
      ArrayType *AT = cast<ArrayType>(T);
      // ARRAY: [numelts, eltty]
      Code = bitc::TYPE_CODE_ARRAY;
      TypeVals.push_back(AT->getNumElements());
      TypeVals.push_back(VE.getTypeID(AT->getElementType()));
      AbbrevToUse = ArrayAbbrev;
      break;
    }
    case Type::FixedVectorTyID:
    case Type::ScalableVectorTyID: {
      VectorType *VT = cast<VectorType>(T);
      // VECTOR [numelts, eltty]
      Code = bitc::TYPE_CODE_VECTOR;
      TypeVals.push_back(VT->getElementCount().getKnownMinValue());
      TypeVals.push_back(VE.getTypeID(VT->getElementType()));
      break;
    }
    }

    // Emit the finished record.
    Stream.EmitRecord(Code, TypeVals, AbbrevToUse);
    TypeVals.clear();
  }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeComdats() {
  SmallVector<uint16_t, 64> Vals;
  for (const Comdat *C : VE.getComdats()) {
    // COMDAT: [selection_kind, name]
    Vals.push_back(getEncodedComdatSelectionKind(*C));
    size_t Size = C->getName().size();
    assert(isUInt<16>(Size));
    Vals.push_back(Size);
    for (char Chr : C->getName())
      Vals.push_back((unsigned char)Chr);
    Stream.EmitRecord(bitc::MODULE_CODE_COMDAT, Vals, /*AbbrevToUse=*/0);
    Vals.clear();
  }
}

void DXILBitcodeWriter::writeValueSymbolTableForwardDecl() {}

/// Emit top-level description of module, including target triple, inline asm,
/// descriptors for global variables, and function prototype info.
/// Returns the bit offset to backpatch with the location of the real VST.
void DXILBitcodeWriter::writeModuleInfo() {
  // Emit various pieces of data attached to a module.
  if (!M.getTargetTriple().empty())
    writeStringRecord(Stream, bitc::MODULE_CODE_TRIPLE, M.getTargetTriple(),
                      0 /*TODO*/);
  const std::string &DL = M.getDataLayoutStr();
  if (!DL.empty())
    writeStringRecord(Stream, bitc::MODULE_CODE_DATALAYOUT, DL, 0 /*TODO*/);
  if (!M.getModuleInlineAsm().empty())
    writeStringRecord(Stream, bitc::MODULE_CODE_ASM, M.getModuleInlineAsm(),
                      0 /*TODO*/);

  // Emit information about sections and GC, computing how many there are. Also
  // compute the maximum alignment value.
  std::map<std::string, unsigned> SectionMap;
  std::map<std::string, unsigned> GCMap;
  MaybeAlign MaxAlignment;
  unsigned MaxGlobalType = 0;
  const auto UpdateMaxAlignment = [&MaxAlignment](const MaybeAlign A) {
    if (A)
      MaxAlignment = !MaxAlignment ? *A : std::max(*MaxAlignment, *A);
  };
  for (const GlobalVariable &GV : M.globals()) {
    UpdateMaxAlignment(GV.getAlign());
    MaxGlobalType = std::max(MaxGlobalType, VE.getTypeID(GV.getValueType()));
    if (GV.hasSection()) {
      // Give section names unique ID's.
      unsigned &Entry = SectionMap[std::string(GV.getSection())];
      if (!Entry) {
        writeStringRecord(Stream, bitc::MODULE_CODE_SECTIONNAME,
                          GV.getSection(), 0 /*TODO*/);
        Entry = SectionMap.size();
      }
    }
  }
  for (const Function &F : M) {
    UpdateMaxAlignment(F.getAlign());
    if (F.hasSection()) {
      // Give section names unique ID's.
      unsigned &Entry = SectionMap[std::string(F.getSection())];
      if (!Entry) {
        writeStringRecord(Stream, bitc::MODULE_CODE_SECTIONNAME, F.getSection(),
                          0 /*TODO*/);
        Entry = SectionMap.size();
      }
    }
    if (F.hasGC()) {
      // Same for GC names.
      unsigned &Entry = GCMap[F.getGC()];
      if (!Entry) {
        writeStringRecord(Stream, bitc::MODULE_CODE_GCNAME, F.getGC(),
                          0 /*TODO*/);
        Entry = GCMap.size();
      }
    }
  }

  // Emit abbrev for globals, now that we know # sections and max alignment.
  unsigned SimpleGVarAbbrev = 0;
  if (!M.global_empty()) {
    // Add an abbrev for common globals with no visibility or thread localness.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::MODULE_CODE_GLOBALVAR));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                              Log2_32_Ceil(MaxGlobalType + 1)));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // AddrSpace << 2
                                                           //| explicitType << 1
                                                           //| constant
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Initializer.
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 5)); // Linkage.
    if (MaxAlignment == 0)                                 // Alignment.
      Abbv->Add(BitCodeAbbrevOp(0));
    else {
      unsigned MaxEncAlignment = getEncodedAlign(MaxAlignment);
      Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                                Log2_32_Ceil(MaxEncAlignment + 1)));
    }
    if (SectionMap.empty()) // Section.
      Abbv->Add(BitCodeAbbrevOp(0));
    else
      Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                                Log2_32_Ceil(SectionMap.size() + 1)));
    // Don't bother emitting vis + thread local.
    SimpleGVarAbbrev = Stream.EmitAbbrev(std::move(Abbv));
  }

  // Emit the global variable information.
  SmallVector<unsigned, 64> Vals;
  for (const GlobalVariable &GV : M.globals()) {
    unsigned AbbrevToUse = 0;

    // GLOBALVAR: [type, isconst, initid,
    //             linkage, alignment, section, visibility, threadlocal,
    //             unnamed_addr, externally_initialized, dllstorageclass,
    //             comdat]
    Vals.push_back(VE.getTypeID(GV.getValueType()));
    Vals.push_back(
        GV.getType()->getAddressSpace() << 2 | 2 |
        (GV.isConstant() ? 1 : 0)); // HLSL Change - bitwise | was used with
                                    // unsigned int and bool
    Vals.push_back(
        GV.isDeclaration() ? 0 : (VE.getValueID(GV.getInitializer()) + 1));
    Vals.push_back(getEncodedLinkage(GV));
    Vals.push_back(getEncodedAlign(GV.getAlign()));
    Vals.push_back(GV.hasSection() ? SectionMap[std::string(GV.getSection())]
                                   : 0);
    if (GV.isThreadLocal() ||
        GV.getVisibility() != GlobalValue::DefaultVisibility ||
        GV.getUnnamedAddr() != GlobalValue::UnnamedAddr::None ||
        GV.isExternallyInitialized() ||
        GV.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
        GV.hasComdat()) {
      Vals.push_back(getEncodedVisibility(GV));
      Vals.push_back(getEncodedThreadLocalMode(GV));
      Vals.push_back(GV.getUnnamedAddr() != GlobalValue::UnnamedAddr::None);
      Vals.push_back(GV.isExternallyInitialized());
      Vals.push_back(getEncodedDLLStorageClass(GV));
      Vals.push_back(GV.hasComdat() ? VE.getComdatID(GV.getComdat()) : 0);
    } else {
      AbbrevToUse = SimpleGVarAbbrev;
    }

    Stream.EmitRecord(bitc::MODULE_CODE_GLOBALVAR, Vals, AbbrevToUse);
    Vals.clear();
  }

  // Emit the function proto information.
  for (const Function &F : M) {
    // FUNCTION:  [type, callingconv, isproto, linkage, paramattrs, alignment,
    //             section, visibility, gc, unnamed_addr, prologuedata,
    //             dllstorageclass, comdat, prefixdata, personalityfn]
    Vals.push_back(VE.getTypeID(F.getFunctionType()));
    Vals.push_back(F.getCallingConv());
    Vals.push_back(F.isDeclaration());
    Vals.push_back(getEncodedLinkage(F));
    Vals.push_back(VE.getAttributeListID(F.getAttributes()));
    Vals.push_back(getEncodedAlign(F.getAlign()));
    Vals.push_back(F.hasSection() ? SectionMap[std::string(F.getSection())]
                                  : 0);
    Vals.push_back(getEncodedVisibility(F));
    Vals.push_back(F.hasGC() ? GCMap[F.getGC()] : 0);
    Vals.push_back(F.getUnnamedAddr() != GlobalValue::UnnamedAddr::None);
    Vals.push_back(
        F.hasPrologueData() ? (VE.getValueID(F.getPrologueData()) + 1) : 0);
    Vals.push_back(getEncodedDLLStorageClass(F));
    Vals.push_back(F.hasComdat() ? VE.getComdatID(F.getComdat()) : 0);
    Vals.push_back(F.hasPrefixData() ? (VE.getValueID(F.getPrefixData()) + 1)
                                     : 0);
    Vals.push_back(
        F.hasPersonalityFn() ? (VE.getValueID(F.getPersonalityFn()) + 1) : 0);

    unsigned AbbrevToUse = 0;
    Stream.EmitRecord(bitc::MODULE_CODE_FUNCTION, Vals, AbbrevToUse);
    Vals.clear();
  }

  // Emit the alias information.
  for (const GlobalAlias &A : M.aliases()) {
    // ALIAS: [alias type, aliasee val#, linkage, visibility]
    Vals.push_back(VE.getTypeID(A.getValueType()));
    Vals.push_back(VE.getValueID(A.getAliasee()));
    Vals.push_back(getEncodedLinkage(A));
    Vals.push_back(getEncodedVisibility(A));
    Vals.push_back(getEncodedDLLStorageClass(A));
    Vals.push_back(getEncodedThreadLocalMode(A));
    Vals.push_back(A.getUnnamedAddr() != GlobalValue::UnnamedAddr::None);
    unsigned AbbrevToUse = 0;
    Stream.EmitRecord(bitc::MODULE_CODE_ALIAS_OLD, Vals, AbbrevToUse);
    Vals.clear();
  }
}

void DXILBitcodeWriter::writeValueAsMetadata(
    const ValueAsMetadata *MD, SmallVectorImpl<uint64_t> &Record) {
  // Mimic an MDNode with a value as one operand.
  Value *V = MD->getValue();
  Record.push_back(VE.getTypeID(V->getType()));
  Record.push_back(VE.getValueID(V));
  Stream.EmitRecord(bitc::METADATA_VALUE, Record, 0);
  Record.clear();
}

void DXILBitcodeWriter::writeMDTuple(const MDTuple *N,
                                     SmallVectorImpl<uint64_t> &Record,
                                     unsigned Abbrev) {
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    Metadata *MD = N->getOperand(i);
    assert(!(MD && isa<LocalAsMetadata>(MD)) &&
           "Unexpected function-local metadata");
    Record.push_back(VE.getMetadataOrNullID(MD));
  }
  Stream.EmitRecord(N->isDistinct() ? bitc::METADATA_DISTINCT_NODE
                                    : bitc::METADATA_NODE,
                    Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDILocation(const DILocation *N,
                                        SmallVectorImpl<uint64_t> &Record,
                                        unsigned &Abbrev) {
  if (!Abbrev)
    Abbrev = createDILocationAbbrev();
  Record.push_back(N->isDistinct());
  Record.push_back(N->getLine());
  Record.push_back(N->getColumn());
  Record.push_back(VE.getMetadataID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getInlinedAt()));

  Stream.EmitRecord(bitc::METADATA_LOCATION, Record, Abbrev);
  Record.clear();
}

static uint64_t rotateSign(APInt Val) {
  int64_t I = Val.getSExtValue();
  uint64_t U = I;
  return I < 0 ? ~(U << 1) : U << 1;
}

static uint64_t rotateSign(DISubrange::BoundType Val) {
  return rotateSign(Val.get<ConstantInt *>()->getValue());
}

void DXILBitcodeWriter::writeDISubrange(const DISubrange *N,
                                        SmallVectorImpl<uint64_t> &Record,
                                        unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(
      N->getCount().get<ConstantInt *>()->getValue().getSExtValue());
  Record.push_back(rotateSign(N->getLowerBound()));

  Stream.EmitRecord(bitc::METADATA_SUBRANGE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIEnumerator(const DIEnumerator *N,
                                          SmallVectorImpl<uint64_t> &Record,
                                          unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(rotateSign(N->getValue()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));

  Stream.EmitRecord(bitc::METADATA_ENUMERATOR, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIBasicType(const DIBasicType *N,
                                         SmallVectorImpl<uint64_t> &Record,
                                         unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getEncoding());

  Stream.EmitRecord(bitc::METADATA_BASIC_TYPE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIDerivedType(const DIDerivedType *N,
                                           SmallVectorImpl<uint64_t> &Record,
                                           unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getBaseType()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getOffsetInBits());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getExtraData()));

  Stream.EmitRecord(bitc::METADATA_DERIVED_TYPE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDICompositeType(const DICompositeType *N,
                                             SmallVectorImpl<uint64_t> &Record,
                                             unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getBaseType()));
  Record.push_back(N->getSizeInBits());
  Record.push_back(N->getAlignInBits());
  Record.push_back(N->getOffsetInBits());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getElements().get()));
  Record.push_back(N->getRuntimeLang());
  Record.push_back(VE.getMetadataOrNullID(N->getVTableHolder()));
  Record.push_back(VE.getMetadataOrNullID(N->getTemplateParams().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawIdentifier()));

  Stream.EmitRecord(bitc::METADATA_COMPOSITE_TYPE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDISubroutineType(const DISubroutineType *N,
                                              SmallVectorImpl<uint64_t> &Record,
                                              unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getFlags());
  Record.push_back(VE.getMetadataOrNullID(N->getTypeArray().get()));

  Stream.EmitRecord(bitc::METADATA_SUBROUTINE_TYPE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIFile(const DIFile *N,
                                    SmallVectorImpl<uint64_t> &Record,
                                    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getRawFilename()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawDirectory()));

  Stream.EmitRecord(bitc::METADATA_FILE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDICompileUnit(const DICompileUnit *N,
                                           SmallVectorImpl<uint64_t> &Record,
                                           unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getSourceLanguage());
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawProducer()));
  Record.push_back(N->isOptimized());
  Record.push_back(VE.getMetadataOrNullID(N->getRawFlags()));
  Record.push_back(N->getRuntimeVersion());
  Record.push_back(VE.getMetadataOrNullID(N->getRawSplitDebugFilename()));
  Record.push_back(N->getEmissionKind());
  Record.push_back(VE.getMetadataOrNullID(N->getEnumTypes().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getRetainedTypes().get()));
  Record.push_back(/* subprograms */ 0);
  Record.push_back(VE.getMetadataOrNullID(N->getGlobalVariables().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getImportedEntities().get()));
  Record.push_back(N->getDWOId());

  Stream.EmitRecord(bitc::METADATA_COMPILE_UNIT, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDISubprogram(const DISubprogram *N,
                                          SmallVectorImpl<uint64_t> &Record,
                                          unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawLinkageName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->isLocalToUnit());
  Record.push_back(N->isDefinition());
  Record.push_back(N->getScopeLine());
  Record.push_back(VE.getMetadataOrNullID(N->getContainingType()));
  Record.push_back(N->getVirtuality());
  Record.push_back(N->getVirtualIndex());
  Record.push_back(N->getFlags());
  Record.push_back(N->isOptimized());
  Record.push_back(VE.getMetadataOrNullID(N->getRawUnit()));
  Record.push_back(VE.getMetadataOrNullID(N->getTemplateParams().get()));
  Record.push_back(VE.getMetadataOrNullID(N->getDeclaration()));
  Record.push_back(VE.getMetadataOrNullID(N->getRetainedNodes().get()));

  Stream.EmitRecord(bitc::METADATA_SUBPROGRAM, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDILexicalBlock(const DILexicalBlock *N,
                                            SmallVectorImpl<uint64_t> &Record,
                                            unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(N->getColumn());

  Stream.EmitRecord(bitc::METADATA_LEXICAL_BLOCK, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDILexicalBlockFile(
    const DILexicalBlockFile *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getDiscriminator());

  Stream.EmitRecord(bitc::METADATA_LEXICAL_BLOCK_FILE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDINamespace(const DINamespace *N,
                                         SmallVectorImpl<uint64_t> &Record,
                                         unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(/* line number */ 0);

  Stream.EmitRecord(bitc::METADATA_NAMESPACE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIModule(const DIModule *N,
                                      SmallVectorImpl<uint64_t> &Record,
                                      unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  for (auto &I : N->operands())
    Record.push_back(VE.getMetadataOrNullID(I));

  Stream.EmitRecord(bitc::METADATA_MODULE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDITemplateTypeParameter(
    const DITemplateTypeParameter *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getType()));

  Stream.EmitRecord(bitc::METADATA_TEMPLATE_TYPE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDITemplateValueParameter(
    const DITemplateValueParameter *N, SmallVectorImpl<uint64_t> &Record,
    unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(VE.getMetadataOrNullID(N->getValue()));

  Stream.EmitRecord(bitc::METADATA_TEMPLATE_VALUE, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIGlobalVariable(const DIGlobalVariable *N,
                                              SmallVectorImpl<uint64_t> &Record,
                                              unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawLinkageName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->isLocalToUnit());
  Record.push_back(N->isDefinition());
  Record.push_back(/* N->getRawVariable() */ 0);
  Record.push_back(VE.getMetadataOrNullID(N->getStaticDataMemberDeclaration()));

  Stream.EmitRecord(bitc::METADATA_GLOBAL_VAR, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDILocalVariable(const DILocalVariable *N,
                                             SmallVectorImpl<uint64_t> &Record,
                                             unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));
  Record.push_back(VE.getMetadataOrNullID(N->getFile()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getType()));
  Record.push_back(N->getArg());
  Record.push_back(N->getFlags());

  Stream.EmitRecord(bitc::METADATA_LOCAL_VAR, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIExpression(const DIExpression *N,
                                          SmallVectorImpl<uint64_t> &Record,
                                          unsigned Abbrev) {
  Record.reserve(N->getElements().size() + 1);

  Record.push_back(N->isDistinct());
  Record.append(N->elements_begin(), N->elements_end());

  Stream.EmitRecord(bitc::METADATA_EXPRESSION, Record, Abbrev);
  Record.clear();
}

void DXILBitcodeWriter::writeDIObjCProperty(const DIObjCProperty *N,
                                            SmallVectorImpl<uint64_t> &Record,
                                            unsigned Abbrev) {
  llvm_unreachable("DXIL does not support objc!!!");
}

void DXILBitcodeWriter::writeDIImportedEntity(const DIImportedEntity *N,
                                              SmallVectorImpl<uint64_t> &Record,
                                              unsigned Abbrev) {
  Record.push_back(N->isDistinct());
  Record.push_back(N->getTag());
  Record.push_back(VE.getMetadataOrNullID(N->getScope()));
  Record.push_back(VE.getMetadataOrNullID(N->getEntity()));
  Record.push_back(N->getLine());
  Record.push_back(VE.getMetadataOrNullID(N->getRawName()));

  Stream.EmitRecord(bitc::METADATA_IMPORTED_ENTITY, Record, Abbrev);
  Record.clear();
}

unsigned DXILBitcodeWriter::createDILocationAbbrev() {
  // Abbrev for METADATA_LOCATION.
  //
  // Assume the column is usually under 128, and always output the inlined-at
  // location (it's never more expensive than building an array size 1).
  std::shared_ptr<BitCodeAbbrev> Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_LOCATION));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  return Stream.EmitAbbrev(std::move(Abbv));
}

unsigned DXILBitcodeWriter::createGenericDINodeAbbrev() {
  // Abbrev for METADATA_GENERIC_DEBUG.
  //
  // Assume the column is usually under 128, and always output the inlined-at
  // location (it's never more expensive than building an array size 1).
  std::shared_ptr<BitCodeAbbrev> Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_GENERIC_DEBUG));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  return Stream.EmitAbbrev(std::move(Abbv));
}

void DXILBitcodeWriter::writeMetadataRecords(ArrayRef<const Metadata *> MDs,
                                             SmallVectorImpl<uint64_t> &Record,
                                             std::vector<unsigned> *MDAbbrevs,
                                             std::vector<uint64_t> *IndexPos) {
  if (MDs.empty())
    return;

    // Initialize MDNode abbreviations.
#define HANDLE_MDNODE_LEAF(CLASS) unsigned CLASS##Abbrev = 0;
#include "llvm/IR/Metadata.def"

  for (const Metadata *MD : MDs) {
    if (IndexPos)
      IndexPos->push_back(Stream.GetCurrentBitNo());
    if (const MDNode *N = dyn_cast<MDNode>(MD)) {
      assert(N->isResolved() && "Expected forward references to be resolved");

      switch (N->getMetadataID()) {
      default:
        llvm_unreachable("Invalid MDNode subclass");
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case Metadata::CLASS##Kind:                                                  \
    if (MDAbbrevs)                                                             \
      write##CLASS(cast<CLASS>(N), Record,                                     \
                   (*MDAbbrevs)[MetadataAbbrev::CLASS##AbbrevID]);             \
    else                                                                       \
      write##CLASS(cast<CLASS>(N), Record, CLASS##Abbrev);                     \
    continue;
#include "llvm/IR/Metadata.def"
      }
    }
    writeValueAsMetadata(cast<ValueAsMetadata>(MD), Record);
  }
}

unsigned DXILBitcodeWriter::createMetadataStringsAbbrev() {
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_STRING_OLD));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
  return Stream.EmitAbbrev(std::move(Abbv));
}

void DXILBitcodeWriter::writeMetadataStrings(ArrayRef<const Metadata *> Strings,
                                             SmallVectorImpl<uint64_t> &Record) {
  for (const Metadata *MD : Strings) {
    const MDString *MDS = cast<MDString>(MD);
    // Code: [strchar x N]
    Record.append(MDS->bytes_begin(), MDS->bytes_end());

    // Emit the finished record.
    Stream.EmitRecord(bitc::METADATA_STRING_OLD, Record, createMetadataStringsAbbrev());
    Record.clear();
  }
}

void DXILBitcodeWriter::writeModuleMetadata() {
  if (!VE.hasMDs() && M.named_metadata_empty())
    return;

  Stream.EnterSubblock(bitc::METADATA_BLOCK_ID, 5);

  // Emit all abbrevs upfront, so that the reader can jump in the middle of the
  // block and load any metadata.
  std::vector<unsigned> MDAbbrevs;

  MDAbbrevs.resize(MetadataAbbrev::LastPlusOne);
  MDAbbrevs[MetadataAbbrev::DILocationAbbrevID] = createDILocationAbbrev();
  MDAbbrevs[MetadataAbbrev::GenericDINodeAbbrevID] =
      createGenericDINodeAbbrev();

  unsigned NameAbbrev = 0;
  if (!M.named_metadata_empty()) {
    // Abbrev for METADATA_NAME.
    std::shared_ptr<BitCodeAbbrev> Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::METADATA_NAME));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
    NameAbbrev = Stream.EmitAbbrev(std::move(Abbv));
  }

  SmallVector<uint64_t, 64> Record;
  writeMetadataStrings(VE.getMDStrings(), Record);

  std::vector<uint64_t> IndexPos;
  IndexPos.reserve(VE.getNonMDStrings().size());
  writeMetadataRecords(VE.getNonMDStrings(), Record, &MDAbbrevs, &IndexPos);

  // Write named metadata.
  for (const NamedMDNode &NMD : M.named_metadata()) {
    // Write name.
    StringRef Str = NMD.getName();
    Record.append(Str.bytes_begin(), Str.bytes_end());
    Stream.EmitRecord(bitc::METADATA_NAME, Record, NameAbbrev);
    Record.clear();

    // Write named metadata operands.
    for (const MDNode *N : NMD.operands())
      Record.push_back(VE.getMetadataID(N));
    Stream.EmitRecord(bitc::METADATA_NAMED_NODE, Record, 0);
    Record.clear();
  }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeFunctionMetadata(const Function &F) {
  if (!VE.hasMDs())
    return;

  Stream.EnterSubblock(bitc::METADATA_BLOCK_ID, 4);
  SmallVector<uint64_t, 64> Record;
  writeMetadataStrings(VE.getMDStrings(), Record);
  writeMetadataRecords(VE.getNonMDStrings(), Record);
  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeFunctionMetadataAttachment(const Function &F) {
  Stream.EnterSubblock(bitc::METADATA_ATTACHMENT_ID, 3);

  SmallVector<uint64_t, 64> Record;

  // Write metadata attachments
  // METADATA_ATTACHMENT - [m x [value, [n x [id, mdnode]]]
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  if (!MDs.empty()) {
    for (const auto &I : MDs) {
      Record.push_back(I.first);
      Record.push_back(VE.getMetadataID(I.second));
    }
    Stream.EmitRecord(bitc::METADATA_ATTACHMENT, Record, 0);
    Record.clear();
  }

  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB) {
      MDs.clear();
      I.getAllMetadataOtherThanDebugLoc(MDs);

      // If no metadata, ignore instruction.
      if (MDs.empty())
        continue;

      Record.push_back(VE.getInstructionID(&I));

      for (unsigned i = 0, e = MDs.size(); i != e; ++i) {
        Record.push_back(MDs[i].first);
        Record.push_back(VE.getMetadataID(MDs[i].second));
      }
      Stream.EmitRecord(bitc::METADATA_ATTACHMENT, Record, 0);
      Record.clear();
    }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeModuleMetadataKinds() {
  SmallVector<uint64_t, 64> Record;

  // Write metadata kinds
  // METADATA_KIND - [n x [id, name]]
  SmallVector<StringRef, 8> Names;
  M.getMDKindNames(Names);

  if (Names.empty())
    return;

  Stream.EnterSubblock(bitc::METADATA_BLOCK_ID, 3);

  for (unsigned MDKindID = 0, e = Names.size(); MDKindID != e; ++MDKindID) {
    Record.push_back(MDKindID);
    StringRef KName = Names[MDKindID];
    Record.append(KName.begin(), KName.end());

    Stream.EmitRecord(bitc::METADATA_KIND, Record, 0);
    Record.clear();
  }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeConstants(unsigned FirstVal, unsigned LastVal,
                                       bool isGlobal) {
  if (FirstVal == LastVal)
    return;

  Stream.EnterSubblock(bitc::CONSTANTS_BLOCK_ID, 4);

  unsigned AggregateAbbrev = 0;
  unsigned String8Abbrev = 0;
  unsigned CString7Abbrev = 0;
  unsigned CString6Abbrev = 0;
  // If this is a constant pool for the module, emit module-specific abbrevs.
  if (isGlobal) {
    // Abbrev for CST_CODE_AGGREGATE.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_AGGREGATE));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(
        BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, Log2_32_Ceil(LastVal + 1)));
    AggregateAbbrev = Stream.EmitAbbrev(std::move(Abbv));

    // Abbrev for CST_CODE_STRING.
    Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_STRING));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
    String8Abbrev = Stream.EmitAbbrev(std::move(Abbv));
    // Abbrev for CST_CODE_CSTRING.
    Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_CSTRING));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7));
    CString7Abbrev = Stream.EmitAbbrev(std::move(Abbv));
    // Abbrev for CST_CODE_CSTRING.
    Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_CSTRING));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
    CString6Abbrev = Stream.EmitAbbrev(std::move(Abbv));
  }

  SmallVector<uint64_t, 64> Record;

  const ValueEnumerator::ValueList &Vals = VE.getValues();
  Type *LastTy = nullptr;
  for (unsigned i = FirstVal; i != LastVal; ++i) {
    const Value *V = Vals[i].first;
    // If we need to switch types, do so now.
    if (V->getType() != LastTy) {
      LastTy = V->getType();
      Record.push_back(VE.getTypeID(LastTy));
      Stream.EmitRecord(bitc::CST_CODE_SETTYPE, Record,
                        CONSTANTS_SETTYPE_ABBREV);
      Record.clear();
    }

    if (const InlineAsm *IA = dyn_cast<InlineAsm>(V)) {
      Record.push_back(unsigned(IA->hasSideEffects()) |
                       unsigned(IA->isAlignStack()) << 1 |
                       unsigned(IA->getDialect() & 1) << 2);

      // Add the asm string.
      const std::string &AsmStr = IA->getAsmString();
      Record.push_back(AsmStr.size());
      Record.append(AsmStr.begin(), AsmStr.end());

      // Add the constraint string.
      const std::string &ConstraintStr = IA->getConstraintString();
      Record.push_back(ConstraintStr.size());
      Record.append(ConstraintStr.begin(), ConstraintStr.end());
      Stream.EmitRecord(bitc::CST_CODE_INLINEASM, Record);
      Record.clear();
      continue;
    }
    const Constant *C = cast<Constant>(V);
    unsigned Code = -1U;
    unsigned AbbrevToUse = 0;
    if (C->isNullValue()) {
      Code = bitc::CST_CODE_NULL;
    } else if (isa<UndefValue>(C)) {
      Code = bitc::CST_CODE_UNDEF;
    } else if (const ConstantInt *IV = dyn_cast<ConstantInt>(C)) {
      if (IV->getBitWidth() <= 64) {
        uint64_t V = IV->getSExtValue();
        emitSignedInt64(Record, V);
        Code = bitc::CST_CODE_INTEGER;
        AbbrevToUse = CONSTANTS_INTEGER_ABBREV;
      } else { // Wide integers, > 64 bits in size.
        // We have an arbitrary precision integer value to write whose
        // bit width is > 64. However, in canonical unsigned integer
        // format it is likely that the high bits are going to be zero.
        // So, we only write the number of active words.
        unsigned NWords = IV->getValue().getActiveWords();
        const uint64_t *RawWords = IV->getValue().getRawData();
        for (unsigned i = 0; i != NWords; ++i) {
          emitSignedInt64(Record, RawWords[i]);
        }
        Code = bitc::CST_CODE_WIDE_INTEGER;
      }
    } else if (const ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
      Code = bitc::CST_CODE_FLOAT;
      Type *Ty = CFP->getType();
      if (Ty->isHalfTy() || Ty->isFloatTy() || Ty->isDoubleTy()) {
        Record.push_back(CFP->getValueAPF().bitcastToAPInt().getZExtValue());
      } else if (Ty->isX86_FP80Ty()) {
        // api needed to prevent premature destruction
        // bits are not in the same order as a normal i80 APInt, compensate.
        APInt api = CFP->getValueAPF().bitcastToAPInt();
        const uint64_t *p = api.getRawData();
        Record.push_back((p[1] << 48) | (p[0] >> 16));
        Record.push_back(p[0] & 0xffffLL);
      } else if (Ty->isFP128Ty() || Ty->isPPC_FP128Ty()) {
        APInt api = CFP->getValueAPF().bitcastToAPInt();
        const uint64_t *p = api.getRawData();
        Record.push_back(p[0]);
        Record.push_back(p[1]);
      } else {
        assert(0 && "Unknown FP type!");
      }
    } else if (isa<ConstantDataSequential>(C) &&
               cast<ConstantDataSequential>(C)->isString()) {
      const ConstantDataSequential *Str = cast<ConstantDataSequential>(C);
      // Emit constant strings specially.
      unsigned NumElts = Str->getNumElements();
      // If this is a null-terminated string, use the denser CSTRING encoding.
      if (Str->isCString()) {
        Code = bitc::CST_CODE_CSTRING;
        --NumElts; // Don't encode the null, which isn't allowed by char6.
      } else {
        Code = bitc::CST_CODE_STRING;
        AbbrevToUse = String8Abbrev;
      }
      bool isCStr7 = Code == bitc::CST_CODE_CSTRING;
      bool isCStrChar6 = Code == bitc::CST_CODE_CSTRING;
      for (unsigned i = 0; i != NumElts; ++i) {
        unsigned char V = Str->getElementAsInteger(i);
        Record.push_back(V);
        isCStr7 &= (V & 128) == 0;
        if (isCStrChar6)
          isCStrChar6 = BitCodeAbbrevOp::isChar6(V);
      }

      if (isCStrChar6)
        AbbrevToUse = CString6Abbrev;
      else if (isCStr7)
        AbbrevToUse = CString7Abbrev;
    } else if (const ConstantDataSequential *CDS =
                   dyn_cast<ConstantDataSequential>(C)) {
      Code = bitc::CST_CODE_DATA;
      Type *EltTy = CDS->getType()->getArrayElementType();
      if (isa<IntegerType>(EltTy)) {
        for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i)
          Record.push_back(CDS->getElementAsInteger(i));
      } else if (EltTy->isFloatTy()) {
        for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
          union {
            float F;
            uint32_t I;
          };
          F = CDS->getElementAsFloat(i);
          Record.push_back(I);
        }
      } else {
        assert(EltTy->isDoubleTy() && "Unknown ConstantData element type");
        for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
          union {
            double F;
            uint64_t I;
          };
          F = CDS->getElementAsDouble(i);
          Record.push_back(I);
        }
      }
    } else if (isa<ConstantArray>(C) || isa<ConstantStruct>(C) ||
               isa<ConstantVector>(C)) {
      Code = bitc::CST_CODE_AGGREGATE;
      for (const Value *Op : C->operands())
        Record.push_back(VE.getValueID(Op));
      AbbrevToUse = AggregateAbbrev;
    } else if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
      switch (CE->getOpcode()) {
      default:
        if (Instruction::isCast(CE->getOpcode())) {
          Code = bitc::CST_CODE_CE_CAST;
          Record.push_back(getEncodedCastOpcode(CE->getOpcode()));
          Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
          Record.push_back(VE.getValueID(C->getOperand(0)));
          AbbrevToUse = CONSTANTS_CE_CAST_Abbrev;
        } else {
          assert(CE->getNumOperands() == 2 && "Unknown constant expr!");
          Code = bitc::CST_CODE_CE_BINOP;
          Record.push_back(getEncodedBinaryOpcode(CE->getOpcode()));
          Record.push_back(VE.getValueID(C->getOperand(0)));
          Record.push_back(VE.getValueID(C->getOperand(1)));
          uint64_t Flags = getOptimizationFlags(CE);
          if (Flags != 0)
            Record.push_back(Flags);
        }
        break;
      case Instruction::GetElementPtr: {
        Code = bitc::CST_CODE_CE_GEP;
        const auto *GO = cast<GEPOperator>(C);
        if (GO->isInBounds())
          Code = bitc::CST_CODE_CE_INBOUNDS_GEP;
        Record.push_back(VE.getTypeID(GO->getSourceElementType()));
        for (unsigned i = 0, e = CE->getNumOperands(); i != e; ++i) {
          Record.push_back(VE.getTypeID(C->getOperand(i)->getType()));
          Record.push_back(VE.getValueID(C->getOperand(i)));
        }
        break;
      }
      case Instruction::Select:
        Code = bitc::CST_CODE_CE_SELECT;
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        Record.push_back(VE.getValueID(C->getOperand(2)));
        break;
      case Instruction::ExtractElement:
        Code = bitc::CST_CODE_CE_EXTRACTELT;
        Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getTypeID(C->getOperand(1)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        break;
      case Instruction::InsertElement:
        Code = bitc::CST_CODE_CE_INSERTELT;
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        Record.push_back(VE.getTypeID(C->getOperand(2)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(2)));
        break;
      case Instruction::ShuffleVector:
        // If the return type and argument types are the same, this is a
        // standard shufflevector instruction.  If the types are different,
        // then the shuffle is widening or truncating the input vectors, and
        // the argument type must also be encoded.
        if (C->getType() == C->getOperand(0)->getType()) {
          Code = bitc::CST_CODE_CE_SHUFFLEVEC;
        } else {
          Code = bitc::CST_CODE_CE_SHUFVEC_EX;
          Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
        }
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        Record.push_back(VE.getValueID(C->getOperand(2)));
        break;
      case Instruction::ICmp:
      case Instruction::FCmp:
        Code = bitc::CST_CODE_CE_CMP;
        Record.push_back(VE.getTypeID(C->getOperand(0)->getType()));
        Record.push_back(VE.getValueID(C->getOperand(0)));
        Record.push_back(VE.getValueID(C->getOperand(1)));
        Record.push_back(CE->getPredicate());
        break;
      }
    } else if (const BlockAddress *BA = dyn_cast<BlockAddress>(C)) {
      Code = bitc::CST_CODE_BLOCKADDRESS;
      Record.push_back(VE.getTypeID(BA->getFunction()->getType()));
      Record.push_back(VE.getValueID(BA->getFunction()));
      Record.push_back(VE.getGlobalBasicBlockID(BA->getBasicBlock()));
    } else {
#ifndef NDEBUG
      C->dump();
#endif
      llvm_unreachable("Unknown constant!");
    }
    Stream.EmitRecord(Code, Record, AbbrevToUse);
    Record.clear();
  }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeModuleConstants() {
  const ValueEnumerator::ValueList &Vals = VE.getValues();

  // Find the first constant to emit, which is the first non-globalvalue value.
  // We know globalvalues have been emitted by WriteModuleInfo.
  for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
    if (!isa<GlobalValue>(Vals[i].first)) {
      writeConstants(i, Vals.size(), true);
      return;
    }
  }
}

/// pushValueAndType - The file has to encode both the value and type id for
/// many values, because we need to know what type to create for forward
/// references.  However, most operands are not forward references, so this type
/// field is not needed.
///
/// This function adds V's value ID to Vals.  If the value ID is higher than the
/// instruction ID, then it is a forward reference, and it also includes the
/// type ID.  The value ID that is written is encoded relative to the InstID.
bool DXILBitcodeWriter::pushValueAndType(const Value *V, unsigned InstID,
                                         SmallVectorImpl<unsigned> &Vals) {
  unsigned ValID = VE.getValueID(V);
  // Make encoding relative to the InstID.
  Vals.push_back(InstID - ValID);
  if (ValID >= InstID) {
    Vals.push_back(VE.getTypeID(V->getType()));
    return true;
  }
  return false;
}

/// pushValue - Like pushValueAndType, but where the type of the value is
/// omitted (perhaps it was already encoded in an earlier operand).
void DXILBitcodeWriter::pushValue(const Value *V, unsigned InstID,
                                  SmallVectorImpl<unsigned> &Vals) {
  unsigned ValID = VE.getValueID(V);
  Vals.push_back(InstID - ValID);
}

void DXILBitcodeWriter::pushValueSigned(const Value *V, unsigned InstID,
                                        SmallVectorImpl<uint64_t> &Vals) {
  unsigned ValID = VE.getValueID(V);
  int64_t diff = ((int32_t)InstID - (int32_t)ValID);
  emitSignedInt64(Vals, diff);
}

/// WriteInstruction - Emit an instruction
void DXILBitcodeWriter::writeInstruction(const Instruction &I, unsigned InstID,
                                         SmallVectorImpl<unsigned> &Vals) {
  unsigned Code = 0;
  unsigned AbbrevToUse = 0;
  VE.setInstructionID(&I);
  switch (I.getOpcode()) {
  default:
    if (Instruction::isCast(I.getOpcode())) {
      Code = bitc::FUNC_CODE_INST_CAST;
      if (!pushValueAndType(I.getOperand(0), InstID, Vals))
        AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_CAST_ABBREV;
      Vals.push_back(VE.getTypeID(I.getType()));
      Vals.push_back(getEncodedCastOpcode(I.getOpcode()));
    } else {
      assert(isa<BinaryOperator>(I) && "Unknown instruction!");
      Code = bitc::FUNC_CODE_INST_BINOP;
      if (!pushValueAndType(I.getOperand(0), InstID, Vals))
        AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_BINOP_ABBREV;
      pushValue(I.getOperand(1), InstID, Vals);
      Vals.push_back(getEncodedBinaryOpcode(I.getOpcode()));
      uint64_t Flags = getOptimizationFlags(&I);
      if (Flags != 0) {
        if (AbbrevToUse == (unsigned)dxilc::FUNCTION_INST_BINOP_ABBREV)
          AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_BINOP_FLAGS_ABBREV;
        Vals.push_back(Flags);
      }
    }
    break;

  case Instruction::GetElementPtr: {
    Code = bitc::FUNC_CODE_INST_GEP;
    AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_GEP_ABBREV;
    auto &GEPInst = cast<GetElementPtrInst>(I);
    Vals.push_back(GEPInst.isInBounds());
    Vals.push_back(VE.getTypeID(GEPInst.getSourceElementType()));
    for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i)
      pushValueAndType(I.getOperand(i), InstID, Vals);
    break;
  }
  case Instruction::ExtractValue: {
    Code = bitc::FUNC_CODE_INST_EXTRACTVAL;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    const ExtractValueInst *EVI = cast<ExtractValueInst>(&I);
    Vals.append(EVI->idx_begin(), EVI->idx_end());
    break;
  }
  case Instruction::InsertValue: {
    Code = bitc::FUNC_CODE_INST_INSERTVAL;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValueAndType(I.getOperand(1), InstID, Vals);
    const InsertValueInst *IVI = cast<InsertValueInst>(&I);
    Vals.append(IVI->idx_begin(), IVI->idx_end());
    break;
  }
  case Instruction::Select:
    Code = bitc::FUNC_CODE_INST_VSELECT;
    pushValueAndType(I.getOperand(1), InstID, Vals);
    pushValue(I.getOperand(2), InstID, Vals);
    pushValueAndType(I.getOperand(0), InstID, Vals);
    break;
  case Instruction::ExtractElement:
    Code = bitc::FUNC_CODE_INST_EXTRACTELT;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValueAndType(I.getOperand(1), InstID, Vals);
    break;
  case Instruction::InsertElement:
    Code = bitc::FUNC_CODE_INST_INSERTELT;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValue(I.getOperand(1), InstID, Vals);
    pushValueAndType(I.getOperand(2), InstID, Vals);
    break;
  case Instruction::ShuffleVector:
    Code = bitc::FUNC_CODE_INST_SHUFFLEVEC;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValue(I.getOperand(1), InstID, Vals);
    pushValue(I.getOperand(2), InstID, Vals);
    break;
  case Instruction::ICmp:
  case Instruction::FCmp: {
    // compare returning Int1Ty or vector of Int1Ty
    Code = bitc::FUNC_CODE_INST_CMP2;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    pushValue(I.getOperand(1), InstID, Vals);
    Vals.push_back(cast<CmpInst>(I).getPredicate());
    uint64_t Flags = getOptimizationFlags(&I);
    if (Flags != 0)
      Vals.push_back(Flags);
    break;
  }

  case Instruction::Ret: {
    Code = bitc::FUNC_CODE_INST_RET;
    unsigned NumOperands = I.getNumOperands();
    if (NumOperands == 0)
      AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_RET_VOID_ABBREV;
    else if (NumOperands == 1) {
      if (!pushValueAndType(I.getOperand(0), InstID, Vals))
        AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_RET_VAL_ABBREV;
    } else {
      for (unsigned i = 0, e = NumOperands; i != e; ++i)
        pushValueAndType(I.getOperand(i), InstID, Vals);
    }
  } break;
  case Instruction::Br: {
    Code = bitc::FUNC_CODE_INST_BR;
    const BranchInst &II = cast<BranchInst>(I);
    Vals.push_back(VE.getValueID(II.getSuccessor(0)));
    if (II.isConditional()) {
      Vals.push_back(VE.getValueID(II.getSuccessor(1)));
      pushValue(II.getCondition(), InstID, Vals);
    }
  } break;
  case Instruction::Switch: {
    Code = bitc::FUNC_CODE_INST_SWITCH;
    const SwitchInst &SI = cast<SwitchInst>(I);
    Vals.push_back(VE.getTypeID(SI.getCondition()->getType()));
    pushValue(SI.getCondition(), InstID, Vals);
    Vals.push_back(VE.getValueID(SI.getDefaultDest()));
    for (auto Case : SI.cases()) {
      Vals.push_back(VE.getValueID(Case.getCaseValue()));
      Vals.push_back(VE.getValueID(Case.getCaseSuccessor()));
    }
  } break;
  case Instruction::IndirectBr:
    Code = bitc::FUNC_CODE_INST_INDIRECTBR;
    Vals.push_back(VE.getTypeID(I.getOperand(0)->getType()));
    // Encode the address operand as relative, but not the basic blocks.
    pushValue(I.getOperand(0), InstID, Vals);
    for (unsigned i = 1, e = I.getNumOperands(); i != e; ++i)
      Vals.push_back(VE.getValueID(I.getOperand(i)));
    break;

  case Instruction::Invoke: {
    const InvokeInst *II = cast<InvokeInst>(&I);
    const Value *Callee = II->getCalledOperand();
    FunctionType *FTy = II->getFunctionType();
    Code = bitc::FUNC_CODE_INST_INVOKE;

    Vals.push_back(VE.getAttributeListID(II->getAttributes()));
    Vals.push_back(II->getCallingConv() | 1 << 13);
    Vals.push_back(VE.getValueID(II->getNormalDest()));
    Vals.push_back(VE.getValueID(II->getUnwindDest()));
    Vals.push_back(VE.getTypeID(FTy));
    pushValueAndType(Callee, InstID, Vals);

    // Emit value #'s for the fixed parameters.
    for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
      pushValue(I.getOperand(i), InstID, Vals); // fixed param.

    // Emit type/value pairs for varargs params.
    if (FTy->isVarArg()) {
      for (unsigned i = FTy->getNumParams(), e = I.getNumOperands() - 3; i != e;
           ++i)
        pushValueAndType(I.getOperand(i), InstID, Vals); // vararg
    }
    break;
  }
  case Instruction::Resume:
    Code = bitc::FUNC_CODE_INST_RESUME;
    pushValueAndType(I.getOperand(0), InstID, Vals);
    break;
  case Instruction::Unreachable:
    Code = bitc::FUNC_CODE_INST_UNREACHABLE;
    AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_UNREACHABLE_ABBREV;
    break;

  case Instruction::PHI: {
    const PHINode &PN = cast<PHINode>(I);
    Code = bitc::FUNC_CODE_INST_PHI;
    // With the newer instruction encoding, forward references could give
    // negative valued IDs.  This is most common for PHIs, so we use
    // signed VBRs.
    SmallVector<uint64_t, 128> Vals64;
    Vals64.push_back(VE.getTypeID(PN.getType()));
    for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i) {
      pushValueSigned(PN.getIncomingValue(i), InstID, Vals64);
      Vals64.push_back(VE.getValueID(PN.getIncomingBlock(i)));
    }
    // Emit a Vals64 vector and exit.
    Stream.EmitRecord(Code, Vals64, AbbrevToUse);
    Vals64.clear();
    return;
  }

  case Instruction::LandingPad: {
    const LandingPadInst &LP = cast<LandingPadInst>(I);
    Code = bitc::FUNC_CODE_INST_LANDINGPAD;
    Vals.push_back(VE.getTypeID(LP.getType()));
    Vals.push_back(LP.isCleanup());
    Vals.push_back(LP.getNumClauses());
    for (unsigned I = 0, E = LP.getNumClauses(); I != E; ++I) {
      if (LP.isCatch(I))
        Vals.push_back(LandingPadInst::Catch);
      else
        Vals.push_back(LandingPadInst::Filter);
      pushValueAndType(LP.getClause(I), InstID, Vals);
    }
    break;
  }

  case Instruction::Alloca: {
    Code = bitc::FUNC_CODE_INST_ALLOCA;
    const AllocaInst &AI = cast<AllocaInst>(I);
    Vals.push_back(VE.getTypeID(AI.getAllocatedType()));
    Vals.push_back(VE.getTypeID(I.getOperand(0)->getType()));
    Vals.push_back(VE.getValueID(I.getOperand(0))); // size.
    using APV = AllocaPackedValues;
    unsigned Record = 0;
    unsigned EncodedAlign = getEncodedAlign(AI.getAlign());
    Bitfield::set<APV::AlignLower>(
        Record, EncodedAlign & ((1 << APV::AlignLower::Bits) - 1));
    Bitfield::set<APV::AlignUpper>(Record,
                                   EncodedAlign >> APV::AlignLower::Bits);
    Bitfield::set<APV::UsedWithInAlloca>(Record, AI.isUsedWithInAlloca());
    Vals.push_back(Record);
    break;
  }

  case Instruction::Load:
    if (cast<LoadInst>(I).isAtomic()) {
      Code = bitc::FUNC_CODE_INST_LOADATOMIC;
      pushValueAndType(I.getOperand(0), InstID, Vals);
    } else {
      Code = bitc::FUNC_CODE_INST_LOAD;
      if (!pushValueAndType(I.getOperand(0), InstID, Vals)) // ptr
        AbbrevToUse = (unsigned)dxilc::FUNCTION_INST_LOAD_ABBREV;
    }
    Vals.push_back(VE.getTypeID(I.getType()));
    Vals.push_back(Log2_32(cast<LoadInst>(I).getAlignment()) + 1);
    Vals.push_back(cast<LoadInst>(I).isVolatile());
    if (cast<LoadInst>(I).isAtomic()) {
      Vals.push_back(getEncodedOrdering(cast<LoadInst>(I).getOrdering()));
      Vals.push_back(getEncodedSyncScopeID(cast<LoadInst>(I).getSyncScopeID()));
    }
    break;
  case Instruction::Store:
    if (cast<StoreInst>(I).isAtomic())
      Code = bitc::FUNC_CODE_INST_STOREATOMIC;
    else
      Code = bitc::FUNC_CODE_INST_STORE;
    pushValueAndType(I.getOperand(1), InstID, Vals); // ptrty + ptr
    pushValueAndType(I.getOperand(0), InstID, Vals); // valty + val
    Vals.push_back(Log2_32(cast<StoreInst>(I).getAlignment()) + 1);
    Vals.push_back(cast<StoreInst>(I).isVolatile());
    if (cast<StoreInst>(I).isAtomic()) {
      Vals.push_back(getEncodedOrdering(cast<StoreInst>(I).getOrdering()));
      Vals.push_back(
          getEncodedSyncScopeID(cast<StoreInst>(I).getSyncScopeID()));
    }
    break;
  case Instruction::AtomicCmpXchg:
    Code = bitc::FUNC_CODE_INST_CMPXCHG;
    pushValueAndType(I.getOperand(0), InstID, Vals); // ptrty + ptr
    pushValueAndType(I.getOperand(1), InstID, Vals); // cmp.
    pushValue(I.getOperand(2), InstID, Vals);        // newval.
    Vals.push_back(cast<AtomicCmpXchgInst>(I).isVolatile());
    Vals.push_back(
        getEncodedOrdering(cast<AtomicCmpXchgInst>(I).getSuccessOrdering()));
    Vals.push_back(
        getEncodedSyncScopeID(cast<AtomicCmpXchgInst>(I).getSyncScopeID()));
    Vals.push_back(
        getEncodedOrdering(cast<AtomicCmpXchgInst>(I).getFailureOrdering()));
    Vals.push_back(cast<AtomicCmpXchgInst>(I).isWeak());
    break;
  case Instruction::AtomicRMW:
    Code = bitc::FUNC_CODE_INST_ATOMICRMW;
    pushValueAndType(I.getOperand(0), InstID, Vals); // ptrty + ptr
    pushValue(I.getOperand(1), InstID, Vals);        // val.
    Vals.push_back(
        getEncodedRMWOperation(cast<AtomicRMWInst>(I).getOperation()));
    Vals.push_back(cast<AtomicRMWInst>(I).isVolatile());
    Vals.push_back(getEncodedOrdering(cast<AtomicRMWInst>(I).getOrdering()));
    Vals.push_back(
        getEncodedSyncScopeID(cast<AtomicRMWInst>(I).getSyncScopeID()));
    break;
  case Instruction::Fence:
    Code = bitc::FUNC_CODE_INST_FENCE;
    Vals.push_back(getEncodedOrdering(cast<FenceInst>(I).getOrdering()));
    Vals.push_back(getEncodedSyncScopeID(cast<FenceInst>(I).getSyncScopeID()));
    break;
  case Instruction::Call: {
    const CallInst &CI = cast<CallInst>(I);
    FunctionType *FTy = CI.getFunctionType();

    Code = bitc::FUNC_CODE_INST_CALL;

    Vals.push_back(VE.getAttributeListID(CI.getAttributes()));
    Vals.push_back((CI.getCallingConv() << 1) | unsigned(CI.isTailCall()) |
                   unsigned(CI.isMustTailCall()) << 14 | 1 << 15);
    Vals.push_back(VE.getTypeID(FTy));
    pushValueAndType(CI.getCalledOperand(), InstID, Vals); // Callee

    // Emit value #'s for the fixed parameters.
    for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i) {
      // Check for labels (can happen with asm labels).
      if (FTy->getParamType(i)->isLabelTy())
        Vals.push_back(VE.getValueID(CI.getArgOperand(i)));
      else
        pushValue(CI.getArgOperand(i), InstID, Vals); // fixed param.
    }

    // Emit type/value pairs for varargs params.
    if (FTy->isVarArg()) {
      for (unsigned i = FTy->getNumParams(), e = CI.arg_size(); i != e; ++i)
        pushValueAndType(CI.getArgOperand(i), InstID, Vals); // varargs
    }
    break;
  }
  case Instruction::VAArg:
    Code = bitc::FUNC_CODE_INST_VAARG;
    Vals.push_back(VE.getTypeID(I.getOperand(0)->getType())); // valistty
    pushValue(I.getOperand(0), InstID, Vals);                 // valist.
    Vals.push_back(VE.getTypeID(I.getType()));                // restype.
    break;
  }

  Stream.EmitRecord(Code, Vals, AbbrevToUse);
  Vals.clear();
}

// Emit names for globals/functions etc.
void DXILBitcodeWriter::writeFunctionLevelValueSymbolTable(
    const ValueSymbolTable &VST) {
  if (VST.empty())
    return;
  Stream.EnterSubblock(bitc::VALUE_SYMTAB_BLOCK_ID, 4);

  // FIXME: Set up the abbrev, we know how many values there are!
  // FIXME: We know if the type names can use 7-bit ascii.
  SmallVector<unsigned, 64> NameVals;

  // HLSL Change - Begin
  // Read the named values from a sorted list instead of the original list
  // to ensure the binary is the same no matter what values ever existed.
  SmallVector<const ValueName *, 16> SortedTable;

  for (ValueSymbolTable::const_iterator SI = VST.begin(), SE = VST.end();
       SI != SE; ++SI) {
    SortedTable.push_back(&(*SI));
  }
  // The keys are unique, so there shouldn't be stability issues
  std::sort(SortedTable.begin(), SortedTable.end(),
            [](const ValueName *A, const ValueName *B) {
              return (*A).first() < (*B).first();
            });

  for (const ValueName *SI : SortedTable) {
    auto &Name = *SI;
// HLSL Change - End
#if 0 // HLSL Change
  for (ValueSymbolTable::const_iterator SI = VST.begin(), SE = VST.end();
       SI != SE; ++SI) {
    const ValueName &Name = *SI;
#endif // HLSL Change

    // Figure out the encoding to use for the name.
    bool is7Bit = true;
    bool isChar6 = true;
    for (const char *C = Name.getKeyData(), *E = C + Name.getKeyLength();
         C != E; ++C) {
      if (isChar6)
        isChar6 = BitCodeAbbrevOp::isChar6(*C);
      if ((unsigned char)*C & 128) {
        is7Bit = false;
        break; // don't bother scanning the rest.
      }
    }

    unsigned AbbrevToUse = VST_ENTRY_8_ABBREV;

    // VST_ENTRY:   [valueid, namechar x N]
    // VST_BBENTRY: [bbid, namechar x N]
    unsigned Code;
    if (isa<BasicBlock>(SI->getValue())) {
      Code = bitc::VST_CODE_BBENTRY;
      if (isChar6)
        AbbrevToUse = VST_BBENTRY_6_ABBREV;
    } else {
      Code = bitc::VST_CODE_ENTRY;
      if (isChar6)
        AbbrevToUse = VST_ENTRY_6_ABBREV;
      else if (is7Bit)
        AbbrevToUse = VST_ENTRY_7_ABBREV;
    }

    NameVals.push_back(VE.getValueID(SI->getValue()));
    for (const char *P = Name.getKeyData(),
                    *E = Name.getKeyData() + Name.getKeyLength();
         P != E; ++P)
      NameVals.push_back((unsigned char)*P);

    // Emit the finished record.
    Stream.EmitRecord(Code, NameVals, AbbrevToUse);
    NameVals.clear();
  }
  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeUseList(UseListOrder &&Order) {
  assert(Order.Shuffle.size() >= 2 && "Shuffle too small");
  unsigned Code;
  if (isa<BasicBlock>(Order.V))
    Code = bitc::USELIST_CODE_BB;
  else
    Code = bitc::USELIST_CODE_DEFAULT;

  SmallVector<uint64_t, 64> Record(Order.Shuffle.begin(), Order.Shuffle.end());
  Record.push_back(VE.getValueID(Order.V));
  Stream.EmitRecord(Code, Record);
}

void DXILBitcodeWriter::writeUseListBlock(const Function *F) {
  assert(VE.shouldPreserveUseListOrder() &&
         "Expected to be preserving use-list order");

  auto hasMore = [&]() {
    return !VE.UseListOrders.empty() && VE.UseListOrders.back().F == F;
  };
  if (!hasMore())
    // Nothing to do.
    return;

  Stream.EnterSubblock(bitc::USELIST_BLOCK_ID, 3);
  while (hasMore()) {
    writeUseList(std::move(VE.UseListOrders.back()));
    VE.UseListOrders.pop_back();
  }
  Stream.ExitBlock();
}

/// Emit a function body to the module stream.
void DXILBitcodeWriter::writeFunction(const Function &F) {
  Stream.EnterSubblock(bitc::FUNCTION_BLOCK_ID, 4);
  VE.incorporateFunction(F);

  SmallVector<unsigned, 64> Vals;

  // Emit the number of basic blocks, so the reader can create them ahead of
  // time.
  Vals.push_back(VE.getBasicBlocks().size());
  Stream.EmitRecord(bitc::FUNC_CODE_DECLAREBLOCKS, Vals);
  Vals.clear();

  // If there are function-local constants, emit them now.
  unsigned CstStart, CstEnd;
  VE.getFunctionConstantRange(CstStart, CstEnd);
  writeConstants(CstStart, CstEnd, false);

  // If there is function-local metadata, emit it now.
  writeFunctionMetadata(F);

  // Keep a running idea of what the instruction ID is.
  unsigned InstID = CstEnd;

  bool NeedsMetadataAttachment = F.hasMetadata();

  DILocation *LastDL = nullptr;

  // Finally, emit all the instructions, in order.
  for (Function::const_iterator BB = F.begin(), E = F.end(); BB != E; ++BB)
    for (BasicBlock::const_iterator I = BB->begin(), E = BB->end(); I != E;
         ++I) {
      writeInstruction(*I, InstID, Vals);

      if (!I->getType()->isVoidTy())
        ++InstID;

      // If the instruction has metadata, write a metadata attachment later.
      NeedsMetadataAttachment |= I->hasMetadataOtherThanDebugLoc();

      // If the instruction has a debug location, emit it.
      DILocation *DL = I->getDebugLoc();
      if (!DL)
        continue;

      if (DL == LastDL) {
        // Just repeat the same debug loc as last time.
        Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_LOC_AGAIN, Vals);
        continue;
      }

      Vals.push_back(DL->getLine());
      Vals.push_back(DL->getColumn());
      Vals.push_back(VE.getMetadataOrNullID(DL->getScope()));
      Vals.push_back(VE.getMetadataOrNullID(DL->getInlinedAt()));
      Stream.EmitRecord(bitc::FUNC_CODE_DEBUG_LOC, Vals);
      Vals.clear();

      LastDL = DL;
    }

  // Emit names for all the instructions etc.
  if (auto *Symtab = F.getValueSymbolTable())
    writeFunctionLevelValueSymbolTable(*Symtab);

  if (NeedsMetadataAttachment)
    writeFunctionMetadataAttachment(F);
  if (VE.shouldPreserveUseListOrder())
    writeUseListBlock(&F);
  VE.purgeFunction();
  Stream.ExitBlock();
}

// Emit blockinfo, which defines the standard abbreviations etc.
void DXILBitcodeWriter::writeBlockInfo() {
  // We only want to emit block info records for blocks that have multiple
  // instances: CONSTANTS_BLOCK, FUNCTION_BLOCK and VALUE_SYMTAB_BLOCK.
  // Other blocks can define their abbrevs inline.
  Stream.EnterBlockInfoBlock();

  { // 8-bit fixed-width VST_ENTRY/VST_BBENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 8));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID,
                                   std::move(Abbv)) != VST_ENTRY_8_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // 7-bit fixed width VST_ENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_ENTRY));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID,
                                   std::move(Abbv)) != VST_ENTRY_7_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // 6-bit char6 VST_ENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_ENTRY));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID,
                                   std::move(Abbv)) != VST_ENTRY_6_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // 6-bit char6 VST_BBENTRY strings.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::VST_CODE_BBENTRY));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Char6));
    if (Stream.EmitBlockInfoAbbrev(bitc::VALUE_SYMTAB_BLOCK_ID,
                                   std::move(Abbv)) != VST_BBENTRY_6_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // SETTYPE abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_SETTYPE));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                              VE.computeBitsRequiredForTypeIndicies()));
    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, std::move(Abbv)) !=
        CONSTANTS_SETTYPE_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // INTEGER abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_INTEGER));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8));
    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, std::move(Abbv)) !=
        CONSTANTS_INTEGER_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // CE_CAST abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_CE_CAST));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // cast opc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,      // typeid
                              VE.computeBitsRequiredForTypeIndicies()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // value id

    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, std::move(Abbv)) !=
        CONSTANTS_CE_CAST_Abbrev)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // NULL abbrev for CONSTANTS_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::CST_CODE_NULL));
    if (Stream.EmitBlockInfoAbbrev(bitc::CONSTANTS_BLOCK_ID, std::move(Abbv)) !=
        CONSTANTS_NULL_Abbrev)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  // FIXME: This should only use space for first class types!

  { // INST_LOAD abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_LOAD));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Ptr
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,    // dest ty
                              VE.computeBitsRequiredForTypeIndicies()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 4));   // Align
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // volatile
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_LOAD_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_BINOP abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_BINOP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // RHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_BINOP_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_BINOP_FLAGS abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_BINOP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // RHS
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 7)); // flags
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_BINOP_FLAGS_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_CAST abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_CAST));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // OpVal
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,    // dest ty
                              VE.computeBitsRequiredForTypeIndicies()));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // opc
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_CAST_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  { // INST_RET abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_RET));
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_RET_VOID_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_RET abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_RET));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ValID
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_RET_VAL_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  { // INST_UNREACHABLE abbrev for FUNCTION_BLOCK.
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_UNREACHABLE));
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_UNREACHABLE_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }
  {
    auto Abbv = std::make_shared<BitCodeAbbrev>();
    Abbv->Add(BitCodeAbbrevOp(bitc::FUNC_CODE_INST_GEP));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, // dest ty
                              Log2_32_Ceil(VE.getTypes().size() + 1)));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
    Abbv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
    if (Stream.EmitBlockInfoAbbrev(bitc::FUNCTION_BLOCK_ID, std::move(Abbv)) !=
        (unsigned)dxilc::FUNCTION_INST_GEP_ABBREV)
      llvm_unreachable("Unexpected abbrev ordering!");
  }

  Stream.ExitBlock();
}

void DXILBitcodeWriter::writeModuleVersion() {
  // VERSION: [version#]
  Stream.EmitRecord(bitc::MODULE_CODE_VERSION, ArrayRef<unsigned>{1});
}

/// WriteModule - Emit the specified module to the bitstream.
void DXILBitcodeWriter::write() {
  // The identification block is new since llvm-3.7, but the old bitcode reader
  // will skip it.
  //writeIdentificationBlock(Stream);

  Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);

  // It is redundant to fully-specify this here, but nice to make it explicit
  // so that it is clear the DXIL module version is different.
  DXILBitcodeWriter::writeModuleVersion();

  // Emit blockinfo, which defines the standard abbreviations etc.
  writeBlockInfo();

  // Emit information about attribute groups.
  writeAttributeGroupTable();

  // Emit information about parameter attributes.
  writeAttributeTable();

  // Emit information describing all of the types in the module.
  writeTypeTable();

  writeComdats();

  // Emit top-level description of module, including target triple, inline asm,
  // descriptors for global variables, and function prototype info.
  writeModuleInfo();

  // Emit constants.
  writeModuleConstants();

  // Emit metadata.
  writeModuleMetadataKinds();

  // Emit metadata.
  writeModuleMetadata();

  // Emit names for globals/functions etc.
  // DXIL uses the same format for module-level value symbol table as for the
  // function level table.
  writeFunctionLevelValueSymbolTable(M.getValueSymbolTable());

  // Emit module-level use-lists.
  if (VE.shouldPreserveUseListOrder())
    writeUseListBlock(nullptr);

  // Emit function bodies.
  for (Module::const_iterator F = M.begin(), E = M.end(); F != E; ++F)
    if (!F->isDeclaration())
      writeFunction(*F);

  Stream.ExitBlock();
}
