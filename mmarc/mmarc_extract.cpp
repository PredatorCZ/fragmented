/*  MMARCExtract
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
#include <map>
#include <mutex>

std::string_view filters[]{
    ".tab$",
};

static AppInfo_s appInfo{
    .header = MMARCExtract_DESC " v" MMARCExtract_VERSION
                                ", " MMARCExtract_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct TABChunk {
  uint32 uncompressedOffset;
  uint32 compressedOffset;
};

struct TABChunks {
  uint32 hash;
  std::vector<TABChunk> chunks;
};

struct TABFile {
  uint32 hash;
  uint32 offset;
  uint32 compressedSize;
  uint32 uncompressedSize;
};

static es::MappedFile mappedFile;
std::map<uint32, std::string_view> FILES;

bool AppInitContext(const std::string &dataFolder) {
  mappedFile = es::MappedFile(dataFolder + "mm_files.txt");
  std::string_view totalMap(static_cast<const char *>(mappedFile.data),
                            mappedFile.fileSize);
  static std::mutex accessMutex;

  while (!totalMap.empty()) {
    size_t found = totalMap.find_first_of("\r\n");

    if (found != totalMap.npos) {
      auto sub = totalMap.substr(0, found);

      if (sub.empty()) {
        continue;
      }

      JenHash3 hash(sub);

      {
        std::lock_guard lg(accessMutex);
        if (FILES.contains(hash) && FILES.at(hash) != sub) {
          PrintError("String colision: ", FILES.at(hash), " vs: ", sub);
        } else {
          FILES.emplace(hash, sub);
        }
      }

      totalMap.remove_prefix(found + 1);

      if (totalMap.front() == '\n') {
        totalMap.remove_prefix(1);
      }
    }
  }

  return true;
}

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  uint32 alignment;
  rd.Read(alignment);

  if (alignment != 0x800) {
    throw std::runtime_error("Unknown tab alignment value");
  }

  std::vector<TABChunks> chunks;
  rd.ReadContainerLambda(chunks, [](BinReaderRef rd, TABChunks &item) {
    rd.Read(item.hash);
    rd.ReadContainer(item.chunks);
  });

  std::vector<TABFile> files;

  while (!rd.IsEOF()) {
    rd.Read(files.emplace_back());
  }

  if (files.back().hash == 0) {
    files.pop_back();
  }

  auto ectx = ctx->ExtractContext();
  std::string arcFile(ctx->workingFile.ChangeExtension2("arc"));
  auto arc = ctx->RequestFile(arcFile);

  if (ectx->RequiresFolders()) {

    for (auto &f : files) {
      auto found = FILES.find(f.hash);

      if (found != FILES.end()) {
        AFileInfo finf(found->second);
        ectx->AddFolderPath(std::string(finf.GetFolder()));
      }
    }

    ectx->GenerateFolders();
  }

  for (auto &f : files) {
    auto found = FILES.find(f.hash);

    if (found == FILES.end()) {
      char filename[0x10]{};
      snprintf(filename, sizeof(filename), "%X", f.hash);
      ectx->NewFile(filename);
    } else {
      ectx->NewFile(std::string(found->second));
    }

    arc->clear();
    arc->seekg(f.offset);
    char streamOut[0x40000];

    if (f.uncompressedSize == f.compressedSize) {
      const size_t numBlocks = f.uncompressedSize / sizeof(streamOut);
      const size_t restBlock = f.uncompressedSize % sizeof(streamOut);

      for (size_t i = 0; i < numBlocks; i++) {
        arc->read(streamOut, sizeof(streamOut));
        ectx->SendData({streamOut, sizeof(streamOut)});
      }

      if (restBlock) {
        arc->read(streamOut, restBlock);
        ectx->SendData({streamOut, restBlock});
      }

      continue;
    }

    z_stream infstream{};
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;

    char streamIn[0x10000];
    infstream.avail_out = sizeof(streamOut);
    infstream.next_out = reinterpret_cast<Bytef *>(streamOut);
    int lastState = Z_STREAM_END;
    uint32 totalOut = 0;
    uint32 totalIn = 0;

    while (totalOut < f.uncompressedSize) {
      if (lastState == Z_STREAM_END) {
        inflateInit2(&infstream, -MAX_WBITS);
      }

      if (infstream.avail_in == 0) {
        const size_t nextIn = f.compressedSize - totalIn;
        infstream.avail_in = std::min(sizeof(streamIn), nextIn);
        arc->read(streamIn, infstream.avail_in);
        infstream.next_in = reinterpret_cast<Bytef *>(streamIn);
      }

      if (infstream.avail_out == 0) {
        ectx->SendData({streamOut, sizeof(streamOut)});
        infstream.avail_out = sizeof(streamOut);
        infstream.next_out = reinterpret_cast<Bytef *>(streamOut);
      }

      lastState = inflate(&infstream, Z_SYNC_FLUSH);

      if (lastState == Z_STREAM_END) {
        inflateEnd(&infstream);
        totalOut += infstream.total_out;
        totalIn += infstream.total_in;
      } else if (lastState < 0) {
        if (infstream.msg) {
          throw std::runtime_error(infstream.msg);
        } else {
          throw std::runtime_error("zlib error: " + std::to_string(lastState));
        }
      }
    }

    if (infstream.avail_out < sizeof(streamOut)) {
      ectx->SendData({streamOut, sizeof(streamOut) - infstream.avail_out});
    }
  }
}
