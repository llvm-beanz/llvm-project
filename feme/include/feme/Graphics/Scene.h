//===- Scene.h - Textual scene description read ------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::parseScene, the reader for the scene
// YAML format `feme-render` consumes (roadmap R31, "FeMeGraphics skeleton"
// -- see feme/docs/Roadmap.md and "Textual scene and image fixtures" in
// feme/docs/Design.md): "extending the resource-heap schema `feme-run
// --heap` already accepts ... with the state a draw needs and nothing
// else".
//
// This is a plain data reader: it validates and normalizes the YAML into
// `Scene` (rejecting any state it does not recognize, per "a scene naming
// state the executor does not implement is an error at load time"), but
// implements no draw execution itself -- see feme-render.cpp for what this
// skeleton milestone actually does with a parsed `Scene` (build and clear
// attachments; a non-empty `draws` list is diagnosed as not yet
// implemented, roadmap R32).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_SCENE_H
#define FEME_GRAPHICS_SCENE_H

#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace feme::graphics {

/// One `attachments` entry.
struct SceneAttachment {
  std::string Name;
  std::string Format;
  uint32_t Width = 0;
  uint32_t Height = 0;
  std::vector<double> Clear;
};

/// One `pipeline.vertex`/`pipeline.fragment` entry: a shader module to load
/// (by path, see the file comment above) and the entry point within it.
struct SceneShaderStage {
  std::string Module;
  std::string Entry;
};

/// The `pipeline.depth` key: `{ test: <compare-op-or-"none">, write:
/// <bool> }` -- `test` is a `CompareOp` spelling (`never`, `less`, `equal`,
/// `lessequal`, `greater`, `notequal`, `greaterequal`, `always`), or `none`
/// to disable depth testing entirely (the default when the scene omits
/// `depth`, matching "state the scene does not mention takes the
/// executor's documented default").
struct SceneDepthState {
  std::string Test = "none";
  bool Write = false;
};

/// The `pipeline` key: fixed-function state plus the two stage modules,
/// spelled the way "Textual scene and image fixtures" in
/// feme/docs/Design.md shows (`cull: none`, `front-face: ccw`, `depth: {
/// test: less, write: true }`, `blend: replace`). Every field has the
/// documented default when the scene omits it.
struct ScenePipeline {
  SceneShaderStage Vertex;
  SceneShaderStage Fragment;
  std::string Cull = "none";
  std::string FrontFace = "ccw";
  SceneDepthState Depth;
  std::string Blend = "replace";
};

/// The `viewport` key.
struct SceneViewport {
  std::vector<double> Rect;
  std::vector<double> Depth;
};

/// One `vertex-buffers[].attributes[]` entry.
struct SceneVertexAttribute {
  uint32_t Location = 0;
  std::string Format;
  uint32_t Offset = 0;
};

/// One `vertex-buffers` entry.
struct SceneVertexBuffer {
  uint32_t Binding = 0;
  uint32_t Stride = 0;
  std::vector<SceneVertexAttribute> Attributes;
  std::vector<double> Data;
};

/// One `textures` entry: a resource-heap index bound to an image fixture
/// file (see "Images" in feme/docs/Design.md's "Textual scene and image
/// fixtures").
struct SceneTexture {
  uint32_t Index = 0;
  std::string File;
};

/// One `draws` entry. When `Indexed` is set, `Vertices` is interpreted as an
/// index count read from the scene's `index-buffer` starting at
/// `FirstIndex`, with `VertexOffset` added to each index read -- matching
/// Vulkan/Direct3D's own indexed-draw semantics (see `feme::graphics::
/// DrawCommand` in PreparedDraw.h, which this maps onto one-for-one).
struct SceneDraw {
  uint32_t Vertices = 0;
  uint32_t Instances = 1;
  bool Indexed = false;
  uint32_t FirstIndex = 0;
  int32_t VertexOffset = 0;
};

/// The `index-buffer` key: an index type (`uint16`/`uint32`) plus the flat
/// index data, matching `vertex-buffers[].data`'s own flat-array shape.
struct SceneIndexBuffer {
  std::string Format = "uint32";
  std::vector<uint32_t> Data;
};

/// The whole scene YAML file's contents (see the file comment above).
struct Scene {
  std::vector<SceneAttachment> Attachments;
  std::optional<ScenePipeline> Pipeline;
  std::optional<SceneViewport> Viewport;
  std::vector<uint32_t> Scissor;
  std::vector<SceneVertexBuffer> VertexBuffers;
  std::optional<SceneIndexBuffer> IndexBuffer;
  std::vector<SceneTexture> Textures;
  std::vector<SceneDraw> Draws;
  /// The `depth-attachment`/`stencil-attachment` keys: the `name` of an
  /// `attachments` entry to bind as the draw's depth/stencil attachment
  /// (roadmap R33), or empty if the scene binds neither.
  std::string DepthAttachment;
  std::string StencilAttachment;
};

/// Parses \p Text as a scene YAML file. Returns an `Error` for a malformed
/// document or an unrecognized key.
llvm::Expected<Scene> parseScene(llvm::StringRef Text);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_SCENE_H
