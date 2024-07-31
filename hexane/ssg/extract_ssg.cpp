/*  SSGExtract
    Copyright(C) 2023 Lukas Cone

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
#include "spike/io/binreader.hpp"
#include "spike/io/binwritter.hpp"
#include "spike/io/stat.hpp"
#include "zlib.h"

static AppInfo_s appInfo{
    .header = SSGExtract_DESC " v" SSGExtract_VERSION ", " SSGExtract_COPYRIGHT
                              "Lukas Cone",
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct FileEntry {
  uint32 unk;
  uint32 fileNameOffset;
  uint32 size;
  uint32 unk1;
  uint32 offset;
  uint32 type;
  uint32 unk4;
  int32 compressedSize;
};

template <> void FByteswapper(FileEntry &item, bool) { FArraySwapper(item); }

struct Header {
  uint32 version;
  uint32 null0;
  uint32 filesSize;
  uint32 stringsSize;
  uint32 dataSize;
  uint32 unk;
  uint32 chunksSize;
  uint16 alignment;
  uint16 null1;
};

template <> void FByteswapper(Header &item, bool) { FArraySwapper(item); }

struct TmpFile {
  std::string path = es::GetTempFilename();
  BinReader stream;

  void Decompress(std::istream &str,
                  const std::vector<uint32> &compressedSizes) {
    BinWritter wr(path);
    Bytef iBuffer[0x10000];
    Bytef oBuffer[0x10000];

    for (uint32 s : compressedSizes) {
      if (s == 0) {
        break;
      }
      str.read(reinterpret_cast<char *>(iBuffer), s);
      z_stream infstream;
      infstream.zalloc = Z_NULL;
      infstream.zfree = Z_NULL;
      infstream.opaque = Z_NULL;
      infstream.avail_in = s;
      infstream.next_in = iBuffer;
      infstream.avail_out = 0x10000;
      infstream.next_out = oBuffer;

      inflateInit(&infstream);

      int state = inflate(&infstream, Z_FINISH);

      if (state < 0) {
        throw std::runtime_error(infstream.msg);
      }
      inflateEnd(&infstream);

      wr.WriteBuffer(reinterpret_cast<const char *>(oBuffer), 0x10000 - infstream.avail_out);
    }

    es::Dispose(wr);
    stream.Open(path);
  }

  ~TmpFile() { es::RemoveFile(path); }
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef_e rd(ctx->GetStream());
  Header hdr;
  rd.Push();
  rd.Read(hdr);

  if (hdr.version != 6) {
    rd.SwapEndian(true);
    rd.Pop();
    rd.Read(hdr);
  }

  if (hdr.version != 6) {
    throw es::InvalidVersionError(hdr.version);
  }

  std::vector<FileEntry> entries;
  rd.ReadContainer(entries, hdr.filesSize / sizeof(FileEntry));
  std::vector<uint32> compressedSizes;
  rd.ReadContainer(compressedSizes, hdr.chunksSize / 4);
  std::string strings;
  rd.ReadContainer(strings, hdr.stringsSize);

  auto ectx = ctx->ExtractContext();

  auto Extract = [&](BinReaderRef rd) {
    for (FileEntry &e : entries) {
      std::string fileName(strings.c_str() + e.fileNameOffset);
      if (e.type == CompileFourCC("HKPT")) {
        fileName.append(".hkpt");
      }
      ectx->NewFile(fileName);
      std::string tBuffer;
      rd.ReadContainer(tBuffer, e.size);
      rd.ApplyPadding(hdr.alignment);
      ectx->SendData(tBuffer);
    }
  };

  if (compressedSizes.size() > 0) {
    TmpFile tFile;
    tFile.Decompress(rd.BaseStream(), compressedSizes);
    Extract(tFile.stream);
  } else {
    rd.SetRelativeOrigin(rd.Tell(), false);
    Extract(rd);
  }
}

size_t AppExtractStat(request_chunk requester) {
  auto data = requester(0, sizeof(Header));
  Header *hdr = reinterpret_cast<Header *>(data.data());

  if (hdr->version != 6) {
    FByteswapper(*hdr);
  }

  if (hdr->version != 6) {
    throw es::InvalidVersionError(hdr->version);
  }

  return hdr->filesSize / sizeof(File);
}
