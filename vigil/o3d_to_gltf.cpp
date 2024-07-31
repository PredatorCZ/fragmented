/*  O3D2GLTF
    Copyright(C) 2024 Lukas Cone

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

#include "nlohmann/json.hpp"
#include "project.h"
#include "spike/app_context.hpp"
#include "spike/except.hpp"
#include "spike/gltf.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/reflect/reflector.hpp"
#include "spike/uni/format.hpp"

std::string_view filters[]{
    ".o3d$",
};

struct O3D2GLTF : ReflectorBase<O3D2GLTF> {
  std::string skeletonPath;
} settings;

REFLECT(CLASS(O3D2GLTF),
        MEMBERNAME(skeletonPath, "skeleton-path",
                   ReflDesc{"Specify path to skeleton o3d file.", "FILE"}))

static AppInfo_s appInfo{
    .header = O3D2GLTF_DESC " v" O3D2GLTF_VERSION ", " O3D2GLTF_COPYRIGHT
                            "Lukas Cone",
    .settings = reinterpret_cast<ReflectorFriend *>(&settings),
    .filters = filters,

};

#include "glm/gtx/quaternion.hpp"
#include "spike/master_printer.hpp"
#include <cassert>

AppInfo_s *AppInitModule() { return &appInfo; }

extern "C" uint64_t crc64(uint64_t crc, const char *buf, uint64_t len);

struct BOD {
  static const uint32 ID = CompileFourCC("BOD\xFD");
  uint32 id;
  uint8 version;
  bool compressed;
  bool hashedStrings;
  bool bigEndian;
  uint32 numStrings;
  uint32 maxStringSize;
};

void ReadObject(BinReaderRef rd, nlohmann::json &node,
                std::vector<std::string> &memberNames);

static const es::Matrix44 corMat{
    {-1, 0, 0, 0}, {0, 0, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}};

void LoadNodes(nlohmann::json &node, GLTF &main, uint32 parent) {
  for (nlohmann::json &child : node) {
    nlohmann::json &node = child["Node3D"];
    const uint32 curId = main.nodes.size();
    main.nodes.at(parent).children.emplace_back(curId);
    auto &gNode = main.nodes.emplace_back();
    gNode.name = node["Name"];

    if (node.contains("Position")) {
      nlohmann::json data = node["Position"];
      Vector4A16 translation(data[0], data[1], data[2], 1);
      memcpy(gNode.translation.data(), &translation, 12);
    }

    if (node.contains("Rotation")) {
      nlohmann::json data = node["Rotation"];
      glm::vec3 rotation(glm::radians(float(data[0])),
                         glm::radians(float(data[1])),
                         glm::radians(float(data[2])));
      glm::quat quat(rotation);
      Vector4A16 qt(quat.x, quat.y, quat.z, quat.w);
      memcpy(gNode.rotation.data(), &qt, 16);
    }

    if (node.contains("Children")) {
      LoadNodes(node["Children"], main, curId);
    }
  }
}

void LoadSkeleton(nlohmann::json &node, GLTF &main) {
  nlohmann::json &skeleton = node["Skeleton3D"];
  main.scenes.front().nodes.emplace_back(main.nodes.size());
  auto &mNode = main.nodes.emplace_back();
  mNode.name = skeleton["Name"];
  Vector4A16 qt(corMat.ToQuat());
  memcpy(mNode.rotation.data(), &qt, 16);
  LoadNodes(skeleton["Children"], main, main.nodes.size() - 1);
}

void LoadVisuals(nlohmann::json &node, GLTF &main) {
  for (auto &visual : node) {
    const bool isSkinned = visual.contains("SkinMeshVisual");
    nlohmann::json &skVisual =
        isSkinned ? visual["SkinMeshVisual"] : visual["StaticMeshVisual"];
    std::string refNode = skVisual["RefNode"];
    bool found = false;

    for (auto &n : main.nodes) {
      if (n.name == refNode) {
        n.children.emplace_back(main.nodes.size());
        found = true;
        break;
      }
    }

    if (!found) {
      main.scenes.front().nodes.emplace_back(main.nodes.size());
    }

    auto &gNode = main.nodes.emplace_back();
    gNode.mesh = skVisual["MeshID"];
    gNode.name = skVisual["MeshName"];

    if (isSkinned) {
      gNode.skin = gNode.mesh;
    }
    /*const uint32 lod = skVisual["LODMeshID"][0];
    gNode.name.append("_LOD");
    gNode.name.append(std::to_string(lod));*/
  }
}

std::string ReadString(BinReaderRef rd) {
  uint8 unk;
  rd.Read(unk);
  assert(unk == 0xff);
  std::string value;
  rd.ReadContainer<uint16>(value);
  return value;
}

void LoadSkin(uint32 numJoints, BinReaderRef rd, GLTFModel &main) {
  auto &skin = main.skins.emplace_back();
  auto &stream = main.SkinStream();
  auto [acc, accId] = main.NewAccessor(stream, 16);
  skin.inverseBindMatrices = accId;
  acc.type = gltf::Accessor::Type::Mat4;
  acc.componentType = gltf::Accessor::ComponentType::Float;
  acc.count = numJoints;

  for (uint32 i = 0; i < numJoints; i++) {
    std::string jointName = ReadString(rd);
    es::Matrix44 ibm;
    rd.Read(ibm);
    uint32 unk0;
    rd.Read(unk0);

    ibm = -(corMat * -ibm);

    stream.wr.Write(ibm);
    bool found = false;

    for (uint32 nodeIndex = 0; auto &n : main.nodes) {
      if (n.name == jointName) {
        skin.joints.emplace_back(nodeIndex);
        found = true;
        break;
      }
      nodeIndex++;
    }

    assert(found);
  }

  assert(skin.joints.size() == numJoints);
}

struct Primitive {
  std::string materialName;
  uint32 numVertices;
  uint32 vertexStart;
  uint32 numIndices;
  uint32 indexStart;

  void Read(BinReaderRef rd) {
    materialName = ReadString(rd);
    int32 dummy;
    rd.Read(dummy);
    assert(dummy < 2);
    rd.Read(dummy);
    assert(dummy == 0);

    rd.Read(numVertices);
    rd.Read(vertexStart);
    rd.Read(numIndices);
    rd.Read(indexStart);
  }
};

struct UV2ColorVertex {
  uint32 uv1;
  uint32 uv2;
  uint32 color;
};

struct SkinnedVertex {
  uint32 numBones;
  Vector pos;
  float unk0;
  Vector normal;
  Vector tangent;
  float unk[2];
};

struct StaticVertex {
  uint32 normal;
  uint32 tangent;
  uint32 uv1;
  uint32 uv2;
  uint32 color;
};

gltf::Attributes LoadStatic(BinReaderRef rd, GLTFModel &main,
                            uint32 numVertices) {
  gltf::Attributes attrs;

  {
    std::vector<Vector> positions;
    rd.ReadContainer(positions, numVertices);
    auto &vstr = main.GetVt12();

    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec3;
      acc.count = numVertices;
      attrs["POSITION"] = accId;
    }

    vstr.wr.WriteContainer(positions);
  }

  {
    std::vector<StaticVertex> staticVertices;
    rd.ReadContainer(staticVertices, numVertices);

    auto &vstr = main.GetVt8();
    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec2;
      acc.count = numVertices;

      attrs["TEXCOORD_0"] = accId;
    }
    auto &codec =
        uni::FormatCodec::Get({uni::FormatType::FLOAT, uni::DataType::R16G16});

    for (auto &f : staticVertices) {
      Vector4A16 value;
      codec.GetValue(value, reinterpret_cast<const char *>(&f.uv1));
      vstr.wr.Write<Vector2>(value);
    }

    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec2;
      acc.count = numVertices;

      attrs["TEXCOORD_1"] = accId;
    }

    for (auto &f : staticVertices) {
      Vector4A16 value;
      codec.GetValue(value, reinterpret_cast<const char *>(&f.uv2));
      vstr.wr.Write<Vector2>(value);
    }

    auto &normalCodec = uni::FormatCodec::Get(
        {uni::FormatType::NORM, uni::DataType::R10G10B10A2});

    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Short;
      acc.normalized = true;
      acc.type = gltf::Accessor::Type::Vec3;
      acc.count = numVertices;
      attrs["NORMAL"] = accId;
    }

    for (auto &f : staticVertices) {
      Vector4A16 norm;
      normalCodec.GetValue(norm, reinterpret_cast<const char *>(&f.normal));
      norm.Normalize() *= 0x7fff;
      norm = Vector4A16(_mm_round_ps(norm._data, _MM_ROUND_NEAREST));
      vstr.wr.Write(norm.Convert<int16>());
    }

    auto &cstr = main.GetVt4();
    {
      auto [acc, accId] = main.NewAccessor(cstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::UnsignedByte;
      acc.type = gltf::Accessor::Type::Vec4;
      acc.count = numVertices;
      acc.normalized = true;

      attrs["COLOR_0"] = accId;
    }

    for (auto &f : staticVertices) {
      cstr.wr.Write(f.color);
    }
  }

  {
    std::vector<Vector4> unkVertices;
    rd.ReadContainer(unkVertices, numVertices);
  }

  return attrs;
}

gltf::Attributes LoadSkinned(BinReaderRef rd, GLTFModel &main,
                             uint32 numVertices) {
  gltf::Attributes attrs;

  {
    std::vector<UV2ColorVertex> fragVertices;
    rd.ReadContainer(fragVertices, numVertices);
    auto &vstr = main.GetVt8();
    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec2;
      acc.count = numVertices;

      attrs["TEXCOORD_0"] = accId;
    }
    auto &codec =
        uni::FormatCodec::Get({uni::FormatType::FLOAT, uni::DataType::R16G16});

    for (auto &f : fragVertices) {
      Vector4A16 value;
      codec.GetValue(value, reinterpret_cast<const char *>(&f.uv1));
      vstr.wr.Write<Vector2>(value);
    }

    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec2;
      acc.count = numVertices;

      attrs["TEXCOORD_1"] = accId;
    }

    for (auto &f : fragVertices) {
      Vector4A16 value;
      codec.GetValue(value, reinterpret_cast<const char *>(&f.uv2));
      vstr.wr.Write<Vector2>(value);
    }

    auto &cstr = main.GetVt4();
    {
      auto [acc, accId] = main.NewAccessor(cstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::UnsignedByte;
      acc.type = gltf::Accessor::Type::Vec4;
      acc.count = numVertices;
      acc.normalized = true;

      attrs["COLOR_0"] = accId;
    }

    for (auto &f : fragVertices) {
      cstr.wr.Write(f.color);
    }
  }

  {
    std::vector<SkinnedVertex> skinnedVertices;
    rd.ReadContainer(skinnedVertices, numVertices);
    auto &vstr = main.GetVt12();

    {
      auto [acc, accId] = main.NewAccessor(vstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec3;
      acc.count = numVertices;
      attrs["POSITION"] = accId;
    }

    for (auto &f : skinnedVertices) {
      Vector4A16 pos(f.pos);
      pos = pos * corMat;
      vstr.wr.Write<Vector>(pos);
    }

    auto &nstr = main.GetVt8();

    {
      auto [acc, accId] = main.NewAccessor(nstr, 4);
      acc.componentType = gltf::Accessor::ComponentType::Short;
      acc.normalized = true;
      acc.type = gltf::Accessor::Type::Vec3;
      acc.count = numVertices;
      attrs["NORMAL"] = accId;
    }

    for (auto &f : skinnedVertices) {
      Vector4A16 norm(f.normal);
      norm = norm * corMat;
      norm.Normalize() *= 0x7fff;
      norm = Vector4A16(_mm_round_ps(norm._data, _MM_ROUND_NEAREST));
      nstr.wr.Write(norm.Convert<int16>());
    }
  }

  {
    std::vector<Vector4A16> weights;
    rd.ReadContainer(weights, numVertices);
    auto &vstr = main.GetVt4();

    auto [acc, accId] = main.NewAccessor(vstr, 4);
    acc.componentType = gltf::Accessor::ComponentType::UnsignedByte;
    acc.normalized = true;
    acc.type = gltf::Accessor::Type::Vec4;
    acc.count = numVertices;
    attrs["WEIGHTS_0"] = accId;

    for (auto &f : weights) {
      f *= 0xff;
      f = Vector4A16(_mm_round_ps(f._data, _MM_ROUND_NEAREST));
      vstr.wr.Write(f.Convert<uint8>());
    }
  }

  {
    std::vector<UCVector4> bones;
    rd.ReadContainer(bones, numVertices);
    auto &vstr = main.GetVt4();
    auto [acc, accId] = main.NewAccessor(vstr, 4);
    acc.componentType = gltf::Accessor::ComponentType::UnsignedByte;
    acc.type = gltf::Accessor::Type::Vec4;
    acc.count = numVertices;
    attrs["JOINTS_0"] = accId;

    vstr.wr.WriteContainer(bones);
  }

  return attrs;
}

void LoadDCM(BinReaderRef rd, GLTFModel &main) {
  uint32 dataOffset;
  uint32 numMeshes;
  rd.Read(dataOffset);
  rd.Read(numMeshes);
  std::vector<uint32> numIndices;
  rd.ReadContainer(numIndices);
  std::vector<int32> numVertices;
  rd.ReadContainer(numVertices);
  int32 unk;
  rd.Read(unk);
  rd.Skip(8 * numMeshes);
  std::vector<std::vector<Primitive>> meshes;

  for (uint32 m = 0; m < numMeshes; m++) {
    uint8 isSkinned;
    uint8 b1;

    rd.Read(isSkinned);
    rd.Read(b1);
    assert(b1 == 1);

    uint32 numVertices;
    uint32 vertexType;
    uint32 bufferOffset;
    uint32 numPrimitives;
    uint32 numJoints;
    Vector bboxMin;
    Vector bboxMax;

    rd.Read(numVertices);
    rd.Read(vertexType);
    rd.Read(bufferOffset);

    if (isSkinned) {
      rd.Read(numPrimitives);
      rd.Read(numJoints);
      rd.Read(bboxMin);
      rd.Read(bboxMax);

      LoadSkin(numJoints, rd, main);
    } else {
      rd.Read(numJoints); // unk
      rd.Read(numPrimitives);
      rd.Read(bboxMin);
      rd.Read(bboxMax);
    }

    rd.ReadContainer(meshes.emplace_back(), numPrimitives);
  }

  rd.Seek(dataOffset);
  std::map<std::string, uint32> materials;

  for (uint32 m = 0; m < numMeshes; m++) {
    auto &prims = meshes[m];
    uint32 indexBufferSize;
    rd.Read(indexBufferSize);
    uint32 indexSize;
    rd.Read(indexSize);
    size_t beginFaces = 0;

    {
      auto &istr = main.GetIndexStream();
      istr.wr.ApplyPadding(indexSize);
      beginFaces = istr.wr.Tell();
      std::string indexBuffer;
      rd.ReadContainer(indexBuffer, indexBufferSize);

      auto SwitchIndex = [](auto *data, uint32 count) {
        for (uint32 i = 0; i < count; i += 3) {
          auto tmp = data[i];
          data[i] = data[i + 1];
          data[i + 1] = tmp;
        }
      };

      switch (indexSize) {
      case 1:
        SwitchIndex(indexBuffer.data(), numIndices[m]);
        break;
      case 2:
        SwitchIndex(reinterpret_cast<uint16 *>(indexBuffer.data()),
                    numIndices[m]);
        break;
      case 4:
        SwitchIndex(reinterpret_cast<uint32 *>(indexBuffer.data()),
                    numIndices[m]);
        break;
      }

      istr.wr.WriteContainer(indexBuffer);
    }

    gltf::Attributes attributes;

    if (int32 numVerts = numVertices.at(m); numVerts > 0) {
      attributes = LoadSkinned(rd, main, numVerts);
    } else {
      attributes = LoadStatic(rd, main, -numVerts);
    }

    auto &gMesh = main.meshes.emplace_back();

    for (auto &p : prims) {
      auto &istr = main.GetIndexStream();
      auto [acc, accId] = main.NewAccessor(istr, indexSize);
      acc.type = gltf::Accessor::Type::Scalar;
      switch (indexSize) {
      case 1:
        acc.componentType = gltf::Accessor::ComponentType::UnsignedByte;
        break;
      case 2:
        acc.componentType = gltf::Accessor::ComponentType::UnsignedShort;
        break;
      case 4:
        acc.componentType = gltf::Accessor::ComponentType::UnsignedInt;
        break;
      }
      acc.count = p.numIndices;
      acc.byteOffset = beginFaces + indexSize * p.indexStart;
      auto &gPrim = gMesh.primitives.emplace_back();
      gPrim.indices = accId;
      gPrim.attributes = attributes;

      if (auto found = materials.find(p.materialName);
          found != materials.end()) {
        gPrim.material = found->second;
      } else {
        gPrim.material = materials.size();
        materials.emplace(p.materialName, gPrim.material);
        main.materials.emplace_back().name = p.materialName;
      }
    }
  }
}

nlohmann::json LoadObject(BinReaderRef rd) {
  BOD hdr;
  rd.Read(hdr);

  if (hdr.id != hdr.ID) {
    throw es::InvalidHeaderError(hdr.id);
  }

  if (hdr.version != 4) {
    throw es::InvalidVersionError(hdr.version);
  }

  nlohmann::json main;
  {
    std::vector<std::string> memberNames;
    ReadObject(rd, main, memberNames);
  }

  if (!main.contains("Object3D")) {
    throw std::runtime_error("Loaded object is not Object3D");
  }

  return main;
}

void LoadExternalSkeleton(AppContext *ctx, GLTF &main) {
  auto str = ctx->RequestFile(settings.skeletonPath);
  BinReaderRef rd(*str.Get());
  nlohmann::json jMain = LoadObject(rd);
  nlohmann::json &object = jMain["Object3D"];

  if (object.contains("Skeleton")) {
    LoadSkeleton(object["Skeleton"], main);
  }
}

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  nlohmann::json main = LoadObject(rd);
  nlohmann::json &object = main["Object3D"];
  GLTFModel gMain;

  if (object.contains("Skeleton")) {
    LoadSkeleton(object["Skeleton"], gMain);
  } else if (settings.skeletonPath.size() > 0) {
    try {
      LoadExternalSkeleton(ctx, gMain);
    } catch (const std::exception &e) {
      PrintWarning("Failed to load external skeleton: ", e.what());
    }
  }

  LoadVisuals(object["Visuals"], gMain);

  auto dcmStream = ctx->RequestFile(ctx->workingFile.ChangeExtension2("dcm"));
  LoadDCM(*dcmStream.Get(), gMain);

  BinWritterRef wr(ctx->NewFile(ctx->workingFile.ChangeExtension(".glb")).str);
  gMain.extensionsRequired.emplace_back("KHR_mesh_quantization");
  gMain.extensionsUsed.emplace_back("KHR_mesh_quantization");
  gMain.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
}
