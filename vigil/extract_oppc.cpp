/*  ExtractOPPC
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
#include <map>
#include <zlib.h>

std::string_view filters[]{
    ".oppc$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header = ExtractOPPC_DESC " v" ExtractOPPC_VERSION
                               ", " ExtractOPPC_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

#include "spike/master_printer.hpp"
#include <cassert>

AppInfo_s *AppInitModule() { return &appInfo; }

extern "C" uint64_t crc64(uint64_t crc, const char *buf, uint64_t len);

struct OBPK {
  static const uint32 ID = CompileFourCC("OBPK");
  uint32 id;
  uint8 unk;
  uint32 version;

  void Read(BinReaderRef rd) {
    rd.Read(id);
    rd.Read(unk);
    rd.Read(version);

    if (id != ID) {
      throw es::InvalidHeaderError(id);
    }
  }
};

struct OBPK9 {
  uint32 filesOffset;
  uint32 filesSize;
  uint32 tocOffset;
  uint32 tocSize;
  uint32 dataOffset;
  bool noCompression;

  void Read(BinReaderRef rd) {
    rd.Read(filesOffset);
    rd.Read(filesSize);
    rd.Read(tocOffset);
    rd.Read(tocSize);
    rd.Read(dataOffset);
    rd.Read(noCompression);
  }
};

struct OBPK6 {
  uint32 numFilesTotal;
  uint32 maxStringSize;
  uint8 noCompression;
  uint32 tocOffset;
  uint32 tocSize;

  void Read(BinReaderRef rd) {
    rd.Read(numFilesTotal);
    rd.Read(maxStringSize);
    rd.Read(noCompression);
    rd.Read(tocOffset);
    rd.Read(tocSize);
  }
};

struct FileIds {
  uint32 numFoldersTotal;
  uint32 numFilesTotal;
  uint32 fileIdsBufferSize;
  uint32 null0;
};

struct FolderIds {
  uint32 numFolders;
  uint32 null0;
  uint32 unk0;
  uint32 unk1;
};

struct Ids {
  uint16 item0;
  uint16 item1;
};

struct Folder {
  uint32 unk0;
  uint32 numFiles;
  uint32 hashOffset;
  uint32 u16Offset;
  std::vector<uint8> fileTypes;

  void Read(BinReaderRef rd) {
    rd.Read(unk0);
    rd.Read(numFiles);
    rd.Read(hashOffset);
    rd.Read(u16Offset);
    rd.ReadContainer(fileTypes, numFiles);
  }
};

struct FileData {
  uint32 fileSize;
  std::string metaData;

  void Read(BinReaderRef rd) {
    rd.Read(fileSize);
    rd.ReadContainer(metaData);
  }
};

auto MakeName(std::string_view name) {
  return std::make_pair(crc64(0, name.data(), name.size()), name);
}

static std::map<uint64, std::string_view> NAMES{
    MakeName("zgfx"),      MakeName("dds"),      MakeName("psystem"),
    MakeName("glomm"),     MakeName("meshpack"), MakeName("thrnode"),
    MakeName("thrnodeop"), MakeName("o3d"),      MakeName("lightdb"),
    MakeName("tnode"),     MakeName("tnodeop"),  MakeName("physpack"),
    MakeName("loc"),       MakeName("bmat"),     MakeName("dcm"),
    MakeName("wpc"),       MakeName("anm"),      MakeName("tfnt"),
    MakeName("sam"),       MakeName("dxsmf"),    MakeName("lighting_complex"),
    MakeName("lighting"),

};
#include <fstream>
std::ifstream str(
    "/media/slib/SteamLibrary/steamapps/common/Darksiders 2/Darksiders2.exe");
std::string exeData;

void LookupHash(uint64 hash) {
  if (exeData.empty()) {
    BinReaderRef rd(str);
    rd.ReadContainer(exeData, rd.GetSize());
  }

  for (uint32 e = 3; e < 20; e++) {
    for (uint32 i = 0; i < exeData.size() - e; i++) {
      const uint64 crc = crc64(0, exeData.data() + i, e);

      if (crc == hash) {
        PrintWarning(std::string_view(exeData.data() + i, e));
      }
    }
  }
}

void ProcessVersion9(AppContext *ctx, BinReaderRef rd) {
  OBPK9 hdr;
  rd.Read(hdr);

  if (!hdr.noCompression) {
    throw std::runtime_error("Compressed data is not supported");
  }

  rd.Seek(hdr.filesOffset);
  FileIds fileIds;
  rd.Read(fileIds);
  std::string fileIdBuffer;
  rd.ReadContainer(fileIdBuffer, fileIds.fileIdsBufferSize);
  FolderIds folderIds;
  rd.Read(folderIds);
  std::vector<Folder> folders;
  rd.ReadContainer(folders, folderIds.numFolders);
  bool useFileNames = false;
  rd.Read(useFileNames);
  std::vector<std::string> fileNames;

  if (useFileNames) {
    uint32 maxFileNameSize;
    rd.Read(maxFileNameSize);
    fileNames.resize(fileIds.numFilesTotal);

    for (auto &f : fileNames) {
      uint64 hash;
      rd.Read(hash);
      rd.ReadContainer(f);
      std::transform(f.begin(), f.end(), f.begin(),
                     [](char c) { return std::tolower(c); });
      NAMES.emplace(MakeName(f));
    }
  }

  rd.Seek(hdr.tocOffset);

  std::vector<FileData> fileData;
  rd.ReadContainer(fileData, fileIds.numFilesTotal);

  rd.SetRelativeOrigin(hdr.dataOffset);

  auto ectx = ctx->ExtractContext();
  std::string tBuffer;
  size_t curGroup = 0;

  for (size_t curFileTotal = 0; auto &f : folders) {
    const char *hashBegin = fileIdBuffer.data() + f.hashOffset;
    // const Ids *ids = reinterpret_cast<const Ids *>(fileIdBuffer.data() +
    // f.u16Offset);

    for (size_t curFile = 0; auto &l : f.fileTypes) {
      uint64 hash;
      memcpy(&hash, hashBegin + 8 * curFile++, 8);

      std::string fileName(NAMES.at(hash));
      fileName.push_back('.');
      memcpy(&hash, fileIdBuffer.data() + 8 * l, 8);
      fileName.append(NAMES.at(hash));
      ectx->NewFile(fileName);
      rd.ReadContainer(tBuffer, fileData.at(curFileTotal++).fileSize);
      ectx->SendData(tBuffer);
    }
    curGroup++;
  }

  /*for (uint32 f = 0; f < fileIds.numFilesTotal + fileIds.numFoldersTotal; f++)
  { uint64 hash; memcpy(&hash, fileIdBuffer.data() + 8 * f, 8); try {
      NAMES.at(hash);
    } catch (...) {
      LookupHash(hash);
    }
  }*/
}

struct FileGroup {
  uint64 type;
  std::vector<uint64> files;

  void Read(BinReaderRef rd) {
    rd.Read(type);
    rd.ReadContainer(files);
  }
};

void ProcessVersion6(AppContext *ctx, BinReaderRef rd) {
  OBPK6 hdr;
  rd.Read(hdr);
  rd.Seek(hdr.tocOffset);
  rd.Push();

  uint32 numNames;
  rd.Read(numNames);
  std::vector<std::string> fileNames;
  fileNames.resize(numNames);

  for (auto &f : fileNames) {
    uint64 hash;
    rd.Read(hash);
    rd.ReadContainer(f);
    std::transform(f.begin(), f.end(), f.begin(),
                   [](char c) { return std::tolower(c); });
  }

  uint32 fileNamesSize;
  uint32 numFileNames;
  rd.Read(fileNamesSize);
  rd.Read(numFileNames);

  for (uint32 i = 0; i < numFileNames; i++) {
    uint64 hash;
    rd.Read(hash);
    std::string &f = fileNames.emplace_back();
    rd.ReadContainer(f);
    std::transform(f.begin(), f.end(), f.begin(),
                   [](char c) { return std::tolower(c); });
  }

  for (auto &f : fileNames) {
    NAMES.emplace(MakeName(f));
  }

  std::vector<FileGroup> fileGroups;
  rd.ReadContainer(fileGroups);

  uint32 numGroups;
  rd.Read(numGroups);
  rd.Skip(12 * numGroups);
  std::vector<uint32> fileSizes;
  for (uint32 i = 0; i < hdr.numFilesTotal; i++) {
    uint64 hash;
    uint8 metaSize;
    rd.Read(hash);
    rd.Read(fileSizes.emplace_back());
    rd.Read(metaSize);
    rd.Skip(metaSize + 4);
  }

  rd.Pop();
  rd.Skip(hdr.tocSize);
  assert(!hdr.noCompression);

  uint32 uncompressedSizeTotal;
  rd.Read(uncompressedSizeTotal);

  Bytef iBuffer[0x10000];
  std::string oBuffer;

  z_stream infstream;
  infstream.zalloc = Z_NULL;
  infstream.zfree = Z_NULL;
  infstream.opaque = Z_NULL;
  infstream.avail_in = 0;
  infstream.next_in = iBuffer;
  inflateInit(&infstream);
  uint32 processed = 0;
  auto ectx = ctx->ExtractContext();

  for (size_t curFileTotal = 0; auto &g : fileGroups) {
    for (uint64 f : g.files) {
      oBuffer.resize(fileSizes.at(curFileTotal++));
      infstream.avail_out = oBuffer.size();
      infstream.next_out = reinterpret_cast<Bytef *>(oBuffer.data());

      std::string fileName(NAMES.at(f));
      fileName.push_back('.');
      fileName.append(NAMES.at(g.type));
      ectx->NewFile(fileName);

      while (infstream.avail_out) {
        if (infstream.avail_in == 0) {
          infstream.next_in = iBuffer;
          infstream.avail_in = sizeof(iBuffer);
          rd.ReadBuffer(reinterpret_cast<char *>(iBuffer), infstream.avail_in);
          processed += infstream.avail_in;
        }

        int state = inflate(&infstream, Z_NO_FLUSH);

        if (state < 0) {
          throw std::runtime_error(infstream.msg);
        }
      }

      ectx->SendData(oBuffer);
    }
  }

  inflateEnd(&infstream);
}

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  OBPK hdr;
  rd.Read(hdr);

  if (hdr.version == 9) {
    ProcessVersion9(ctx, rd);
  } else if (hdr.version == 6) {
    ProcessVersion6(ctx, rd);
  } else {
    throw es::InvalidVersionError(hdr.version);
  }
}
