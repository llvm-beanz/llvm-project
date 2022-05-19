//===- DXContainer.cpp - DXContainer object file implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/DXContainer.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Object/Error.h"

#include <stddef.h>

using namespace llvm;
using namespace llvm::object;

static Error parseFailed(const Twine &Msg) {
  return make_error<GenericBinaryError>(Msg.str(), object_error::parse_failed);
}

template <typename T>
static Error readStruct(StringRef Buffer, const char *P, T &Struct) {
  // Don't read before the beginning or past the end of the file
  if (P < Buffer.begin() || P + sizeof(T) > Buffer.end())
    return parseFailed("Reading structure out of file bounds");

  memcpy(&Struct, P, sizeof(T));
  // DXContainer is always BigEndian
  if (sys::IsBigEndianHost)
    Struct.byteSwap();
  return Error::success();
}

template <typename T>
static Error readValue(StringRef Buffer, const char *P, T &Val) {
  // Don't read before the beginning or past the end of the file
  if (P < Buffer.begin() || P + sizeof(T) > Buffer.end())
    return parseFailed("Reading structure out of file bounds");

  Val = *reinterpret_cast<const T *>(P);
  // DXContainer is always BigEndian
  if (sys::IsBigEndianHost)
    sys::swapByteOrder(Val);
  return Error::success();
}

DXContainer::DXContainer(MemoryBufferRef O) : Data(O) {}

Error DXContainer::parseHeader() {
  return readStruct(Data.getBuffer(), Data.getBuffer().data(), Header);
}

Error DXContainer::parseDXILHeader(uint32_t Offset) {
  if (DXIL)
    return parseFailed("More than one DXIL part is present in the file");
  const char *Current = Data.getBuffer().data() + Offset;
  dxbc::ProgramHeader Header;
  if (Error Err = readStruct(Data.getBuffer(), Current, Header))
    return Err;
  Current += offsetof(dxbc::ProgramHeader, Bitcode) + Header.Bitcode.Offset;
  DXIL.emplace(std::make_pair(Header, Current));
  return Error::success();
}

Error DXContainer::parsePartOffsets() {
  const char *Current = Data.getBuffer().data() + sizeof(dxbc::Header);
  for (uint32_t Part = 0; Part < Header.PartCount; ++Part) {
    uint32_t PartOffset;
    if (Error Err = readValue(Data.getBuffer(), Current, PartOffset))
      return Err;
    Current += sizeof(uint32_t);
    if (PartOffset + sizeof(dxbc::PartHeader) > Data.getBufferSize())
      return parseFailed("Part offset points beyond boundary of the file");
    PartOffsets.push_back(PartOffset);

    // If this isn't a dxil part stop here...
    if(Data.getBuffer().substr(PartOffset, 4) != "DXIL")
      continue;
    if (Error Err = parseDXILHeader(PartOffset + sizeof(dxbc::PartHeader)))
      return Err;
  }
  return Error::success();
}

Expected<DXContainer> DXContainer::create(MemoryBufferRef Object) {
  DXContainer Container(Object);
  if (Error Err = Container.parseHeader())
    return std::move(Err);
  if (Error Err = Container.parsePartOffsets())
    return std::move(Err);
  return Container;
}

void DXContainer::PartIterator::updateIterator() {
  if (OffsetIt == Container.PartOffsets.end())
    return;
  IteratorState.Offset = *OffsetIt;
  StringRef Buffer = Container.Data.getBuffer();
  const char *Current = Buffer.data() + *OffsetIt;
  // Offsets are validated during parsing, so all offsets in the container are
  // valid and contain enough readable data to read a header.
  cantFail(readStruct(Buffer, Current, IteratorState.Part));
  IteratorState.Data =
      StringRef(Current + sizeof(dxbc::PartHeader), IteratorState.Part.Size);
}
