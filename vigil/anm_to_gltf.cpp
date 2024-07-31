/*  ANM2GLTF
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

std::string_view filters[]{".anm$"};

std::string_view controlFilters[]{
    ".glb$",
    ".gltf$",
};

static AppInfo_s appInfo{
    .header = ANM2GLTF_DESC " v" ANM2GLTF_VERSION ", " ANM2GLTF_COPYRIGHT
                            "Lukas Cone",
    .filters = filters,
    .batchControlFilters = controlFilters,
};

#include "spike/master_printer.hpp"
#include <cassert>

AppInfo_s *AppInitModule() { return &appInfo; }

extern "C" uint64_t crc64(uint64_t crc, const char *buf, uint64_t len);

struct ANM {
  static const uint32 ID = CompileFourCC("ANM\x01");
  uint32 id;
  uint8 frameRate;
  uint16 numFrames;
  uint32 fileSize;
  uint32 unk0;
  std::string animName;

  void Read(BinReaderRef rd) {
    rd.Read(id);

    if (id != ID) {
      throw es::InvalidHeaderError(id);
    }

    rd.Read(frameRate);
    assert(frameRate == 0);
    rd.Read(frameRate);
    rd.Read(numFrames);
    rd.Read(fileSize);
    rd.Read(unk0);
    rd.ReadString(animName);
  }
};

struct Frame1 {
  CVector data;
  uint8 knot;
};

struct FrameBlock0 {
  std::vector<CVector4> frames0;
  std::vector<Frame1> frames1;

  void Read(BinReaderRef rd) {
    uint16 numFrames;
    uint16 curFrame = 0;

    rd.Read(numFrames);

    while (curFrame < numFrames) {
      rd.Read(frames0.emplace_back());
      curFrame += (frames0.back().w & 0x3f) + 1;
    }

    for (uint32 i = 0; i < numFrames; i++) {
      rd.Read(frames1.emplace_back().data);
    }

    for (uint32 i = 0; i < numFrames; i++) {
      rd.Read(frames1.at(i).knot);
    }
  }
};

struct FrameBlock1 {
  std::vector<SVector4> frames0;
  std::vector<Frame1> frames1;

  void Read(BinReaderRef rd) {
    uint16 numFrames;
    uint16 curFrame = 0;

    rd.Read(numFrames);

    while (curFrame < numFrames) {
      rd.Read(frames0.emplace_back());
      curFrame += frames0.back().x;
    }

    for (uint32 i = 0; i < numFrames; i++) {
      rd.Read(frames1.emplace_back().data);
    }

    for (uint32 i = 0; i < numFrames; i++) {
      rd.Read(frames1.at(i).knot);
    }
  }
};

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

void LoadAnim(GLTFAni &main, BinReaderRef rd,
              const std::map<uint64, uint32> &nodes) {
  ANM hdr;
  rd.Read(hdr);

  auto &str = main.AnimStream();
  auto &anim = main.animations.emplace_back();
  anim.name = hdr.animName;
  float frameFrac = 1.f / hdr.frameRate;

  nlohmann::json dump;

  while (rd.Tell() < hdr.fileSize) {
    uint16 tag;
    rd.Read(tag);
    assert(tag == 0x22);
    rd.ApplyPadding(8);
    uint64 nodeHash;
    rd.Read(nodeHash);
    int16 blendWeight;
    rd.Read(blendWeight);
    FrameBlock0 block0;
    rd.Read(block0);
    FrameBlock1 block1;
    rd.Read(block1);

    if (nodes.contains(nodeHash)) {
      auto &channel = anim.channels.emplace_back();
      channel.target.node = nodes.at(nodeHash);
      channel.target.path = "translation";
      channel.sampler = anim.samplers.size();

      auto &sampler = anim.samplers.emplace_back();

      {
        auto [acc, accId] = main.NewAccessor(str, 4);
        acc.type = gltf::Accessor::Type::Scalar;
        acc.componentType = gltf::Accessor::ComponentType::Float;
        acc.count = block1.frames0.size();
        sampler.input = accId;
        uint32 curFrame = 0;

        for (auto &f : block1.frames1) {
          curFrame += f.knot;
          str.wr.Write(curFrame * frameFrac);
        }
      }

      {
        auto [acc, accId] = main.NewAccessor(str, 4);
        acc.type = gltf::Accessor::Type::Vec3;
        acc.componentType = gltf::Accessor::ComponentType::Float;
        acc.count = block1.frames0.size();
        sampler.output = accId;

        if (block1.frames0.size() == 1) {
          Vector4 controlBegin(block1.frames0.front().Convert<float>() * 0.5);
          Vector controlDataBegin(controlBegin.y, controlBegin.z,
                                  controlBegin.w);
          str.wr.Write(controlDataBegin);
          continue;
        }

        uint32 curFrame = 0;
        const float frameScale =
            block1.frames0.size() > 1
                ? hdr.numFrames / float(block1.frames0.size() - 1)
                : hdr.numFrames;

        for (auto &f : block1.frames1) {
          curFrame += f.knot;
          Vector t(f.data.Convert<float>() * 1.f / 0xff);

          uint32 curControlFrame = 0;
          uint32 curControlIndex = 0;

          for (auto &c : block1.frames0) {
            if (curControlFrame > curFrame) {
              break;
            }

            curControlFrame += c.x * frameScale;
            curControlIndex++;
          }

          Vector4 controlBegin(
              block1.frames0.at(curControlIndex - 1).Convert<float>() * 0.5);
          Vector controlDataBegin(controlBegin.y, controlBegin.z,
                                  controlBegin.w);
          Vector4 controlEnd(
              block1.frames0.at((curControlIndex) % block1.frames0.size())
                  .Convert<float>() *
              0.5);
          Vector controlDataEnd(controlEnd.y, controlEnd.z, controlEnd.w);

          str.wr.Write(controlDataBegin +
                       (controlDataEnd - controlDataBegin) * t);
        }
      }

      // PrintInfo(nodeHash, main.nodes.at(nodes.at(nodeHash)).name);
      nlohmann::json &node = dump[main.nodes.at(nodes.at(nodeHash)).name];
      nlohmann::json &jblock0 = node["block0"];

      for (auto &f : block0.frames0) {
        auto &item = jblock0.emplace_back();
        item.push_back(f.x);
        item.push_back(f.y);
        item.push_back(f.z);
      }

      nlohmann::json &jblock1 = node["block1"];

      for (auto &f : block0.frames1) {
        auto &item = jblock1.emplace_back();
        item.push_back(f.data.x);
        item.push_back(f.data.y);
        item.push_back(f.data.z);
      }

      nlohmann::json &jblock2 = node["block2"];

      for (auto &f : block1.frames0) {
        auto &item = jblock2.emplace_back();
        item.push_back(f.y);
        item.push_back(f.z);
        item.push_back(f.w);
      }

      nlohmann::json &jblock3 = node["block3"];

      for (auto &f : block1.frames1) {
        auto &item = jblock3.emplace_back();
        item.push_back(f.data.x);
        item.push_back(f.data.y);
        item.push_back(f.data.z);
      }
    }
  }

  // PrintInfo(dump.dump());
}

void AppProcessFile(AppContext *ctx) {
  GLTFAni main(gltf::LoadFromBinary(ctx->GetStream(), ""));
  auto &anims = ctx->SupplementalFiles();

  std::map<uint64, uint32> nodes;

  for (uint32 curNode = 0; auto &n : main.nodes) {
    std::string nodeName = n.name;
    // std::transform(nodeName.begin(), nodeName.end(), nodeName.begin(),
    //              [](char c) { return std::tolower(c); });
    nodes.emplace(crc64(0, nodeName.data(), nodeName.size()), curNode++);
  }

  for (auto &animFile : anims) {
    auto animStream = ctx->RequestFile(animFile);
    LoadAnim(main, *animStream.Get(), nodes);
  }

  BinWritterRef wr(
      ctx->NewFile(std::string(ctx->workingFile.GetFullPathNoExt()) +
                   "_out.glb")
          .str);
  main.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
}
