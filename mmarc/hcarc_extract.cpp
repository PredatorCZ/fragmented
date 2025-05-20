/*  HCARCExtract
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
#include <map>
#include <mutex>

std::string_view filters[]{
    ".tab$",
};

static AppInfo_s appInfo{
    .header = HCARCExtract_DESC " v" HCARCExtract_VERSION
                                ", " HCARCExtract_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct TABFile {
  uint32 hash;
  uint32 offset;
  uint32 size;
  uint32 hasConflict;
};

static es::MappedFile mappedFile;
std::map<uint32, std::string_view> FILES;

bool AppInitContext(const std::string &dataFolder) {
  mappedFile = es::MappedFile(dataFolder + "hc_files.txt");
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

      if (size_t flId = sub.find_last_of('/'); flId != sub.npos) {
        hash = sub.substr(flId + 1);
      }

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
    arc->clear();
    arc->seekg(f.offset);

    auto found = FILES.find(f.hash);

    if (found == FILES.end()) {
      char filename[0x10]{};
      snprintf(filename, sizeof(filename), "%X", f.hash);
      std::string fileName(filename);
      arc->read(filename, sizeof(filename));
      arc->seekg(f.offset);
      const uint32 BIN4ID =  0x401;
      const uint32 BIN5ID =  0x501;
      const uint32 BIN2ID =  0x402;
      const uint32 JPGID =  0xFFD8FF;

      if (filename[0] == 'x') {
        fileName.append(".az");
      } else if (!memcmp(filename, "DDS", 3)) {
        fileName.append(".dds");
      } else if (!memcmp(filename, "<?xml", 5)) {
        fileName.append(".hkx");
      } else if (!memcmp(filename, "<svg", 4)) {
        fileName.append(".svg");
      } else if (!memcmp(filename, "<!doctype html>", 15)) {
        fileName.append(".html");
      } else if (!memcmp(filename, &BIN4ID, 3)) {
        fileName.append(".bin");
      } else if (!memcmp(filename, &BIN5ID, 3)) {
        fileName.append(".bin");
      } else if (!memcmp(filename, &BIN2ID, 3)) {
        fileName.append(".bin");
      } else if (!memcmp(filename + 1, "PNG", 3)) {
        fileName.append(".png");
      } else if (!memcmp(filename, "GIF", 3)) {
        fileName.append(".gif");
      } else if (!memcmp(filename, &JPGID, 3)) {
        fileName.append(".jpg");
      } else if (!memcmp(filename, "<object name=\"St", 16)) {
        fileName.append(".afsm");
      } else if (!memcmp(filename, "<value name=\"Ag", 16)) {
        fileName.append(".xml");
      } else if (!memcmp(filename, "BM", 2)) {
        fileName.append(".bmp");
      }

      ectx->NewFile(fileName);
    } else {
      ectx->NewFile(std::string(found->second));
    }

    char streamOut[0x40000];

    const size_t numBlocks = f.size / sizeof(streamOut);
    const size_t restBlock = f.size % sizeof(streamOut);

    for (size_t i = 0; i < numBlocks; i++) {
      arc->read(streamOut, sizeof(streamOut));
      ectx->SendData({streamOut, sizeof(streamOut)});
    }

    if (restBlock) {
      arc->read(streamOut, restBlock);
      ectx->SendData({streamOut, restBlock});
    }
  }
}
