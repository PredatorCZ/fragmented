/*  CPC2GLTF
    Copyright(C) 2025 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.If not, see <https://www.gnu.org/licenses/>.
*/

#include "glm/gtx/quaternion.hpp"
#include "project.h"
#include "spike/app_context.hpp"
#include "spike/except.hpp"
#include "spike/gltf.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/master_printer.hpp"
#include "spike/type/pointer.hpp"
#include <cassert>
#include <set>

std::string_view filters[]{".cpc$", ".CPC$", "^ITM*.BIN$"};

static AppInfo_s appInfo{
    .header = CPC2GLTF_DESC " v" CPC2GLTF_VERSION ", " CPC2GLTF_COPYRIGHT
                            "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

template <class C> struct Array {
  es::PointerX86<C> items;
  uint32 numItems;

  C *begin() { return items; }
  C *end() { return begin() + numItems; }
};

struct Unk8 {
  es::PointerX86<char> unk0;
  int32 unk1[18];
  Array<char> unk2;
  Array<char> unk3;
  Array<char> unk4;
  Array<char> unk5;
  uint32 null[16];
};

struct Node {
  char name[0x80];
  uint32 null0[2];
  Array<Unk8> unk0;
  int32 parentIndex;
  Array<uint32> unk1;
  uint32 null1[3];
  float tm[16];
};

struct Unk11 {
  uint32 unk[9];
};

struct Unk12 {
  uint32 unk[2];
};

struct Unk0 {
  union {
    Array<Unk11> unk00;
    Array<Unk12> unk01;
  };
  int32 unk1;
  uint32 null[4];
};

static_assert(sizeof(Unk0) == 7 * 4);

struct VertexType {
  uint32 position : 2; // 2 = rgb32
  uint32 numWeights : 2;
  uint32 normalType : 2; // 1 = rgb32
  uint32 unkType0 : 2;

  uint32 texcoordType : 2; // 1 = rgb32
  uint32 unkType1 : 2;
  uint32 unkType2 : 2; // 2, 3 = ?
  uint32 unkType3 : 2;

  uint32 unkType4 : 2; // 1 = tbn?
  uint32 rest : 14;
};

static_assert(sizeof(VertexType) == 4);

struct Primitive {
  Array<char> vertices;
  Array<uint16> indices;
  es::PointerX86<uint16> unk1;
  VertexType vertexType;
  uint32 numWeights;
  uint32 vertexStride;
  uint32 null;
};

struct Mesh {
  Array<Primitive> primitives;
  uint32 unk[9];
};

struct SkinJoint {
  char unk[0x80];
  uint32 nodeIndex;
  float ibm[16];
  uint32 unk1[14];
};

struct MeshGroup {
  char name[0x80];
  uint32 nodeIndex;
  char null[0xa4];
  Array<Mesh> meshes;
  uint32 null0[4];
  Array<char> unk1;
  Array<char> unk2;
  Array<SkinJoint> skinJoints;
  Array<char> unk4;
  float bbox[6];
  uint32 null1[2];
};

struct Unk2 {};

struct Unk3 {};

struct Unk4 {
  uint32 unk[169];
};

struct Texture {
  uint32 unk;
  char path[0x190];
};

struct Unk5 {
  uint32 unk[49];
};

struct Unk6 {};

struct Unk7 {};

struct Model {
  Array<Node> nodes;
  Array<uint32> unk0;
  Array<Unk0> unk1;
  Array<Unk2> unk2;
  Array<Unk3> unk3;
  Array<MeshGroup> unk4;
  Array<Unk4> unk5;
  Array<Texture> textures;
  Array<Unk5> unk6;
  Array<Unk6> unk7;
  Array<Unk7> unk8;
};

struct AnimFrame {
  Vector4A16 elements[4];
};

struct AnimTrack {
  uint16 numTranslations;
  uint16 numRotations;
  uint32 null[3];
  AnimFrame frames[];
};

struct AnimTracks {
  uint32 numFrames;
  uint32 null[3];
  es::PointerX86<AnimTrack> tracks[];
};

struct AnimGroup {
  es::PointerX86<AnimTracks> anim[1][2];
};

struct AnimNode {
  uint8 unk0[6];
  uint8 trackGroup;
  uint8 unk2;
  float unk1[6];
};

struct AnimNodes {
  uint32 numNodes;
  uint32 null0[3];
  AnimNode nodes[];
};

#pragma pack(1)
struct TGA {
  enum class ImageType : uint8 {
    NoData,
    ColorMapped,
    TrueColor,
    Grayscale,
    RleColorMapped = 9,
    RleTrueColor,
    RleGrayscale,
  };

  uint8 idLen;
  uint8 colorMapType;
  ImageType imageType;
  uint16 firstEntryIndex;
  uint16 colorMapLength;
  uint8 colorMapEntrySize;
  uint16 xOrigin;
  uint16 yOrigin;
  uint16 width;
  uint16 height;
  uint8 pixelDepth;
  uint8 alphaDepth : 4;
  uint8 pixelOrder : 2;
};
#pragma pack()
static_assert(sizeof(TGA) == 0x12);

struct CPC {
  uint32 null;
  uint32 numAnimGroups;
  uint32 numModels;
  uint32 numImages;
  es::PointerX86<AnimNodes> animNodes;
  es::PointerX86<char> unk1;
  es::PointerX86<char> offsets[];

  Model *ModelAt(uint32 index) {
    return reinterpret_cast<Model *>(offsets[numAnimGroups + index].Get());
  }

  TGA *ImageAt(uint32 index) {
    return reinterpret_cast<TGA *>(
        offsets[numAnimGroups + index + numModels].Get());
  }

  AnimGroup *AnimGroupAt(uint32 index) {
    return reinterpret_cast<AnimGroup *>(offsets[index].Get());
  }
};

void Fixup(AnimGroup &item, uint32 numSlots, uint32 numNodes0,
           uint32 numNodes1) {
  const char *root = reinterpret_cast<const char *>(&item);
  std::set<void *> fixed;

  for (uint32 i = 0; i < numSlots; i++) {
    auto &tcks0 = item.anim[i][0];
    auto &tcks1 = item.anim[i][1];
    tcks0.Fixup(root);
    tcks1.Fixup(root);

    if (tcks0) {
      const char *subRoot = reinterpret_cast<const char *>(tcks0->tracks);
      for (uint32 n = 0; n < numNodes0; n++) {
        tcks0->tracks[n].Fixup(subRoot, fixed);
      }
    }

    if (tcks1) {
      const char *subRoot = reinterpret_cast<const char *>(tcks1->tracks);
      for (uint32 n = 0; n < numNodes1; n++) {
        tcks1->tracks[n].Fixup(subRoot, fixed);
      }
    }
  }
}

template <class C>
void Fixup(C &array, const char *&current, const char *root) {
  array.items.Reset(std::distance(root, current));
  array.items.Fixup(root);
  current = reinterpret_cast<const char *>(array.end());
}

template <>
void Fixup(Primitive &item, const char *&current, const char *root) {
  item.vertices.items.Reset(std::distance(root, current));
  item.vertices.items.Fixup(root);
  current += item.vertices.numItems * item.vertexStride;
  Fixup(item.indices, current, root);
  item.unk1.Reset(std::distance(root, current));
  item.unk1.Fixup(root);
  current += item.vertices.numItems * 2;
}

void Fixup(Model &item) {
  const char *root = reinterpret_cast<const char *>(&item);
  const char *current = root + sizeof(Model);
  Fixup(item.nodes, current, root);
  Fixup(item.unk0, current, root);
  Fixup(item.unk1, current, root);
  assert(item.unk2.numItems == 0);
  assert(item.unk3.numItems == 0);
  Fixup(item.unk4, current, root);
  Fixup(item.unk5, current, root);
  Fixup(item.textures, current, root);
  // Fixup(item.unk6, current, root);
  // Fixup(item.unk7, current, root);
  // Fixup(item.unk8, current, root);
  assert(item.unk6.numItems == 0);
  assert(item.unk7.numItems == 0);
  assert(item.unk8.numItems == 0);

  for (Node &b : item.nodes) {
    Fixup(b.unk0, current, root);
    Fixup(b.unk1, current, root);
  }

  for (Unk0 &b : item.unk1) {
    if (b.unk1 < 0) {
      Fixup(b.unk01, current, root);
    } else {
      Fixup(b.unk00, current, root);
    }
  }

  for (auto &g : item.unk4) {
    Fixup(g.meshes, current, root);
    assert(g.unk1.numItems == 0);
    assert(g.unk2.numItems == 0);
    // assert(g.unk4.numItems == 0);
  }

  for (auto &g : item.unk4) {
    for (auto &m : g.meshes) {
      assert(m.primitives.numItems == 1);
      Fixup(m.primitives, current, root);

      for (auto &p : m.primitives) {
        Fixup(p, current, root);
      }
    }
  }

  for (auto &g : item.unk4) {
    Fixup(g.skinJoints, current, root);
  }
}

void Fixup(CPC &item, const char *eof) {
  const char *root = reinterpret_cast<const char *>(&item.animNodes);
  std::set<Model *> fixed;
  const uint32 numOffsets =
      item.numModels + item.numAnimGroups + item.numImages;
  item.animNodes.Fixup(root);
  item.unk1.Fixup(root);
  assert(item.numImages < 2);

  for (uint32 i = 0; i < numOffsets; i++) {
    if (item.offsets[i] < eof) {
      item.offsets[i].Fixup(root);
    } else {
      item.offsets[i].Reset();
    }
  }

  for (uint32 i = 0; i < item.numModels; i++) {
    Model *mod = item.ModelAt(i);
    if (!fixed.contains(mod)) {
      fixed.emplace(mod);
      if (mod) {
        Fixup(*mod);
      }
    }
  }

  AnimNodes *nodes = item.animNodes;
  uint32 numNodes0 = 0;
  uint32 numNodes1 = 0;

  for (uint32 i = 0; i < nodes->numNodes; i++) {
    AnimNode &node = nodes->nodes[i];

    numNodes0 += node.trackGroup == 0;
    numNodes1 += node.trackGroup == 1;
  }

  if (item.numAnimGroups == 1) {
    Fixup(*item.AnimGroupAt(0), 4, numNodes0, numNodes1);
  } else {
    for (uint32 i = 0; i < item.numAnimGroups; i++) {
      static const uint32 NUM_SLOTS[]{28, 26, 50};
      Fixup(*item.AnimGroupAt(i), NUM_SLOTS[i], numNodes0, numNodes1);
    }
  }
}

const float CORSCALE = 0.1;
const es::Matrix44 CORMATS{{CORSCALE, 0, 0, 0},
                           {0, -CORSCALE, 0, 0},
                           {0, 0, -CORSCALE, 0},
                           {0, 0, 0, 1}};
const es::Matrix44 CORMAT{
    {1, 0, 0, 0}, {0, -1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 1}};

void Evaluate(AnimFrame &frame, Vector4A16 &out, float delta) {
  const Vector4A16 v1(delta, delta, delta, 1);
  const Vector4A16 v2(delta, delta, 1, 1);
  const Vector4A16 v3(delta, 1, 1, 1);
  const Vector4A16 vn = v1 * v2 * v3;
  es::Matrix44 mtx(frame.elements);
  mtx.TransposeFull();

  for (size_t i = 0; i < 4; i++) {
    Vector4A16 &v4 = mtx[i];
    out[i] = v4.Dot(vn);
  }
}

std::pair<uint32, std::vector<float>> MakeTimes(GLTFModel &main,
                                                size_t numFrames) {
  auto &str = main.LastStream();
  auto [acc, accIdx] = main.NewAccessor(str, 4);

  std::vector<float> samples{0};
  if (numFrames > 1) {
    samples = gltfutils::MakeSamples(60, (numFrames - 1) * (1 / 60.f));
  }

  acc.count = samples.size();
  acc.type = gltf::Accessor::Type::Scalar;
  acc.componentType = gltf::Accessor::ComponentType::Float;
  acc.min.push_back(0);
  acc.max.push_back(samples.back());
  str.wr.WriteContainer(samples);

  return {accIdx, samples};
}

void MakeAnimation(GLTFModel &main, size_t nodeIndex, AnimTrack &track,
                   uint32 timesAcc, const std::vector<float> &samples) {
  gltf::Animation &anim = main.animations.back();
  bool isRoot = std::find(main.scenes.front().nodes.begin(),
                          main.scenes.front().nodes.end(),
                          nodeIndex) != main.scenes.front().nodes.end();

  if (track.numRotations > 0) {
    gltf::Animation::Channel &chan = anim.channels.emplace_back();
    chan.sampler = anim.samplers.size();
    chan.target.path = "rotation";
    chan.target.node = nodeIndex;
    gltf::Animation::Sampler &sampl = anim.samplers.emplace_back();
    sampl.input = timesAcc;
    auto &str = main.LastStream();

    auto [acc, accIdx] = main.NewAccessor(str, 2);
    sampl.output = accIdx;
    acc.count = samples.size();
    acc.type = gltf::Accessor::Type::Vec4;
    acc.componentType = gltf::Accessor::ComponentType::Short;
    acc.normalized = true;

    for (float time : samples) {
      const float frame = time * 60;
      AnimFrame *found = track.frames + track.numTranslations;
      for (uint16 i = 0; i < track.numRotations; i++, found++) {
        if (found->elements[0].w > frame) {
          break;
        }
      }

      Vector4A16 value = found->elements[3];
      if (frame < track.frames[track.numTranslations + track.numRotations - 1]
                      .elements[0]
                      .w) {
        const float nextFrame = found[0].elements[0].w;
        const float thisFrame = found[-1].elements[0].w;
        const float delta = (frame - thisFrame) / (nextFrame - thisFrame);
        Evaluate(found[-1], value, delta);
      }

      glm::quat qt(glm::vec3(value.x, value.y, value.z));
      Vector4A16 quat(qt.x, qt.y, qt.z, qt.w);
      if (isRoot) {
        es::Matrix44 mtx(quat);
        mtx = CORMAT * mtx;
        quat = mtx.ToQuat();
      }
      quat.Normalize();
      quat *= 0x7fff;
      quat = Vector4A16(_mm_round_ps(quat._data, _MM_ROUND_NEAREST));
      str.wr.Write(quat.Convert<int16>());
    }
  }

  if (track.numTranslations > 0) {
    gltf::Animation::Channel &chan = anim.channels.emplace_back();
    chan.sampler = anim.samplers.size();
    chan.target.path = "translation";
    chan.target.node = nodeIndex;
    gltf::Animation::Sampler &sampl = anim.samplers.emplace_back();
    sampl.input = timesAcc;
    auto &str = main.LastStream();
    auto [acc, accIdx] = main.NewAccessor(str, 4);
    sampl.output = accIdx;
    acc.count = samples.size();
    acc.type = gltf::Accessor::Type::Vec3;
    acc.componentType = gltf::Accessor::ComponentType::Float;

    for (float time : samples) {
      const float frame = time * 60;
      AnimFrame *found = track.frames;
      for (uint16 i = 0; i < track.numTranslations; i++, found++) {
        if (found->elements[0].w > frame) {
          break;
        }
      }

      Vector4A16 value = found->elements[3];
      if (frame < track.frames[track.numTranslations - 1].elements[0].w) {
        const float nextFrame = found[0].elements[0].w;
        const float thisFrame = found[-1].elements[0].w;
        const float delta = (frame - thisFrame) / (nextFrame - thisFrame);
        Evaluate(found[-1], value, delta);
      }

      if (isRoot) {
        value = value * CORMAT;
      }
      value *= CORSCALE;
      str.wr.Write<Vector>(value);
    }
  }
}

struct TexStream : TexelOutput {
  GLTF &main;
  GLTFStream *str = nullptr;
  bool ignore = false;

  TexStream(GLTF &main_) : main{main_} {}

  void SendData(std::string_view data) override {
    if (str) [[likely]] {
      str->wr.WriteContainer(data);
    }
  }
  void NewFile(std::string path) override {
    if (ignore) {
      return;
    }

    str = &main.NewStream(path);
  }
};

size_t ExtractImage(const TGA &item, GLTF &main, AppContext *ctx) {
  TexStream texOut(main);
  NewTexelContextCreate tctx{
      .width = item.width,
      .height = item.height,
      .baseFormat =
          {
              .type = TexelInputFormatType::RGBA8,
          },
      .data = reinterpret_cast<const char *>(&item + 1),
      .texelOutput = &texOut,
      .formatOverride = TexelContextFormat::UPNG,
  };

  ctx->NewImage(tctx);

  gltf::Texture glTexture{};
  glTexture.source = main.textures.size();
  gltf::Image glImage{};
  glImage.mimeType = "image/png";
  glImage.name = "texture_" + std::to_string(glTexture.source);
  glImage.bufferView = texOut.str->slot;
  main.textures.emplace_back(glTexture);
  main.images.emplace_back(glImage);
  return glTexture.source;
}

struct AttributeTex : AttributeCodec {
  void Sample(uni::FormatCodec::fvec &, const char *, size_t) const override {}
  void Transform(uni::FormatCodec::fvec &in) const override {
    for (Vector4A16 &v : in) {
      v.y = 1 - v.y;
    }
  }
  bool CanSample() const override { return false; }
  bool CanTransform() const override { return true; }
  bool IsNormalized() const override { return false; }
} ATTR_TEX;

void SaveNodes(GLTFModel &main, Model *mod,
               std::map<std::string_view, size_t> &nodes) {
  for (auto &n : mod->nodes) {
    if (nodes.contains(n.name)) {
      continue;
    }

    const size_t nodeIndex = main.nodes.size();
    gltf::Node &glNode = main.nodes.emplace_back();
    glNode.name = n.name;
    es::Matrix44 mtx;
    if (n.tm[15]) {
      memcpy(&mtx, n.tm, sizeof(mtx));
    }
    if (n.parentIndex < 0) {
      mtx *= CORMAT;
    }
    Vector4A16 rotation, translation, scale;
    mtx.Decompose(translation, rotation, scale);
    translation *= CORSCALE;
    memcpy(glNode.translation.data(), &translation, 12);
    memcpy(glNode.rotation.data(), &rotation, 16);
    memcpy(glNode.scale.data(), &scale, 12);

    if (n.parentIndex > -1) {
      const size_t idx = nodes.at(mod->nodes.begin()[n.parentIndex].name);
      main.nodes.at(idx).children.push_back(nodeIndex);
    } else {
      main.scenes.front().nodes.push_back(nodeIndex);
    }

    nodes.emplace(n.name, nodeIndex);
  }
}

void SaveModel(GLTFModel &main, Model *mod,
               std::map<std::string_view, size_t> &nodes) {
  for (auto &g : mod->unk4) {
    int32 skinIndex = -1;
    if (g.skinJoints.numItems > 0) {
      skinIndex = main.skins.size();
      gltf::Skin &glSkin = main.skins.emplace_back();

      glSkin.joints.emplace_back(nodes.at("root"));

      for (auto &j : g.skinJoints) {
        const size_t nodeIndex = nodes.at(mod->nodes.begin()[j.nodeIndex].name);
        glSkin.joints.emplace_back(nodeIndex);
      }

      auto &str = main.SkinStream();
      auto [acc, accIdx] = main.NewAccessor(str, 16);
      acc.type = gltf::Accessor::Type::Mat4;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = glSkin.joints.size();
      glSkin.inverseBindMatrices = accIdx;

      str.wr.Write(CORMAT);

      for (auto &j : g.skinJoints) {
        es::Matrix44 mtx;
        memcpy(&mtx, j.ibm, sizeof(mtx));
        mtx = -(CORMAT * -mtx);
        mtx.r4() *= CORSCALE;
        mtx.r4().W = 1;
        str.wr.Write(mtx);
      }
    }

    for (auto &m : g.meshes) {
      const size_t mIndex = main.nodes.size();
      const size_t pIndex = nodes.at(mod->nodes.begin()[g.nodeIndex].name);
      main.nodes.at(pIndex).children.push_back(mIndex);
      // main.scenes.front().nodes.emplace_back(mIndex);
      gltf::Node &glmNode = main.nodes.emplace_back();
      glmNode.name = g.name;
      glmNode.mesh = main.meshes.size();
      glmNode.rotation = {1, 0, 0, 0};
      glmNode.skin = skinIndex;
      gltf::Mesh &glMesh = main.meshes.emplace_back();

      for (auto &p : m.primitives) {
        gltf::Primitive &prim = glMesh.primitives.emplace_back();
        prim.material = 0;

        static const uni::DataType WTYPES[]{
            uni::DataType::CUSTOM,       uni::DataType::R32,
            uni::DataType::R32G32,       uni::DataType::R32G32B32,
            uni::DataType::R32G32B32A32,
        };

        assert(p.numWeights < 5);

        std::vector<Attribute> attrs{
            Attribute{
                .type = uni::DataType::R32G32B32,
                .format = uni::FormatType::FLOAT,
                .usage = AttributeType::Position,
            },
            Attribute{
                .type = uni::DataType::R32G32B32,
                .format = uni::FormatType::FLOAT,
                .usage = AttributeType::Normal,
            },
            Attribute{
                .type = uni::DataType::R32G32,
                .format = uni::FormatType::FLOAT,
                .usage = AttributeType::TextureCoordiante,
                .customCodec = &ATTR_TEX,
            },
            Attribute{
                .type = uni::DataType::R32,
                .format = uni::FormatType::FLOAT,
                .usage = AttributeType::Undefined,
            },
            /* TBN mat3x3
              Attribute{
                  .type = uni::DataType::R32G32B32,
                  .format = uni::FormatType::FLOAT,
                  .usage = AttributeType::Tangent,
              },
              Attribute{
                  .type = uni::DataType::R32G32B32,
                  .format = uni::FormatType::FLOAT,
                  .usage = AttributeType::BiNormal,
              },
              Attribute{
                  .type = uni::DataType::R32G32B32,
                  .format = uni::FormatType::FLOAT,
                  .usage = AttributeType::Normal,
              },
              */
        };

        if (p.numWeights > 0) {
          attrs.insert(std::next(attrs.begin(), 1),
                       Attribute{
                           .type = WTYPES[p.numWeights],
                           .format = uni::FormatType::FLOAT,
                           .usage = AttributeType::BoneWeights,
                       });
        }

        if (p.vertexType.numWeights > 0) {
          attrs.insert(std::next(attrs.begin(), (p.numWeights > 0) + 1),
                       Attribute{
                           .type = uni::DataType::R8G8B8A8,
                           .format = uni::FormatType::UINT,
                           .usage = AttributeType::BoneIndices,
                       });
        }

        prim.attributes = main.SaveVertices(
            p.vertices.items, p.vertices.numItems, attrs, p.vertexStride);
        prim.indices =
            main.SaveIndices(p.indices.items, p.indices.numItems).accessorIndex;
      }
    }
  }
}

void SaveCPC(GLTFModel &main, AppContext *ctx) {
  std::string buffer = ctx->GetBuffer();
  CPC *hdr = reinterpret_cast<CPC *>(buffer.data());
  Fixup(*hdr, &buffer.back());
  std::map<std::string_view, size_t> nodes;
  main.materials.emplace_back().pbrMetallicRoughness.baseColorTexture.index = 0;

  for (uint32 i = 0; i < hdr->numModels; i++) {
    Model *mod = hdr->ModelAt(i);

    if (!mod) {
      continue;
    }

    SaveNodes(main, mod, nodes);
  }

  for (uint32 i = 0; i < hdr->numModels; i++) {
    Model *mod = hdr->ModelAt(i);

    if (!mod) {
      continue;
    }

    SaveModel(main, mod, nodes);
  }

  Model *mod = hdr->ModelAt(0);
  if (mod) {
    AnimNodes *aNodes = hdr->animNodes;
    assert(aNodes->numNodes == mod->nodes.numItems);
    main.NewStream("animations");

    std::vector<uint32> nodes0;
    std::vector<uint32> nodes1;

    for (uint32 i = 0; i < aNodes->numNodes; i++) {
      AnimNode &node = aNodes->nodes[i];
      const size_t nodeIndex = nodes.at(mod->nodes.begin()[i].name);

      if (node.trackGroup == 0) {
        nodes0.push_back(nodeIndex);
      } else if (node.trackGroup == 1) {
        nodes1.push_back(nodeIndex);
      }
    }

    for (uint32 g = 0; g < hdr->numAnimGroups; g++) {
      AnimGroup *group = hdr->AnimGroupAt(g);
      static const uint32 NUM_SLOTS[]{28, 26, 50};
      const uint32 numSlots = hdr->numAnimGroups == 1 ? 4 : NUM_SLOTS[g];

      for (uint32 s = 0; s < numSlots; s++) {
        gltf::Animation *glAnim = nullptr;
        if (AnimTracks *group0 = group->anim[s][0]; group0) {
          if (!glAnim) {
            glAnim = &main.animations.emplace_back();
          }
          auto [timesAcc, samples] = MakeTimes(main, group0->numFrames);
          for (uint32 t = 0; auto &n : nodes0) {
            AnimTrack *tck = group0->tracks[t];
            if (tck) {
              MakeAnimation(main, n, *tck, timesAcc, samples);
            }
            t++;
          }
        }

        if (AnimTracks *group1 = group->anim[s][1]; group1) {
          if (!glAnim) {
            glAnim = &main.animations.emplace_back();
          }
          auto [timesAcc, samples] = MakeTimes(main, group1->numFrames);
          for (uint32 t = 0; auto &n : nodes1) {
            AnimTrack *tck = group1->tracks[t];
            if (tck) {
              MakeAnimation(main, n, *tck, timesAcc, samples);
            }
            t++;
          }
        }

        if (glAnim) {
          if (glAnim->channels.empty()) {
            main.animations.pop_back();
          } else {
            glAnim->name =
                "motion_" + std::to_string(g) + "_" + std::to_string(s);
          }
        }
      }
    }
  }

  for (uint32 i = 0; i < hdr->numImages; i++) {
    if (TGA *img = hdr->ImageAt(i); img) {
      ExtractImage(*img, main, ctx);
    }
  }
}

void SaveItem(GLTFModel &main, AppContext *ctx) {
  std::string buffer = ctx->GetBuffer();
  uint32 *hdr = reinterpret_cast<uint32 *>(buffer.data());
  uint32 *itemsBegin = hdr + 1;
  uint32 *itemsEnd = hdr + *itemsBegin / 4;
  auto &mat = main.materials.emplace_back();
  mat.name = "item";

  std::map<std::string_view, size_t> nodes;

  for (uint32 *offset = hdr + 1; offset < itemsEnd; offset++) {
    if (!*offset) {
      continue;
    }

    Model *mod = reinterpret_cast<Model *>(buffer.data() + *offset);

    Fixup(*mod);
    SaveNodes(main, mod, nodes);
  }

  for (uint32 *offset = hdr + 1; offset < itemsEnd; offset++) {
    if (!*offset) {
      continue;
    }

    Model *mod = reinterpret_cast<Model *>(buffer.data() + *offset);

    SaveModel(main, mod, nodes);
  }

  std::string imgPath(ctx->workingFile.GetFolder());
  try {
    AppContextStream imgStr = ctx->RequestFile(imgPath + "item.tga");
    BinReaderRef ird(*imgStr.Get());
    std::string buff;
    ird.ReadContainer(buff, ird.GetSize());
    ExtractImage(reinterpret_cast<TGA&>(buff.front()), main, ctx);
    mat.pbrMetallicRoughness.baseColorTexture.index = 0;
  } catch (es::FileNotFoundError &) {
    PrintWarning("item.tga not found, skipped");
  }
}

void AppProcessFile(AppContext *ctx) {
  GLTFModel main;
  main.transform = CORMATS;

  std::string_view fileName = ctx->workingFile.GetFilenameExt();

  if (fileName.starts_with("ITM") && fileName.ends_with(".BIN")) {
    SaveItem(main, ctx);
  } else {
    SaveCPC(main, ctx);
  }

  BinWritterRef wr(ctx->NewFile(ctx->workingFile.ChangeExtension2("glb")).str);
  main.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
}
