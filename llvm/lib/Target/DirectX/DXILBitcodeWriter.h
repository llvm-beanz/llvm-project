//===- Bitcode/Writer/DXILBitcodeWriter.cpp - DXIL Bitcode Writer ---------===//
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

#include "llvm/Bitcode/BitcodeCommon.h"
#include "llvm/Bitcode/BitcodeWriterBase.h"

namespace llvm {

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
  void write() override;

  static std::unique_ptr<ModuleBitcodeWriterBase>
  Create(const Module &M, SmallVectorImpl<char> &Buffer,
         StringTableBuilder &StrtabBuilder, BitstreamWriter &Stream,
         bool ShouldPreserveUseListOrder, const ModuleSummaryIndex *Index,
         bool GenerateHash, ModuleHash *ModHash);

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
} // namespace llvm
