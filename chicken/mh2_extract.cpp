/*  MH2Extract
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
#include "spike/io/binreader_stream.hpp"

std::string_view filters[]{
    "^MoorHuhn2.wtn$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header = MH2Extract_DESC " v" MH2Extract_VERSION ", " MH2Extract_COPYRIGHT
                              "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct Chunk {
  std::string name;
  uint32 offset;
  uint32 size;
  std::vector<Chunk> subItems;

  void Read(BinReaderRef rd) {
    uint8 type;
    rd.Read(type);
    uint32 null;
    rd.Read(null);
    rd.ReadContainer(name);
    if (type == 2) {
      uint32 const1;
      rd.Read(const1);
      rd.Read(offset);
      rd.Read(size);
      offset ^= 0xFFAA5533;
      size ^= 0x3355AAFF;
    }
    rd.ReadContainer(subItems);
  }
};

struct Header {
  char id[56];
  uint32 nullOffset;
  uint32 tocOffset;
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  Header hdr;
  rd.Read(hdr);

  if (!std::string_view(hdr.id).starts_with("MUDGE4.0")) {
    throw es::InvalidHeaderError();
  }

  auto ectx = ctx->ExtractContext();
  rd.Seek(hdr.tocOffset);
  Chunk rootChunk;
  rd.Read(rootChunk);

  for (auto &folder : rootChunk.subItems) {
    for (auto &file : folder.subItems) {
      std::string path = folder.name + "/" + file.name;
      ectx->NewFile(path);

      rd.Seek(file.offset);
      std::vector<uint8> buffer;
      rd.ReadContainer(buffer, file.size);
      for (uint8 &c : buffer) {
        c ^= 0x88;
      }
      ectx->SendData(
          {reinterpret_cast<const char *>(buffer.data()), buffer.size()});
    }
  }
}
