/*  ExtractManifest
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
    ".mnfst$",
};

static AppInfo_s appInfo{
    .header = ExtractManifest_DESC " v" ExtractManifest_VERSION
                                   ", " ExtractManifest_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

#include "spike/master_printer.hpp"
#include <cassert>

AppInfo_s *AppInitModule() { return &appInfo; }

extern "C" uint64_t crc64(uint64_t crc, const char *buf, uint64_t len);

struct Header {
  uint32 version;
  uint32 files;
  uint32 dataObject;
  uint32 hashes;
  uint32 locales;
  uint32 animStreams;
  uint32 filesSize;
  uint32 dataObjectSize;
  uint32 hashesSize;
  uint32 localesSize;
  uint32 animStreamsSize;
};

static const uint32 VERSION = 0xD;

struct File {
  std::string name;
  uint64 hash;
  uint16 fileIndex;
  uint16 folderIndex;
  std::vector<uint16> depends;
  uint8 unk0;
  int8 upakIndex;
  uint8 unk1;
  uint8 unk2;
  uint32 dataOffset;

  void Read(BinReaderRef rd) {
    rd.ReadContainer<uint8>(name);
    rd.Read(hash);
    rd.Read(fileIndex);
    rd.Read(folderIndex);
    rd.ReadContainer<uint8>(depends);
    rd.Read(unk0);
    rd.Read(upakIndex);
    rd.Read(unk1);
    rd.Read(unk2);
    rd.Read(dataOffset);
  }
};

struct OBPK {
  static const uint32 ID = CompileFourCC("OBPK");
  uint32 id;
  uint8 unk;
  uint32 version;
  uint32 filesOffset;
  uint32 filesSize;
  uint32 tocOffset;
  uint32 tocSize;
  uint32 dataOffset;
  bool noCompression;

  void Read(BinReaderRef rd) {
    rd.Read(id);
    rd.Read(unk);
    rd.Read(version);

    if (id != ID) {
      throw es::InvalidHeaderError(id);
    }

    if (version != 9) {
      throw es::InvalidVersionError(version);
    }

    rd.Read(filesOffset);
    rd.Read(filesSize);
    rd.Read(tocOffset);
    rd.Read(tocSize);
    rd.Read(dataOffset);
    rd.Read(noCompression);
  }
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  Header hdr;
  rd.Read(hdr);

  if (hdr.version != VERSION) {
    throw es::InvalidVersionError(hdr.version);
  }

  rd.Seek(hdr.files);
  uint32 numFolders;
  uint32 maxFolderNameSize;
  rd.Read(numFolders);
  rd.Read(maxFolderNameSize);

  std::vector<std::string> folders;
  folders.resize(numFolders);

  for (auto &f : folders) {
    uint64 hash;
    rd.Read(hash);
    rd.ReadContainer(f);
  }

  uint32 numUPaks;
  rd.Read(numUPaks);
  std::vector<AppContextStream> upaks;
  std::vector<std::vector<uint32>> offsets;
  std::string workingFolder(ctx->workingFile.GetFolder());

  for (uint32 i = 0; i < numUPaks; i++) {
    int8 type;
    rd.Read(type);
    assert(type == -1);
    std::string upakName;
    rd.ReadContainer<uint16>(upakName);
    auto stream = ctx->RequestFile(workingFolder + upakName + ".upak");
    offsets.emplace_back().emplace_back(BinReaderRef(*stream.Get()).GetSize());
    upaks.emplace_back(std::move(stream));
  }

  uint32 numFiles;
  rd.Read(numFiles);
  std::map<uint16, File> files;

  for (uint32 i = 0; i < numFiles; i++) {
    File file;
    rd.Read(file);
    files.emplace(file.fileIndex, std::move(file));
    if (file.upakIndex >= 0) {
      offsets.at(file.upakIndex).emplace_back(file.dataOffset);
    }
  }

  assert(files.size() == numFiles);

  auto ectx = ctx->ExtractContext();

  if (ectx->RequiresFolders()) {
    for (auto &f : folders) {
      ectx->AddFolderPath(f);
    }

    ectx->GenerateFolders();
  }

  for (auto &o : offsets) {
    std::sort(o.begin(), o.end());
  }

  char buffer[0x40000];
  Bytef iBuffer[0x10000];
  Bytef *bufferPtr = reinterpret_cast<Bytef *>(buffer);
  std::string sBuffer;

  auto UncompressData = [&](BinReaderRef ard) {
    uint32 uncompressedSize;
    ard.Read(uncompressedSize);
    uint32 processed = 0;

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = 0;
    infstream.next_in = iBuffer;
    infstream.avail_out = sizeof(buffer);
    infstream.next_out = bufferPtr;
    inflateInit(&infstream);

    while (uncompressedSize > 0) {
      if (infstream.avail_in == 0) {
        infstream.next_in = iBuffer;
        infstream.avail_in = sizeof(iBuffer);
        ard.ReadBuffer(reinterpret_cast<char *>(iBuffer), infstream.avail_in);
        processed += infstream.avail_in;
      }

      int state = inflate(&infstream, Z_NO_FLUSH);

      if (state < 0) {
        throw std::runtime_error(infstream.msg);
      }

      if (sizeof(buffer) - infstream.avail_out == uncompressedSize) {
        ectx->SendData(
            {reinterpret_cast<const char *>(bufferPtr), uncompressedSize});
        break;
      } else if (infstream.avail_out == 0) {
        infstream.avail_out = sizeof(buffer);
        infstream.next_out = bufferPtr;
        uncompressedSize -= sizeof(buffer);
        ectx->SendData({buffer, infstream.avail_out});
      }
    }

    inflateEnd(&infstream);
  };

  for (auto &[id, file] : files) {
    if (file.upakIndex < 0) {
      PrintWarning(file.name);
      continue; // what to do????
    }

    BinReaderRef stream(*upaks.at(file.upakIndex).Get());
    stream.SetRelativeOrigin(file.dataOffset);
    ectx->NewFile(folders.at(file.folderIndex) + "/" + file.name + ".oppc");

    OBPK opk;
    stream.Read(opk);
    stream.Seek(0);

    if (opk.noCompression) {
      auto &upakOffsets = offsets.at(file.upakIndex);

      auto found = std::upper_bound(upakOffsets.begin(), upakOffsets.end(),
                                    file.dataOffset);
      assert(found != upakOffsets.end());
      const uint32 fileSize = *found - file.dataOffset;
      const size_t numBlocks = fileSize / sizeof(buffer);
      const size_t restBytes = fileSize % sizeof(buffer);

      for (size_t i = 0; i < numBlocks; i++) {
        stream.Read(buffer);
        ectx->SendData({buffer, sizeof(buffer)});
      }

      if (restBytes) {
        stream.ReadBuffer(buffer, restBytes);
        ectx->SendData({buffer, restBytes});
      }
    } else {
      stream.ReadContainer(sBuffer, opk.dataOffset);
      sBuffer.at(29) = true;
      ectx->SendData(sBuffer);
      UncompressData(stream);
    }
  }

  ectx->NewFile(std::string(ctx->workingFile.GetFilename()) + ".bin");
  const size_t numBlocks = hdr.dataObjectSize / sizeof(buffer);
  const size_t restBytes = hdr.dataObjectSize % sizeof(buffer);
  rd.Seek(hdr.dataObject);

  for (size_t i = 0; i < numBlocks; i++) {
    rd.Read(buffer);
    ectx->SendData({buffer, sizeof(buffer)});
  }

  if (restBytes) {
    rd.ReadBuffer(buffer, restBytes);
    ectx->SendData({buffer, restBytes});
  }

  rd.Seek(hdr.animStreams);
  uint32 numAnimStreams;
  uint32 maxNameSize;
  rd.Read(numAnimStreams);
  rd.Read(maxNameSize);
  std::vector<std::string> names;
  names.resize(numAnimStreams);

  for (auto &n : names) {
    uint64 hash;
    rd.Read(hash);
    rd.ReadContainer(n);
  }

  std::vector<uint32> animOffsets;
  rd.ReadContainer(animOffsets, numAnimStreams);
  auto animStream = ctx->RequestFile(workingFolder + "anim_streams.upak");
  BinReaderRef ard(*animStream.Get());

  for (uint32 curAnim = 0; auto &a : names) {
    const uint32 offset = animOffsets.at(curAnim++);
    ectx->NewFile("anim_streams/" + a);
    ard.Seek(offset);
    UncompressData(ard);
  }
}
