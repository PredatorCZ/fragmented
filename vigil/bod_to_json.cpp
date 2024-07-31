/*  BOD2JSON
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

static AppInfo_s appInfo{
    .header = BOD2JSON_DESC " v" BOD2JSON_VERSION ", " BOD2JSON_COPYRIGHT
                            "Lukas Cone",
};

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

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  BOD hdr;
  rd.Read(hdr);

  if (hdr.id != hdr.ID) {
    throw es::InvalidHeaderError(hdr.id);
  }

  if (hdr.version != 4) {
    throw es::InvalidVersionError(hdr.version);
  }

  nlohmann::json main;
  std::vector<std::string> memberNames;
  ReadObject(rd, main, memberNames);

  ctx->NewFile(ctx->workingFile.ChangeExtension2("json")).str << std::setw(2)
                                                              << main;
}
