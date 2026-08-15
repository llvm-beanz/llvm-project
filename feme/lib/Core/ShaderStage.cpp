//===- ShaderStage.cpp - Source-independent shader stage identity --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

StringRef feme::getShaderStageAttrName() { return "feme.shader.stage"; }

StringRef feme::getShaderStageName(ShaderStage Stage) {
  switch (Stage) {
  case ShaderStage::Vertex:
    return "vertex";
  case ShaderStage::Hull:
    return "hull";
  case ShaderStage::Domain:
    return "domain";
  case ShaderStage::Geometry:
    return "geometry";
  case ShaderStage::Fragment:
    return "fragment";
  case ShaderStage::Compute:
    return "compute";
  case ShaderStage::Amplification:
    return "amplification";
  case ShaderStage::Mesh:
    return "mesh";
  case ShaderStage::Library:
    return "library";
  case ShaderStage::RayGeneration:
    return "raygeneration";
  case ShaderStage::Intersection:
    return "intersection";
  case ShaderStage::AnyHit:
    return "anyhit";
  case ShaderStage::ClosestHit:
    return "closesthit";
  case ShaderStage::Miss:
    return "miss";
  case ShaderStage::Callable:
    return "callable";
  case ShaderStage::NumStages:
    break;
  }
  llvm_unreachable("not a shader stage");
}

std::optional<feme::ShaderStage> feme::parseShaderStage(StringRef Name) {
  return StringSwitch<std::optional<ShaderStage>>(Name)
      .Case("vertex", ShaderStage::Vertex)
      .Case("hull", ShaderStage::Hull)
      .Case("domain", ShaderStage::Domain)
      .Case("geometry", ShaderStage::Geometry)
      .Cases({"fragment", "pixel"}, ShaderStage::Fragment)
      .Case("compute", ShaderStage::Compute)
      .Case("amplification", ShaderStage::Amplification)
      .Case("mesh", ShaderStage::Mesh)
      .Case("library", ShaderStage::Library)
      .Case("raygeneration", ShaderStage::RayGeneration)
      .Case("intersection", ShaderStage::Intersection)
      .Case("anyhit", ShaderStage::AnyHit)
      .Case("closesthit", ShaderStage::ClosestHit)
      .Case("miss", ShaderStage::Miss)
      .Case("callable", ShaderStage::Callable)
      .Default(std::nullopt);
}

std::optional<feme::ShaderStage>
feme::getShaderStageForEnvironment(Triple::EnvironmentType Env) {
  switch (Env) {
  case Triple::Vertex:
    return ShaderStage::Vertex;
  case Triple::Hull:
    return ShaderStage::Hull;
  case Triple::Domain:
    return ShaderStage::Domain;
  case Triple::Geometry:
    return ShaderStage::Geometry;
  case Triple::Pixel:
    return ShaderStage::Fragment;
  case Triple::Compute:
    return ShaderStage::Compute;
  case Triple::Amplification:
    return ShaderStage::Amplification;
  case Triple::Mesh:
    return ShaderStage::Mesh;
  case Triple::Library:
    return ShaderStage::Library;
  case Triple::RayGeneration:
    return ShaderStage::RayGeneration;
  case Triple::Intersection:
    return ShaderStage::Intersection;
  case Triple::AnyHit:
    return ShaderStage::AnyHit;
  case Triple::ClosestHit:
    return ShaderStage::ClosestHit;
  case Triple::Miss:
    return ShaderStage::Miss;
  case Triple::Callable:
    return ShaderStage::Callable;
  default:
    // Every other environment -- including `rootsignature`, a container
    // profile rather than a pipeline stage -- names no stage.
    return std::nullopt;
  }
}

Triple::EnvironmentType feme::getEnvironmentForShaderStage(ShaderStage Stage) {
  switch (Stage) {
  case ShaderStage::Vertex:
    return Triple::Vertex;
  case ShaderStage::Hull:
    return Triple::Hull;
  case ShaderStage::Domain:
    return Triple::Domain;
  case ShaderStage::Geometry:
    return Triple::Geometry;
  case ShaderStage::Fragment:
    return Triple::Pixel;
  case ShaderStage::Compute:
    return Triple::Compute;
  case ShaderStage::Amplification:
    return Triple::Amplification;
  case ShaderStage::Mesh:
    return Triple::Mesh;
  case ShaderStage::Library:
    return Triple::Library;
  case ShaderStage::RayGeneration:
    return Triple::RayGeneration;
  case ShaderStage::Intersection:
    return Triple::Intersection;
  case ShaderStage::AnyHit:
    return Triple::AnyHit;
  case ShaderStage::ClosestHit:
    return Triple::ClosestHit;
  case ShaderStage::Miss:
    return Triple::Miss;
  case ShaderStage::Callable:
    return Triple::Callable;
  case ShaderStage::NumStages:
    break;
  }
  llvm_unreachable("not a shader stage");
}

bool feme::isShaderStageCompatibleWithEnvironment(ShaderStage Stage,
                                                  Triple::EnvironmentType Env) {
  std::optional<ShaderStage> EnvStage = getShaderStageForEnvironment(Env);
  if (!EnvStage || *EnvStage == ShaderStage::Library)
    return true;
  return *EnvStage == Stage;
}

void feme::setShaderStage(Function &F, ShaderStage Stage) {
  F.addFnAttr(getShaderStageAttrName(), getShaderStageName(Stage));
}

std::optional<feme::ShaderStage> feme::getShaderStage(const Function &F) {
  for (StringRef Attr : {getShaderStageAttrName(), StringRef("hlsl.shader")})
    if (F.hasFnAttribute(Attr))
      return parseShaderStage(F.getFnAttribute(Attr).getValueAsString());
  return std::nullopt;
}
