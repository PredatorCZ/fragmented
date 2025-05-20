/*  BINExtract
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
#include "spike/crypto/jenkinshash3.hpp"
#include "spike/except.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/io/fileinfo.hpp"
#include "spike/io/stat.hpp"
#include "spike/master_printer.hpp"
#include "zlib.h"

std::string_view filters[]{
    ".bin$",
};

static AppInfo_s appInfo{
    .header = BINExtract_DESC " v" BINExtract_VERSION ", " BINExtract_COPYRIGHT
                              "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct TocFile {
  std::string path;
  uint32 uncompressedSize;
  uint32 compressedSize;
  uint64 offset;
  uint64 hash;

  void Read(BinReaderRef rd) {
    rd.ReadContainer(path);
    rd.Read(uncompressedSize);
    rd.Read(compressedSize);
    rd.Read(offset);
    rd.Read(hash);
  }
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());

  uint32 numFiles;
  rd.Read(numFiles);

  if (numFiles > 0x20000) {
    throw std::runtime_error("Invalid archive, too many files");
  }

  std::vector<TocFile> files;
  rd.ReadContainer(files, numFiles);
  uint32 dataOffset;
  rd.Read(dataOffset);
  rd.SetRelativeOrigin(dataOffset, false);

  auto ectx = ctx->ExtractContext();
  char inBuffer[0x10000];
  char outBuffer[0x10000];

  for (auto &f : files) {
    ectx->NewFile(f.path);
    rd.Seek(f.offset);

    if (f.compressedSize == 0) {
      const uint32 numBlocks = f.uncompressedSize / sizeof(outBuffer);
      const uint32 restBlock = f.uncompressedSize % sizeof(outBuffer);

      for (uint32 i = 0; i < numBlocks; i++) {
        rd.Read(outBuffer);
        ectx->SendData({outBuffer, sizeof(outBuffer)});
      }

      if (restBlock) {
        rd.ReadBuffer(outBuffer, restBlock);
        ectx->SendData({outBuffer, restBlock});
      }

      continue;
    }

    uint32 totalOut = 0;

    z_stream infstream{};
    infstream.avail_out = sizeof(outBuffer);
    infstream.next_out = reinterpret_cast<Bytef *>(outBuffer);
    inflateInit(&infstream);

    while (totalOut < f.uncompressedSize) {
      if (infstream.avail_in == 0) {
        rd.ReadBuffer(inBuffer, sizeof(inBuffer));
        infstream.avail_in = sizeof(inBuffer);
        infstream.next_in = reinterpret_cast<Bytef *>(inBuffer);
      }

      if (infstream.avail_out == 0) {
        ectx->SendData({outBuffer, sizeof(outBuffer)});
        totalOut += sizeof(outBuffer);
        infstream.avail_out = sizeof(outBuffer);
        infstream.next_out = reinterpret_cast<Bytef *>(outBuffer);
      }

      int state = inflate(&infstream, Z_SYNC_FLUSH);

      if (state < 0) {
        if (infstream.msg) {
          throw std::runtime_error(infstream.msg);
        } else {
          throw std::runtime_error("zlib error: " + std::to_string(state));
        }
      }

      if (state == Z_STREAM_END) {
        ectx->SendData({outBuffer, infstream.total_out - totalOut});
        break;
      }
    }

    inflateEnd(&infstream);
  }
}

size_t AppExtractStat(request_chunk requester) {
  auto buffer = requester(0, 4);
  auto numfiles = reinterpret_cast<const uint32 *>(buffer.data());
  return *numfiles;
}
