/*  SAI2GLTF
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

#include "project.h"
#include "spike/app_context.hpp"
#include "spike/except.hpp"
#include "spike/gltf.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/io/binwritter_stream.hpp"
#include "spike/master_printer.hpp"
#include "spike/type/pointer.hpp"

std::string_view filters[]{".sai$"};

std::string_view controlFilters[]{
    ".glb$",
    ".gltf$",
};

static AppInfo_s appInfo{
    .header = SAI2GLTF_DESC " v" SAI2GLTF_VERSION ", " SAI2GLTF_COPYRIGHT
                            "Lukas Cone",
    .filters = filters,
    .batchControlFilters = controlFilters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct GLTFAni : GLTF {
  using GLTF::GLTF;

  GLTFStream &AnimStream() {
    if (aniStream < 0) {
      auto &newStream = NewStream("anims");
      aniStream = newStream.slot;
      return newStream;
    }
    return Stream(aniStream);
  }

private:
  int32 aniStream = -1;
};

struct Track {
  uint32 frameRangeEnd;
  uint16 numFrames;
  es::PointerX86<uint16> frames;
  es::PointerX86<Vector4A16> data;
};

struct Bone {
  uint32 frameRangeEnd;
  char name[32];
  es::PointerX86<Track> rotation;
  es::PointerX86<Track> position;
  es::PointerX86<Track> scale;
};

struct Header {
  uint32 id;
  uint32 fileSize;
  uint16 numBones;
  es::PointerX86<Bone> bones;
  uint32 null0;
};

void Fixup(Track &item, const char *root) {
  es::FixupPointers(root, item.frames, item.data);
}

void Fixup(Bone &item, const char *root) {
  es::FixupPointers(root, item.rotation, item.position, item.scale);

  if (item.rotation) {
    Fixup(*item.rotation, root);
  }
  if (item.position) {
    Fixup(*item.position, root);
  }
  if (item.scale) {
    Fixup(*item.scale, root);
  }
}

void Fixup(Header &item, const char *root) {
  item.bones.Fixup(root);

  for (uint32 i = 0; i < item.numBones; i++) {
    Fixup(item.bones[i], root);
  }
}

static const es::Matrix44 corMat{};
//{0, 0, -1, 0}, {-1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1});

void LoadAnim(GLTFAni &main, BinReaderRef rd, std::string animName) {
  std::string buffer;
  {
    Header hdr;
    rd.Push();
    rd.Read(hdr);

    if (hdr.id != 0x20030818) {
      PrintError(animName, " is not valid format");
      return;
    }

    rd.Pop();
    rd.ReadContainer(buffer, hdr.fileSize);
  }

  Header *hdr = reinterpret_cast<Header *>(buffer.data());
  Fixup(*hdr, buffer.data());
  float fpsInv = 1.f / 60;

  auto &stream = main.AnimStream();
  /*size_t nullInput = 0;
  {
    auto [acc, accid] = main.NewAccessor(stream, 4);
    acc.componentType = gltf::Accessor::ComponentType::Float;
    acc.type = gltf::Accessor::Type::Scalar;
    acc.count = 1;
    acc.min.emplace_back(0);
    acc.max.emplace_back(0);
    nullInput = accid;
    stream.wr.Write(0);
  }*/

  auto NewFrameAcc = [&](std::span<uint16> frameTimes) {
    auto [acc, accid] = main.NewAccessor(stream, 4);
    acc.componentType = gltf::Accessor::ComponentType::Float;
    acc.type = gltf::Accessor::Type::Scalar;
    acc.count = frameTimes.size();
    acc.min.emplace_back(0);
    acc.max.emplace_back(frameTimes.back() * fpsInv);

    for (uint16 f : frameTimes) {
      stream.wr.Write(f * fpsInv);
    }

    return accid;
  };

  auto &anim = main.animations.emplace_back();
  anim.name = animName;

  for (uint32 i = 0; i < hdr->numBones; i++) {
    Bone &b = hdr->bones[i];
    int32 foundNode = -1;
    for (int32 nodeIndex = 0; auto &n : main.nodes) {
      if (n.name == b.name) {
        foundNode = nodeIndex;
        break;
      }

      nodeIndex++;
    }

    if (b.position) {
      auto &chan = anim.channels.emplace_back();
      chan.target.path = "translation";
      chan.target.node = foundNode;
      chan.sampler = anim.samplers.size();

      auto &sampl = anim.samplers.emplace_back();
      sampl.input =
          NewFrameAcc({b.position->frames.Get(), b.position->numFrames});

      auto [acc, accid] = main.NewAccessor(stream, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec3;
      sampl.output = accid;
      acc.count = b.position->numFrames;
      Vector4A16 *data = b.position->data.Get();

      for (uint32_t i = 0; i < b.position->numFrames; i++) {
        stream.wr.Write<Vector>(data[i]);
      }
    }

    if (b.rotation) {
      auto &chan = anim.channels.emplace_back();
      chan.target.path = "rotation";
      chan.target.node = foundNode;
      chan.sampler = anim.samplers.size();

      auto &sampl = anim.samplers.emplace_back();
      sampl.input =
          NewFrameAcc({b.rotation->frames.Get(), b.rotation->numFrames});

      auto [acc, accid] = main.NewAccessor(stream, 4);
      acc.componentType = gltf::Accessor::ComponentType::Short;
      acc.type = gltf::Accessor::Type::Vec4;
      acc.normalized = true;
      acc.count = b.rotation->numFrames;
      sampl.output = accid;

      Vector4A16 *data = b.rotation->data.Get();

      for (uint32_t i = 0; i < b.rotation->numFrames; i++) {
        Vector4A16 rot(data[i]);
        rot *= 0x7fff;
        rot = Vector4A16(_mm_round_ps(rot._data, _MM_ROUND_NEAREST));
        stream.wr.Write(rot.Convert<int16>());
      }
    }

    if (b.scale) {
      auto &chan = anim.channels.emplace_back();
      chan.target.path = "scale";
      chan.target.node = foundNode;
      chan.sampler = anim.samplers.size();

      auto &sampl = anim.samplers.emplace_back();
      sampl.input = NewFrameAcc({b.scale->frames.Get(), b.scale->numFrames});

      auto [acc, accid] = main.NewAccessor(stream, 4);
      acc.componentType = gltf::Accessor::ComponentType::Float;
      acc.type = gltf::Accessor::Type::Vec3;
      acc.count = b.scale->numFrames;
      sampl.output = accid;
      Vector4A16 *data = b.scale->data.Get();

      for (uint32_t i = 0; i < b.scale->numFrames; i++) {
        stream.wr.Write<Vector>(data[i]);
      }
    }
  }
}

void AppProcessFile(AppContext *ctx) {
  GLTFAni main(gltf::LoadFromText(ctx->GetStream(),
                                  std::string(ctx->workingFile.GetFolder())));
  main.buffers.front().uri.clear();
  auto &anims = ctx->SupplementalFiles();

  for (auto &animFile : anims) {
    auto animStream = ctx->RequestFile(animFile);
    LoadAnim(main, *animStream.Get(),
             std::string(AFileInfo(animFile).GetFilename()));
  }

  BinWritterRef wr(
      ctx->NewFile(std::string(ctx->workingFile.GetFullPathNoExt()) +
                   "_out.glb")
          .str);
  main.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
}
