/*  MH3Extract
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
    "^moorhuhn3.dat$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header = MH3Extract_DESC " v" MH3Extract_VERSION ", " MH3Extract_COPYRIGHT
                              "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct File {
  char fileName[48];
  uint32 offset;
  uint32 size;
  uint64 null1;
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  File hdr;
  rd.Read(hdr);

  if (std::string_view("MH3 V1.0 ") != hdr.fileName) {
    throw es::InvalidHeaderError();
  }

  auto ectx = ctx->ExtractContext();

  while (!rd.IsEOF()) {
    rd.Read(hdr);

    if (std::string_view("****") == hdr.fileName) {
      break;
    }

    rd.Push();
    rd.Seek(hdr.offset);
    std::string buffer;
    rd.ReadContainer(buffer, hdr.size);
    rd.Pop();

    ectx->NewFile(hdr.fileName);
    ectx->SendData(buffer);
  }
}
