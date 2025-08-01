/*  BST2GLTF
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

#include "project.h"
#include "spike/app_context.hpp"
#include "spike/crypto//crc32.hpp"
#include "spike/except.hpp"
#include "spike/gltf.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/type/matrix44.hpp"
#include "spike/type/vectors.hpp"
#include <algorithm>
#include <cassert>
#include <memory>
#include <set>
#include <variant>
#include <map>

constexpr bool ANIMATED = true;

std::string_view filters[]{
    ".an$",
    ".sgh$",
};

std::string_view controlFilters[]{
    ".bst$",
};

static AppInfo_s appInfo{
    .header = BST2GLTF_DESC " v" BST2GLTF_VERSION ", " BST2GLTF_COPYRIGHT
                            "Lukas Cone",
    .filters = filters,
    .batchControlFilters = controlFilters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

std::string ToLower(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](char c) { return std::tolower(c); });
  return name;
}

struct GLTFAni : GLTFModel {
  using GLTFModel::GLTFModel;

  GLTFStream &AnimStream() {
    if (aniStream < 0) {
      auto &newStream = NewStream("anims");
      aniStream = newStream.slot;
      return newStream;
    }
    return Stream(aniStream);
  }

  int32 StaticTime() {
    if (staticTimes > -1) {
      return staticTimes;
    }

    auto &str = AnimStream();
    auto [acc, accId] = NewAccessor(str, 4);
    acc.type = gltf::Accessor::Type::Scalar;
    acc.componentType = gltf::Accessor::ComponentType::Float;
    acc.count = 1;
    staticTimes = accId;

    str.wr.Write(0);

    return staticTimes;
  }

  int32 FindNode(std::string name) {
    int32 boneIndex = -1;
    for (uint32 n = 0; n < nodes.size(); n++) {
      std::string glNodeName = nodes.at(n).name;
      if (glNodeName == name) {
        boneIndex = n;
        break;
      }
    }

    return boneIndex;
  }

  std::map<std::string_view, size_t> pathToMesh;
  std::map<std::string_view, size_t> pathToSkin;
  es::Matrix44 npcTm;
  es::Matrix44 meshTm;

private:
  int32 aniStream = -1;
  int32 staticTimes = -1;
};

uint32 HashClassName(std::string_view data) {
  return ~crc32b(0, data.data(), data.size()) ^ 0xFABCDEF7;
}

void CheckClass(BinReaderRef rd, uint32 clsId) {
  uint32 id;
  rd.Read(id);

  if (clsId != id) {
    throw std::runtime_error("Invalid class check at: " +
                             std::to_string(rd.Tell() - 4));
  }
};

std::string ReadString(BinReaderRef rd) {
  static const uint32 CLSID = HashClassName("string");
  CheckClass(rd, CLSID);
  std::string retVal;
  rd.ReadContainer(retVal);
  return retVal;
}

template <class Ty, class Cb>
void ReadVector(BinReaderRef rd, std::vector<Ty> &vec, Cb &&cb) {
  static const uint32 CLSID = HashClassName("vector");
  CheckClass(rd, CLSID);
  rd.ReadContainerLambda(vec, cb);
}

template <class Ty> std::vector<Ty> ReadVector(BinReaderRef rd) {
  static const uint32 CLSID = HashClassName("vector");
  CheckClass(rd, CLSID);
  std::vector<Ty> retVal;
  rd.ReadContainer(retVal);
  return retVal;
}

void CheckClassStart(BinReaderRef rd) {
  int32 check;
  rd.Read(check);

  if (check != -1) {
    throw std::runtime_error("Polymorphic class check failed at: " +
                             std::to_string(rd.Tell() - 4));
  }
}

uint32 ReadHeader(BinReaderRef rd) {
  char id[8];
  rd.Read(id);

  if (std::string_view(id, 8) != "ARTWORKS") {
    throw es::InvalidHeaderError(std::string_view(id, 8));
  }

  uint8 unk0;
  rd.Read(unk0);
  uint32 version;
  rd.Read(version);

  if (version != 3 && version != 2) {
    throw es::InvalidVersionError(version);
  }

  uint32 unk1;
  rd.Read(unk1);
  std::string desc = ReadString(rd);
  return version;
}

struct Streamable;

using Streamables = std::vector<std::unique_ptr<Streamable>>;

struct Streamable {
  virtual ~Streamable() = default;
  virtual uint32 ClassId() const = 0;
  virtual void Read(BinReaderRef rd) = 0;
  virtual void Link(Streamables & /*classes*/) {}
  virtual void ToGltf(GLTFAni &) {}

  template <class Ty> Ty *As() {
    if (Ty::CLSID != ClassId()) {
      throw std::bad_cast();
    }

    return static_cast<Ty *>(this);
  }
};

struct PointerLocator {
  uint32 index;
  uint32 classId;

  void Read(BinReaderRef rd) {
    rd.Read(index);
    if (index) {
      rd.Read(classId);
    }
  }
};

struct Pointer : std::variant<PointerLocator, Streamable *> {
  using Base = std::variant<PointerLocator, Streamable *>;
  using Base::operator=;

  Streamable *operator->() { return std::get<Streamable *>(*this); }
  Streamable &operator*() { return *std::get<Streamable *>(*this); }
};

void Link(Pointer &ptr, Streamables &classes) {
  PointerLocator &locator = std::get<PointerLocator>(ptr);
  if (locator.index > 0) {
    Streamable *cls = classes.at(locator.index - 1).get();
    if (locator.classId != cls->ClassId()) {
      throw std::runtime_error("Polymorphic class check failed");
    }

    ptr = cls;
  } else {
    ptr = nullptr;
  }
}

template <class Ty> t_Vector4<Ty> SwapQuat(const t_Vector4<Ty> &in) {
  return {in.y, in.z, in.w, in.x};
}

struct StaticPositionKeyFrameData : Streamable {
  static inline const uint32 CLSID =
      HashClassName("StaticPositionKeyFrameData");

  struct KeyFrame {
    float time;
    Vector data;
  };

  KeyFrame value;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    rd.Read(value);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    auto &str = main.AnimStream();
    {
      anim.channels.back().target.path = "translation";
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(value.data);
    }
  }
};

struct StaticScaleKeyFrameData : Streamable {
  static inline const uint32 CLSID = HashClassName("StaticScaleKeyFrameData");

  struct KeyFrame {
    float time;
    Vector data;
  };

  KeyFrame value;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    rd.Read(value);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    auto &str = main.AnimStream();
    {
      anim.channels.back().target.path = "scale";
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(value.data);
    }
  }
};

struct StaticScaleAndPositionKeyFrameData : Streamable {
  static inline const uint32 CLSID =
      HashClassName("StaticScaleAndPositionKeyFrameData");

  struct KeyFrame {
    float time;
    Vector scale;
    Vector position;
  };

  KeyFrame value;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    rd.Read(value);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    auto &str = main.AnimStream();
    {
      anim.channels.back().target.path = "translation";
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(value.position);
    }

    {
      auto nchan = anim.channels.back();
      nchan.target.path = "scale";
      nchan.sampler = anim.samplers.size();
      anim.channels.emplace_back(nchan);
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(value.scale);
    }
  }
};

struct StaticRotationKeyFrameData : Streamable {
  static inline const uint32 CLSID =
      HashClassName("StaticRotationKeyFrameData");

  struct KeyFrame {
    float time;
    Vector4 rotation;
  };

  KeyFrame value;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    rd.Read(value);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    auto &str = main.AnimStream();
    {
      anim.channels.back().target.path = "rotation";
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec4;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(SwapQuat(value.rotation));
    }
  }
};

struct StaticKeyFrameData : Streamable {
  static inline const uint32 CLSID = HashClassName("StaticKeyFrameData");

  struct KeyFrame {
    float time;
    Vector position;
    Vector4 rotation;
    Vector scale;
  };

  KeyFrame value;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    rd.Read(value);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    auto &str = main.AnimStream();
    {
      anim.channels.back().target.path = "rotation";
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec4;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(SwapQuat(value.rotation));
    }

    {
      auto nchan = anim.channels.back();
      nchan.target.path = "translation";
      nchan.sampler = anim.samplers.size();
      anim.channels.emplace_back(nchan);
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(value.position);
    }

    {
      auto nchan = anim.channels.back();
      nchan.target.path = "scale";
      nchan.sampler = anim.samplers.size();
      anim.channels.emplace_back(nchan);
      auto &sampl = anim.samplers.emplace_back();
      sampl.input = main.StaticTime();
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = 1;
      sampl.output = accId;
      str.wr.Write(value.scale);
    }
  }
};

struct CompRotationKeyFrameData : Streamable {
  static inline const uint32 CLSID = HashClassName("CompRotationKeyFrameData");

  struct KeyFrame {
    uint16 frame;
    SVector4 data;
  };

  std::vector<KeyFrame> values;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 3 || version == 2 || version == 1);
    values = ReadVector<KeyFrame>(rd);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    anim.channels.back().target.path = "rotation";
    auto &sampl = anim.samplers.emplace_back();
    auto &str = main.AnimStream();
    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Scalar;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.input = accId;

      for (auto &v : values) {
        str.wr.Write(v.frame * 0.0091731902f);
      }
    }

    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec4;
      acc.componentType = gltf::Accessor::ComponentType::Short;
      acc.normalized = true;
      acc.count = values.size();
      sampl.output = accId;

      for (auto &v : values) {
        IVector4 tmp(v.data.Convert<int>());
        tmp -= 0x7fff;
        SVector4 vec(tmp.Convert<int16>());
        str.wr.Write(SwapQuat(vec));
      }
    }
  }
};

struct RotationKeyFrameData : Streamable {
  static inline const uint32 CLSID = HashClassName("RotationKeyFrameData");

  struct KeyFrame {
    float time;
    Vector4 data;
  };

  std::vector<KeyFrame> values;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    values = ReadVector<KeyFrame>(rd);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    anim.channels.back().target.path = "rotation";
    auto &sampl = anim.samplers.emplace_back();
    auto &str = main.AnimStream();
    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Scalar;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.input = accId;

      for (auto &v : values) {
        str.wr.Write(v.time);
      }
    }

    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec4;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.output = accId;

      for (auto &v : values) {
        str.wr.Write(SwapQuat(v.data));
      }
    }
  }
};

struct PositionKeyFrameData : Streamable {
  static inline const uint32 CLSID = HashClassName("PositionKeyFrameData");

  struct KeyFrame {
    float time;
    Vector data;
  };

  std::vector<KeyFrame> values;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    values = ReadVector<KeyFrame>(rd);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    anim.channels.back().target.path = "translation";
    auto &sampl = anim.samplers.emplace_back();
    auto &str = main.AnimStream();
    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Scalar;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.input = accId;

      for (auto &v : values) {
        str.wr.Write(v.time);
      }
    }

    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.output = accId;

      for (auto &v : values) {
        str.wr.Write(v.data);
      }
    }
  }
};

struct ScaleKeyFrameData : PositionKeyFrameData {
  static inline const uint32 CLSID = HashClassName("ScaleKeyFrameData");

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    values = ReadVector<KeyFrame>(rd);
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.back();
    anim.channels.back().target.path = "scale";
    auto &sampl = anim.samplers.emplace_back();
    auto &str = main.AnimStream();
    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Scalar;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.input = accId;

      for (auto &v : values) {
        str.wr.Write(v.time);
      }
    }

    {
      auto [acc, accId] = main.NewAccessor(str, 4);
      acc.type = gltf::Accessor::Type::Vec3;
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.count = values.size();
      sampl.output = accId;

      for (auto &v : values) {
        str.wr.Write(v.data);
      }
    }
  }
};

struct AnimatedBone {
  std::vector<Pointer> tracks;

  void Read(BinReaderRef rd) {
    ReadVector(rd, tracks, [](BinReaderRef rd, auto &track) {
      PointerLocator ptr;
      rd.Read(ptr);
      track = ptr;
    });
  }
};

struct UnkData {
  float unk0;
  uint32 unk1;
  uint32 null0;
  uint16 unk2;

  void Read(BinReaderRef rd) {
    rd.Read(unk0);
    rd.Read(unk1);
    rd.Read(null0);
    rd.Read(unk2);
  }
};

struct Animation : Streamable {
  static inline const uint32 CLSID = HashClassName("Animation");
  std::string name;
  std::vector<std::string> boneNames;
  std::vector<AnimatedBone> bones;
  std::vector<UnkData> unks;
  std::string transitionFrom;
  float duration;
  float unk3;
  float frameDuration;
  uint32 unk4;
  uint16 unk1;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);

    if (version > 4) {
      unks = ReadVector<UnkData>(rd);
    }

    name = ReadString(rd);
    ReadVector(rd, boneNames,
               [](BinReaderRef rd, auto &item) { item = ReadString(rd); });
    bool isLooping;
    rd.Read(isLooping); // is looping?
    rd.Read(duration);
    bones = ReadVector<AnimatedBone>(rd);
    transitionFrom = ReadString(rd);
    rd.Read(unk3);

    if (version == 3) {
      ReadVector(rd, boneNames,
                 [](BinReaderRef rd, auto &item) { item = ReadString(rd); });
      rd.Read(unk1);
      rd.Read(unk4);
    } else {
      rd.Read(unk1);
      rd.Read(frameDuration);
    }
  }

  void Link(std::vector<std::unique_ptr<Streamable>> &classes) override {
    for (auto &c : bones) {
      for (auto &t : c.tracks) {
        ::Link(t, classes);
      }
    }
  }

  void ToGltf(GLTFAni &main) override {
    auto &anim = main.animations.emplace_back();
    anim.name = name;

    for (uint32 b = 0; b < boneNames.size(); b++) {
      int32 boneIndex = -1;
      for (uint32 n = 0; n < main.nodes.size(); n++) {
        std::string glNodeName = ToLower(main.nodes.at(n).name);
        if (glNodeName == boneNames.at(b)) {
          boneIndex = n;
          break;
        }
      }

      for (auto &t : bones.at(b).tracks) {
        auto &chan = anim.channels.emplace_back();
        chan.sampler = anim.samplers.size();
        chan.target.node = boneIndex;
        t->ToGltf(main);
      }
    }
  }
};

struct CNode : Streamable {
  static inline const uint32 CLSID = HashClassName("CNode");
  std::string name;
  Vector unk0;
  es::Matrix44 tm;
  std::vector<Pointer> children;
  Pointer parent;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 3);

    rd.Read(unk0);
    rd.Read(tm);
    name = ToLower(ReadString(rd));

    ReadVector(rd, children, [](BinReaderRef rd, auto &node) {
      PointerLocator ptr;
      rd.Read(ptr);
      node = ptr;
    });

    PointerLocator parent_;
    rd.Read(parent_);
    parent = parent_;
  }

  void Link(std::vector<std::unique_ptr<Streamable>> &classes) override {
    for (auto &c : children) {
      ::Link(c, classes);
    }

    ::Link(parent, classes);
  }

  void WalkNodes(GLTFAni &main, int32 parentNode) {
    int32 foundNode = main.FindNode(name);

    if (foundNode < 0) {
      foundNode = main.nodes.size();
      gltf::Node &glNode = main.nodes.emplace_back();
      glNode.name = name;
      memcpy(glNode.matrix.data(), &tm, sizeof(tm));

      if (parentNode > -1) {
        main.nodes.at(parentNode).children.push_back(foundNode);
      } else {
        main.scenes.front().nodes.push_back(foundNode);
      }

      if (name == "npc") {
        main.npcTm = tm;
      }
    }

    for (auto &c : children) {
      static_cast<CNode *>(std::get<Streamable *>(c))
          ->WalkNodes(main, foundNode);
    }
  }

  void ToGltf(GLTFAni &main) override { WalkNodes(main, -1); }
};

struct MeshNodeMeshLod {
  std::string meshPath;
  float unk1;

  void Read(BinReaderRef rd) {
    meshPath = ReadString(rd);
    rd.Read(unk1);
  }
};

struct MeshNode : CNode {
  static inline const uint32 CLSID = HashClassName("MeshNode");
  std::vector<MeshNodeMeshLod> lods;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    CNode::Read(rd);
    uint8 version;
    rd.Read(version);
    rd.ReadContainer(lods);
  }

  void ToGltf(GLTFAni &main) override {
    CNode::ToGltf(main);
    const int32 nodeIndex = main.FindNode(name);
    assert(nodeIndex > -1);

    for (auto &l : lods) {
      auto &glNode = main.nodes.at(nodeIndex);
      if (glNode.mesh < 0) {
        glNode.mesh = main.pathToMesh.at(l.meshPath);

        if (ANIMATED && main.pathToSkin.contains(l.meshPath)) {
          glNode.skin = main.pathToSkin.at(l.meshPath);
        }
      } else {
        glNode.children.emplace_back(main.nodes.size());
        auto &node = main.nodes.emplace_back();
        node.mesh = main.pathToMesh.at(l.meshPath);

        if (ANIMATED && main.pathToSkin.contains(l.meshPath)) {
          node.skin = main.pathToSkin.at(l.meshPath);
        }
      }
    }
  }
};

struct CSkinMeshNode : MeshNode {
  static inline const uint32 CLSID = HashClassName("CSkinMeshNode");

  uint32 ClassId() const override { return CLSID; }

  void ToGltf(GLTFAni &main) override { MeshNode::ToGltf(main); }
};

struct Material : Streamable {
  static inline const uint32 CLSID = HashClassName("Material");

  uint32 ClassId() const override { return CLSID; }

  float data[17];

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);

    rd.Read(data);
  }
};

struct BoundingSphere : Streamable {
  static inline const uint32 CLSID = HashClassName("BoundingSphere");

  uint32 ClassId() const override { return CLSID; }

  Vector center;
  float radius;

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);

    rd.Read(center);
    rd.Read(radius);
  }
};

struct BoundingBox : Streamable {
  static inline const uint32 CLSID = HashClassName("BoundingBox");

  uint32 ClassId() const override { return CLSID; }

  Vector min;
  Vector max;

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);

    rd.Read(min);
    rd.Read(max);
  }
};

struct RenderState : Streamable {
  static inline const uint32 CLSID = HashClassName("RenderState");

  uint32 ClassId() const override { return CLSID; }

  std::string name;
  uint8 unk2[17];
  Pointer material;

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 5 || version == 4);

    rd.Skip(version == 4 ? 45 : 49);
    name = ReadString(rd);
    rd.Read(unk2);
    PointerLocator ptr;
    rd.Read(ptr);
    material = ptr;
  }

  void Link(std::vector<std::unique_ptr<Streamable>> &classes) override {
    ::Link(material, classes);
  }
};

struct GenMesh : Streamable {
  static inline const uint32 CLSID = HashClassName("GenMesh");

  uint32 ClassId() const override { return CLSID; }

  uint32 unk0;
  uint32 unk1;
  std::vector<Pointer> groups;
  Pointer boundingSphere;
  Pointer boundingBox;

  std::string unk2;

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 2 || version == 1);

    if (version == 1) {
      unk2 = ReadString(rd);
      uint8 subVersion;
      rd.Read(subVersion);
      assert(subVersion == 4 || subVersion == 3);

      if (subVersion > 3) {
        rd.Read(unk0);
        rd.Read(unk1);
      }
    } else {
      rd.Read(unk0);
      rd.Read(unk1);
    }

    ReadVector(rd, groups, [](BinReaderRef rd, auto &node) {
      PointerLocator ptr;
      rd.Read(ptr);
      node = ptr;
    });

    if (version == 1) {
      uint32 null0;
      rd.Read(null0);
      assert(null0 == 0);
    }

    PointerLocator ptr;
    rd.Read(ptr);
    boundingSphere = ptr;
    rd.Read(ptr);
    boundingBox = ptr;
  }

  void Link(std::vector<std::unique_ptr<Streamable>> &classes) override {
    for (auto &g : groups) {
      ::Link(g, classes);
    }
    ::Link(boundingSphere, classes);
    ::Link(boundingBox, classes);
  }

  void ToGltf(GLTFAni &main) override {
    main.meshes.emplace_back();

    for (auto &g : groups) {
      g->ToGltf(main);
    }
  }
};

struct GenSkinMesh : GenMesh {
  static inline const uint32 CLSID = HashClassName("GenSkinMesh");

  uint32 ClassId() const override { return CLSID; }

  std::vector<std::string> boneNames;
  std::vector<es::Matrix44> ibms;

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);

    if (version == 2) {
      ibms = ReadVector<es::Matrix44>(rd);
      ReadVector(rd, groups, [](BinReaderRef rd, auto &node) {
        PointerLocator ptr;
        rd.Read(ptr);
        node = ptr;
      });
      ReadVector(rd, boneNames, [](BinReaderRef rd, auto &item) {
        item = ToLower(ReadString(rd));
      });
      PointerLocator ptr;
      rd.Read(ptr);
      boundingSphere = ptr;
      rd.Read(ptr);
      boundingBox = ptr;
    } else {
      assert(version == 3);
      GenMesh::Read(rd);
      ReadVector(rd, boneNames, [](BinReaderRef rd, auto &item) {
        item = ToLower(ReadString(rd));
      });
      ibms = ReadVector<es::Matrix44>(rd);
    }
  }

  void ToGltf(GLTFAni &main) override {
    GenMesh::ToGltf(main);
    auto &glSkin = main.skins.emplace_back();

    for (uint32 i = 0; i < boneNames.size(); i++) {
      for (uint32 n = 0; n < main.nodes.size(); n++) {
        if (main.nodes.at(n).name == boneNames.at(i)) {
          glSkin.joints.emplace_back(n);
          break;
        }
      }
    }

    assert(glSkin.joints.size() == boneNames.size());

    auto &str = main.SkinStream();
    auto [acc, accId] = main.NewAccessor(str, 16);
    acc.count = ibms.size();
    acc.type = gltf::Accessor::Type::Mat4;
    acc.componentType = gltf::Accessor::ComponentType::Float;
    glSkin.inverseBindMatrices = accId;
    for (auto t : ibms) {
      t = -main.meshTm * t;
      str.wr.Write(-t);
    }
  }
};

struct GenGroup : Streamable {
  static inline const uint32 CLSID = HashClassName("GenGroup");

  uint32 ClassId() const override { return CLSID; }

  struct Vertex9 {
    uint32 unk0;
    Vector position;
    Vector normal;
    Vector2 uv;
  };

  struct Vertex11 {
    uint32 unk0;
    Vector position;
    Vector normal;
    uint32 color;
    Vector2 uv;
  };

  struct Vertex0 {
    Vector position;
    Vector normal;
    Vector2 uv;
  };

  struct Vertex25Out {
    Vector position;
    Vector normal;
    Vector2 uv;
    USVector4 joints;
    Vector4 weights;
  };

  uint32 vertexType;
  uint32 numVertices;
  Pointer renderState;
  std::string vertexData;
  std::vector<uint16> indices;

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 3 || version == 2 || version == 1);
    vertexType = 0;

    if (version > 1) {
      rd.Read(vertexType);
    }
    PointerLocator ptr;
    rd.Read(ptr);
    renderState = ptr;

    switch (vertexType) {
    case 0: {
      auto vtx = ReadVector<Vertex0>(rd);
      numVertices = vtx.size();
      vertexData = {reinterpret_cast<char *>(vtx.data()),
                    reinterpret_cast<char *>(&*vtx.end())};
      break;
    }
    case 9: {
      auto vtx = ReadVector<Vertex9>(rd);
      numVertices = vtx.size();
      vertexData = {reinterpret_cast<char *>(vtx.data()),
                    reinterpret_cast<char *>(&*vtx.end())};
      break;
    }

    case 11: {
      auto vtx = ReadVector<Vertex11>(rd);
      numVertices = vtx.size();
      vertexData = {reinterpret_cast<char *>(vtx.data()),
                    reinterpret_cast<char *>(&*vtx.end())};
      break;
    }

    case 25: {
      std::vector<Vertex25Out> vtx;
      ReadVector(rd, vtx,
                 [vertexType = vertexType](BinReaderRef rd, Vertex25Out &item) {
                   if (vertexType == 25) {
                     uint32 vtxType;
                     rd.Read(vtxType);
                   }
                   rd.Read(item.position);
                   rd.Read(item.normal);
                   rd.Read(item.uv);
                   uint8 numBones;
                   rd.Read(numBones);
                   assert(numBones < 5);
                   for (uint8 b = 0; b < numBones; b++) {
                     rd.Read(item.joints[b]);
                     rd.Read(item.weights[b]);
                   }
                 });

      numVertices = vtx.size();
      vertexData = {reinterpret_cast<char *>(vtx.data()),
                    reinterpret_cast<char *>(&*vtx.end())};
      break;
    }

    default:
      throw std::runtime_error("Unhandled GenGroup vertex type");
    }

    indices = ReadVector<uint16>(rd);

    if (version > 2) {
      uint32 null0;
      rd.Read(null0);
      assert(null0 == 0);
    }
  }

  void Link(std::vector<std::unique_ptr<Streamable>> &classes) override {
    ::Link(renderState, classes);
  }

  void ToGltf(GLTFAni &main) override {
    gltf::Mesh &glMesh = main.meshes.back();
    auto &prim = glMesh.primitives.emplace_back();
    prim.indices =
        main.SaveIndices(indices.data(), indices.size()).accessorIndex;

    switch (vertexType) {
    case 0: {
      std::vector<Attribute> attrs{
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Position,
          },
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Normal,
          },
          {
              .type = uni::DataType::R32G32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::TextureCoordiante,
          },
      };

      prim.attributes = main.SaveVertices(vertexData.data(), numVertices, attrs,
                                          sizeof(Vertex0));
      break;
    }

    case 9: {
      std::vector<Attribute> attrs{
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Position,
              .offset = 4,
          },
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Normal,
          },
          {
              .type = uni::DataType::R32G32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::TextureCoordiante,
          },
      };

      prim.attributes = main.SaveVertices(vertexData.data(), numVertices, attrs,
                                          sizeof(Vertex9));
      break;
    }

    case 11: {
      std::vector<Attribute> attrs{
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Position,
              .offset = 4,
          },
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Normal,
          },
          {
              .type = uni::DataType::R8G8B8A8,
              .format = uni::FormatType::UNORM,
              .usage = AttributeType::VertexColor,
          },
          {
              .type = uni::DataType::R32G32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::TextureCoordiante,
          },
      };

      prim.attributes = main.SaveVertices(vertexData.data(), numVertices, attrs,
                                          sizeof(Vertex11));
      break;
    }

    case 25: {
      std::vector<Attribute> attrs{
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Position,
          },
          {
              .type = uni::DataType::R32G32B32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::Normal,
          },
          {
              .type = uni::DataType::R32G32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::TextureCoordiante,
          },
          {
              .type = uni::DataType::R16G16B16A16,
              .format = uni::FormatType::UINT,
              .usage = AttributeType::BoneIndices,
          },
          {
              .type = uni::DataType::R32G32B32A32,
              .format = uni::FormatType::FLOAT,
              .usage = AttributeType::BoneWeights,
          },
      };

      prim.attributes = main.SaveVertices(vertexData.data(), numVertices, attrs,
                                          sizeof(Vertex25Out));
      break;
    }
    }
  }
};

struct GenSkinGroup : GenGroup {
  static inline const uint32 CLSID = HashClassName("GenSkinGroup");

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 3 || version == 4 || version == 2);
    if (version == 2 || version == 3) {
      if (version == 3) {
        rd.Read(vertexType);
      }
      rd.Read(vertexType);
      vertexType = 25;

      PointerLocator ptr;
      rd.Read(ptr);
      renderState = ptr;

      std::vector<Vertex25Out> vtx;
      ReadVector(rd, vtx, [](BinReaderRef rd, Vertex25Out &item) {
        rd.Read(item.position);
        rd.Read(item.normal);
        rd.Read(item.uv);
        uint8 numBones;
        rd.Read(numBones);
        assert(numBones < 5);
        for (uint8 b = 0; b < numBones; b++) {
          rd.Read(item.joints[b]);
          rd.Read(item.weights[b]);
        }
      });

      numVertices = vtx.size();
      vertexData = {reinterpret_cast<char *>(vtx.data()),
                    reinterpret_cast<char *>(&*vtx.end())};
      indices = ReadVector<uint16>(rd);
    } else {
      GenGroup::Read(rd);
      int32 unk;
      rd.Read(unk);
      assert(unk == -1);
    }
  }
};

struct AnimationSet : Streamable {
  static inline const uint32 CLSID = HashClassName("AnimationSet");
  std::vector<std::string> animations;
  std::string unk;

  uint32 ClassId() const override { return CLSID; }

  void Read(BinReaderRef rd) override {
    uint8 version;
    rd.Read(version);
    assert(version == 1);
    unk = ReadString(rd);
    rd.ReadContainerLambda(animations, [](BinReaderRef rd, std::string &item) {
      item = ReadString(rd);
    });
  }
};

template <class Ty> auto MakeClass() {
  return std::make_pair(Ty::CLSID, [] -> std::unique_ptr<Streamable> {
    return std::make_unique<Ty>();
  });
}

std::map<uint32, std::unique_ptr<Streamable> (*)()> CLASSES{
    MakeClass<CNode>(),
    MakeClass<MeshNode>(),
    MakeClass<CSkinMeshNode>(),
    MakeClass<StaticScaleKeyFrameData>(),
    MakeClass<StaticScaleAndPositionKeyFrameData>(),
    MakeClass<StaticKeyFrameData>(),
    MakeClass<CompRotationKeyFrameData>(),
    MakeClass<PositionKeyFrameData>(),
    MakeClass<StaticRotationKeyFrameData>(),
    MakeClass<ScaleKeyFrameData>(),
    MakeClass<StaticPositionKeyFrameData>(),
    MakeClass<RotationKeyFrameData>(),
    MakeClass<Material>(),
    MakeClass<RenderState>(),
    MakeClass<BoundingBox>(),
    MakeClass<BoundingSphere>(),
    MakeClass<GenMesh>(),
    MakeClass<GenGroup>(),
    MakeClass<GenSkinMesh>(),
    MakeClass<GenSkinGroup>(),
    MakeClass<Animation>(),
    MakeClass<AnimationSet>(),
};

Streamables LoadArtworks(BinReaderRef rd) {
  const uint32 streamVersion = ReadHeader(rd);
  Streamables classes;
  auto CarryOn = [&rd] {
    bool carryOn;
    rd.Read(carryOn);
    return carryOn;
  };

  do {
    CheckClassStart(rd);
    uint32 classId;
    rd.Read(classId);

    if (streamVersion == 2) {
      uint64 classSize;
      rd.Read(classSize);
    }

    auto found = CLASSES.find(classId);

    if (found == CLASSES.end()) {
      throw std::runtime_error(
          "Undefined class: " + std::to_string(classId) +
          " at: " + std::to_string(rd.Tell() - 4));
    }

    rd.Read(*classes.emplace_back(found->second()));
  } while (CarryOn());

  for (auto &c : classes) {
    c->Link(classes);
  }

  return classes;
}

void AppProcessFile(AppContext *ctx) {
  std::vector<Streamables> bodyParts;
  GLTFAni main;
  size_t curMesh = 0;
  size_t curSkin = 0;

  for (auto &file : ctx->SupplementalFiles()) {
    auto partFile = ctx->RequestFile(file);
    bodyParts.emplace_back(LoadArtworks(*partFile.Get()));

    for (auto &c : bodyParts.back()) {
      std::vector<MeshNodeMeshLod> *lods = nullptr;
      bool isSkinned = false;

      if (c->ClassId() == MeshNode::CLSID) {
        lods = &c->As<MeshNode>()->lods;
      } else if (c->ClassId() == CSkinMeshNode::CLSID) {
        lods = &c->As<CSkinMeshNode>()->lods;
        isSkinned = true;
      }

      if (lods) {
        for (auto &l : *lods) {
          main.pathToMesh[l.meshPath] = curMesh++;
          if (isSkinned) {
            main.pathToSkin[l.meshPath] = curSkin++;
          }
        }
      }
    }
  }

  for (int32 i = -1; auto &b : bodyParts) {
    i++;

    for (auto &c : b) {
      std::vector<MeshNodeMeshLod> *lods = nullptr;
      CNode *node = nullptr;

      if (c->ClassId() == MeshNode::CLSID) {
        lods = &c->As<MeshNode>()->lods;
        node = c->As<MeshNode>();
      } else if (c->ClassId() == CSkinMeshNode::CLSID) {
        lods = &c->As<CSkinMeshNode>()->lods;
        node = c->As<CSkinMeshNode>();
      }

      if (lods) {
        main.meshTm = main.npcTm * node->tm;
        for (auto &l : *lods) {
          std::string path = ToLower(l.meshPath);
          AFileInfo pathInf(path);
          AFileInfo partInfo(ctx->SupplementalFiles()[i]);
          AppContextStream str =
              ctx->RequestFile(std::string(partInfo.GetFolder()) +
                               std::string(pathInf.GetFilenameExt()));
          Streamables classes = LoadArtworks(*str.Get());
          classes.front()->ToGltf(main);
        }
      }

      c->ToGltf(main);
    }
  }

  if (ANIMATED) {
    Streamables classes = LoadArtworks(ctx->GetStream());
    AnimationSet *set = classes.front()->As<AnimationSet>();
    std::set<std::string> anims{set->animations.begin(), set->animations.end()};

    for (auto &a : anims) {
      if (a.empty()) {
        continue;
      }
      AppContextStream str = ctx->RequestFile(
          std::string(ctx->workingFile.GetFolder()) + a + ".an");
      Streamables classes = LoadArtworks(*str.Get());
      classes.front()->ToGltf(main);
    }
  }

  BinWritterRef wr(
      ctx->NewFile(ctx->workingFile.ChangeExtension("_out.glb")).str);
  main.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
}
