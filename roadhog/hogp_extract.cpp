/*  HOGPExtract
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
    .header = HOGPExtract_DESC " v" HOGPExtract_VERSION
                               ", " HOGPExtract_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct HOGP {
  static const uint32 ID = CompileFourCC("HOGP");
  uint32 id;
  uint32 version;
  uint64 tocOffset;
};

struct TocFile {
  std::string path;
  uint32 uncompressedSize;
  uint32 compressedSize;
  uint32 chunkIndex;
  uint64 offset;

  void Read(BinReaderRef rd) {
    rd.ReadContainer<uint16>(path);
    rd.Read(uncompressedSize);
    rd.Read(compressedSize);
    rd.Read(chunkIndex);
    rd.Read(offset);
  }
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());

  HOGP hdr;
  rd.Read(hdr);

  if (hdr.id != hdr.ID) {
    throw es::InvalidHeaderError(hdr.id);
  }

  if (hdr.version != 3) {
    throw es::InvalidVersionError(hdr.version);
  }

  rd.Seek(hdr.tocOffset + sizeof(HOGP));
  uint32 tocSize;
  rd.Read(tocSize);
  std::vector<TocFile> files;
  rd.ReadContainer(files);
  std::vector<uint16> chunkSizes;
  rd.ReadContainer(chunkSizes);

  auto ectx = ctx->ExtractContext();
  char inBuffer[0x10000];
  char outBuffer[0x10000];

  for (auto &f : files) {
    if (f.offset == -1) {
      continue;
    }

    ectx->NewFile(f.path);
    rd.Seek(f.offset);

    if (f.compressedSize == f.uncompressedSize) {
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

    uint32 curChunkIndex = f.chunkIndex;
    uint32 totalOut = 0;

    while (totalOut < f.uncompressedSize) {
      const uint32 chunkSize = chunkSizes.at(curChunkIndex++);
      if (chunkSize == 0) {
        rd.ReadBuffer(outBuffer, sizeof(outBuffer));
        ectx->SendData({outBuffer, sizeof(outBuffer)});
        totalOut += sizeof(outBuffer);
        continue;
      }

      rd.ReadBuffer(inBuffer, chunkSize);
      z_stream infstream;
      infstream.zalloc = Z_NULL;
      infstream.zfree = Z_NULL;
      infstream.opaque = Z_NULL;
      infstream.avail_in = chunkSize;
      infstream.next_in = reinterpret_cast<Bytef *>(inBuffer);
      infstream.avail_out = sizeof(outBuffer);
      infstream.next_out = reinterpret_cast<Bytef *>(outBuffer);
      inflateInit2(&infstream, -MAX_WBITS);
      int state = inflate(&infstream, Z_FINISH);
      inflateEnd(&infstream);

      if (state < 0) {
        if (infstream.msg) {
          throw std::runtime_error(infstream.msg);
        } else {
          throw std::runtime_error("zlib error: " + std::to_string(state));
        }
      }

      ectx->SendData({outBuffer, infstream.total_out});
      totalOut += infstream.total_out;
    }
  }
}
