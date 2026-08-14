//===- FormatRegistryTest.cpp - Tests for feme::FormatRegistry -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/FormatRegistry.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Export/Exporter.h"
#include "feme/Import/Importer.h"
#include "gtest/gtest.h"

using namespace feme;

namespace {

/// A minimal Importer, just enough to register into a FormatRegistry:
/// FormatRegistry itself is format-agnostic, so this test never needs a
/// real (DXIL/SPIR-V/DXBC) Importer -- see DriverTest.cpp's
/// PopulatesFormatRegistry for the "real Importers actually get
/// registered" end of this.
class FakeImporter : public Importer {
public:
  explicit FakeImporter(llvm::StringRef Name) : Name(Name) {}

  llvm::Expected<Module> import(llvm::MemoryBufferRef, const ImportOptions &,
                                Context &) const override {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "FakeImporter never actually imports");
  }

  llvm::StringRef getFormatName() const override { return Name; }

private:
  llvm::StringRef Name;
};

/// A minimal Exporter, mirroring FakeImporter above.
class FakeExporter : public Exporter {
public:
  explicit FakeExporter(llvm::StringRef Name) : Name(Name) {}

  llvm::Error exportModule(Module &, const ExportOptions &, Context &,
                           llvm::raw_pwrite_stream &) const override {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "FakeExporter never actually exports");
  }

  llvm::StringRef getFormatName() const override { return Name; }

private:
  llvm::StringRef Name;
};

TEST(FormatRegistryTest, StartsEmpty) {
  FormatRegistry Registry;
  EXPECT_TRUE(Registry.empty());
  EXPECT_EQ(Registry.lookupImporter("anything"), nullptr);
  EXPECT_EQ(Registry.lookupExporter("anything"), nullptr);
  EXPECT_TRUE(Registry.importers().empty());
}

TEST(FormatRegistryTest, RegisterImporterMakesItLookupable) {
  FormatRegistry Registry;
  FakeImporter Fake("fake");
  Registry.registerImporter(Fake);

  EXPECT_FALSE(Registry.empty());
  EXPECT_EQ(Registry.lookupImporter("fake"), &Fake);
  EXPECT_EQ(Registry.lookupImporter("not-registered"), nullptr);
  ASSERT_EQ(Registry.importers().size(), 1u);
  EXPECT_EQ(Registry.importers()[0], &Fake);
}

TEST(FormatRegistryTest, RegisterExporterMakesItLookupable) {
  FormatRegistry Registry;
  FakeExporter Fake("fake");
  Registry.registerExporter(Fake);

  EXPECT_FALSE(Registry.empty());
  EXPECT_EQ(Registry.lookupExporter("fake"), &Fake);
  EXPECT_EQ(Registry.lookupExporter("not-registered"), nullptr);
  // Registering an Exporter does not also register an Importer: the two
  // are independent (DXBC, for example, has an Importer but no Exporter).
  EXPECT_EQ(Registry.lookupImporter("fake"), nullptr);
}

TEST(FormatRegistryTest, RegistersMultipleFormatsIndependently) {
  FormatRegistry Registry;
  FakeImporter A("format-a");
  FakeImporter B("format-b");
  Registry.registerImporter(A);
  Registry.registerImporter(B);

  EXPECT_EQ(Registry.lookupImporter("format-a"), &A);
  EXPECT_EQ(Registry.lookupImporter("format-b"), &B);
  EXPECT_EQ(Registry.importers().size(), 2u);
}

TEST(ContextTest, GetFormatRegistryStartsEmptyAndIsMutable) {
  // Context itself registers nothing (see the "Deviation: FormatRegistry
  // population" note in feme/docs/Design.md): a bare Context's registry
  // stays empty until some other layer (feme::Driver) populates it.
  Context Ctx;
  EXPECT_TRUE(Ctx.getFormatRegistry().empty());

  FakeImporter Fake("fake");
  Ctx.getFormatRegistry().registerImporter(Fake);
  EXPECT_EQ(Ctx.getFormatRegistry().lookupImporter("fake"), &Fake);
  EXPECT_EQ(const_cast<const Context &>(Ctx).getFormatRegistry().lookupImporter(
                "fake"),
            &Fake);
}

} // namespace
