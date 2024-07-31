/*  ExtractOBSP
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
#include "spike/io/binreader_stream.hpp"
#include <map>

std::string_view filters[]{
    ".obsp$",
};

static AppInfo_s appInfo{
    .header = ExtractOBSP_DESC " v" ExtractOBSP_VERSION
                               ", " ExtractOBSP_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct OBSP {
  static const uint32 ID = CompileFourCC("OBSP");
  uint32 id;
  uint8 bigEndian;
  uint32 versions[2];
  uint32 numScripts;
  uint32 dataOffset;
  uint32 numStrings;
  uint32 maxNameSize;

  void Read(BinReaderRef_e rd) {
    rd.Read(id);

    if (id != ID) {
      throw es::InvalidHeaderError(id);
    }

    rd.Read(bigEndian);
    rd.SwapEndian(bigEndian);
    rd.Read(versions);

    if (versions[0] != 0xA) {
      throw es::InvalidVersionError(versions[0]);
    }

    rd.Read(numScripts);
    rd.Read(dataOffset);
    rd.Read(numStrings);
    rd.Read(maxNameSize);
  }
};

struct ScriptEntry {
  uint64 fullPath;
  uint64 fileName;
  uint32 dataOffset;
  uint32 dataSize;
  char data[3];
  uint64 name;
  uint64 category;
  uint64 className;

  void Read(BinReaderRef_e rd) {
    rd.Read(fullPath);
    rd.Read(fileName);
    rd.Read(dataOffset);
    rd.Read(dataSize);
    rd.Read(data);
    rd.Read(name);
    rd.Read(category);
    rd.Read(className);
  }
};

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

void ReadClass(BinReaderRef_e rd, nlohmann::json &node,
                std::vector<std::string> &memberNames);

void AppProcessFile(AppContext *ctx) {
  BinReaderRef_e rd(ctx->GetStream());
  OBSP hdr;
  rd.Read(hdr);
  rd.SwapEndian(hdr.bigEndian);
  std::map<uint64, std::string> names;

  for (uint32 i = 0; i < hdr.numStrings; i++) {
    uint64 hash;
    rd.Read(hash);
    rd.ReadContainer(names[hash]);
  }

  std::vector<ScriptEntry> entries;
  rd.ReadContainer(entries, hdr.numScripts);
  auto ectx = ctx->ExtractContext();
  std::string buffer;

  for (auto &entry : entries) {
    rd.Seek(entry.dataOffset);
    rd.ReadContainer(buffer, entry.dataSize);
    std::string entryName(names.at(entry.fullPath));

    const BOD *hdr = reinterpret_cast<const BOD *>(buffer.data());

   if (hdr->id == BOD::ID) {
      nlohmann::json main;
      std::vector<std::string> memberNames;
      std::stringstream sstr(std::move(buffer));
      BinReaderRef_e brd(sstr);
      brd.Seek(sizeof(BOD));
      brd.SwapEndian(hdr->bigEndian);
      ReadClass(brd, main, memberNames);
      entryName.append(".json");
      buffer = main.dump(2);
    }

    ectx->NewFile(entryName);
    ectx->SendData(buffer);
  }
};
