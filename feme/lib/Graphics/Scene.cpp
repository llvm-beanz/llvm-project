//===- Scene.cpp - Textual scene description read -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/Scene.h"

#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

LLVM_YAML_IS_SEQUENCE_VECTOR(feme::graphics::SceneAttachment)
LLVM_YAML_IS_SEQUENCE_VECTOR(feme::graphics::SceneVertexAttribute)
LLVM_YAML_IS_SEQUENCE_VECTOR(feme::graphics::SceneVertexBuffer)
LLVM_YAML_IS_SEQUENCE_VECTOR(feme::graphics::SceneTexture)
LLVM_YAML_IS_SEQUENCE_VECTOR(feme::graphics::SceneDraw)

namespace llvm::yaml {

/// `std::vector<uint32_t>`/`std::vector<double>` sequences:
/// `LLVM_YAML_IS_SEQUENCE_VECTOR` rejects fundamental element types (see
/// its own comment), so these are spelled out directly, matching
/// feme-run.cpp's identical `std::vector<uint32_t>` trait.
template <> struct SequenceTraits<std::vector<uint32_t>> {
  static size_t size(IO &Io, std::vector<uint32_t> &Seq) { return Seq.size(); }
  static uint32_t &element(IO &Io, std::vector<uint32_t> &Seq, size_t Index) {
    if (Index >= Seq.size())
      Seq.resize(Index + 1);
    return Seq[Index];
  }
};
template <> struct SequenceTraits<std::vector<double>> {
  static size_t size(IO &Io, std::vector<double> &Seq) { return Seq.size(); }
  static double &element(IO &Io, std::vector<double> &Seq, size_t Index) {
    if (Index >= Seq.size())
      Seq.resize(Index + 1);
    return Seq[Index];
  }
};

template <> struct MappingTraits<feme::graphics::SceneAttachment> {
  static void mapping(IO &Io, feme::graphics::SceneAttachment &A) {
    Io.mapRequired("name", A.Name);
    Io.mapRequired("format", A.Format);
    std::vector<uint32_t> Extent;
    Io.mapRequired("extent", Extent);
    if (Extent.size() != 2) {
      Io.setError("attachment 'extent' must be [width, height]");
      return;
    }
    A.Width = Extent[0];
    A.Height = Extent[1];
    Io.mapOptional("clear", A.Clear);
  }
};

template <> struct MappingTraits<feme::graphics::SceneShaderStage> {
  static void mapping(IO &Io, feme::graphics::SceneShaderStage &Stage) {
    Io.mapRequired("module", Stage.Module);
    Io.mapOptional("entry", Stage.Entry, std::string("main"));
  }
};

template <> struct MappingTraits<feme::graphics::SceneDepthState> {
  static void mapping(IO &Io, feme::graphics::SceneDepthState &D) {
    Io.mapOptional("test", D.Test, std::string("none"));
    Io.mapOptional("write", D.Write, false);
  }
};

template <> struct MappingTraits<feme::graphics::ScenePipeline> {
  static void mapping(IO &Io, feme::graphics::ScenePipeline &P) {
    Io.mapRequired("vertex", P.Vertex);
    Io.mapRequired("fragment", P.Fragment);
    Io.mapOptional("cull", P.Cull, std::string("none"));
    Io.mapOptional("front-face", P.FrontFace, std::string("ccw"));
    Io.mapOptional("depth", P.Depth);
    Io.mapOptional("blend", P.Blend, std::string("replace"));
  }
};

template <> struct MappingTraits<feme::graphics::SceneViewport> {
  static void mapping(IO &Io, feme::graphics::SceneViewport &V) {
    Io.mapRequired("rect", V.Rect);
    Io.mapOptional("depth", V.Depth);
  }
};

template <> struct MappingTraits<feme::graphics::SceneVertexAttribute> {
  static void mapping(IO &Io, feme::graphics::SceneVertexAttribute &Attr) {
    Io.mapRequired("location", Attr.Location);
    Io.mapRequired("format", Attr.Format);
    Io.mapOptional("offset", Attr.Offset, 0u);
  }
};

template <> struct MappingTraits<feme::graphics::SceneVertexBuffer> {
  static void mapping(IO &Io, feme::graphics::SceneVertexBuffer &VB) {
    Io.mapRequired("binding", VB.Binding);
    Io.mapRequired("stride", VB.Stride);
    Io.mapOptional("attributes", VB.Attributes);
    Io.mapOptional("data", VB.Data);
  }
};

template <> struct MappingTraits<feme::graphics::SceneTexture> {
  static void mapping(IO &Io, feme::graphics::SceneTexture &Tex) {
    Io.mapRequired("index", Tex.Index);
    Io.mapRequired("file", Tex.File);
  }
};

template <> struct MappingTraits<feme::graphics::SceneDraw> {
  static void mapping(IO &Io, feme::graphics::SceneDraw &Draw) {
    Io.mapRequired("vertices", Draw.Vertices);
    Io.mapOptional("instances", Draw.Instances, 1u);
    Io.mapOptional("indexed", Draw.Indexed, false);
    Io.mapOptional("first-index", Draw.FirstIndex, 0u);
    Io.mapOptional("vertex-offset", Draw.VertexOffset, 0);
  }
};

template <> struct MappingTraits<feme::graphics::SceneIndexBuffer> {
  static void mapping(IO &Io, feme::graphics::SceneIndexBuffer &IB) {
    Io.mapOptional("format", IB.Format, std::string("uint32"));
    Io.mapOptional("data", IB.Data);
  }
};

template <> struct MappingTraits<feme::graphics::Scene> {
  static void mapping(IO &Io, feme::graphics::Scene &S) {
    Io.mapOptional("attachments", S.Attachments);
    Io.mapOptional("pipeline", S.Pipeline);
    Io.mapOptional("viewport", S.Viewport);
    Io.mapOptional("scissor", S.Scissor);
    Io.mapOptional("vertex-buffers", S.VertexBuffers);
    Io.mapOptional("index-buffer", S.IndexBuffer);
    Io.mapOptional("textures", S.Textures);
    Io.mapOptional("draws", S.Draws);
  }
};

} // namespace llvm::yaml

namespace feme::graphics {

Expected<Scene> parseScene(StringRef Text) {
  Scene S;
  yaml::Input Yin(Text);
  Yin >> S;
  if (Yin.error())
    return createStringError(Yin.error(), "could not parse scene");
  return S;
}

} // namespace feme::graphics
