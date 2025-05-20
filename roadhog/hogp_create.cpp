/*  HOGPCreate
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
#include "spike/io/binreader.hpp"
#include "spike/io/binwritter.hpp"
#include "spike/master_printer.hpp"
#include <mutex>

static AppInfo_s appInfo{
    .header = HOGPCreate_DESC " v" HOGPCreate_VERSION ", " HOGPCreate_COPYRIGHT
                              "Lukas Cone",
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct HOGP {
  static const uint32 ID = CompileFourCC("HOGP");
  uint32 id = ID;
  uint32 version = 3;
  uint64 tocOffset;
};

struct TocFile {
  std::string path;
  uint32 uncompressedSize;
  uint32 compressedSize;
  uint32 chunkIndex;
  uint64 offset;

  void Write(BinWritterRef wr) const {
    wr.WriteContainerWCount<uint16>(path);
    wr.Write(uncompressedSize);
    wr.Write(compressedSize);
    wr.Write(chunkIndex);
    wr.Write(offset);
  }
};

struct MakeContext : AppPackContext {
  std::vector<TocFile> files;
  BinWritter wr;
  std::mutex mtx;

  MakeContext(std::string baseFile) : wr(baseFile + ".bin") {
    HOGP hdr{};
    wr.Write(hdr);
  }

  void SendFile(std::string_view path, std::istream &str) override {
    std::lock_guard lg(mtx);
    BinReaderRef rd(str);

    char outBuffer[0x40000];
    const size_t inSize = rd.GetSize();
    const uint32 numBlocks = inSize / sizeof(outBuffer);
    const uint32 restBlock = inSize % sizeof(outBuffer);

    files.emplace_back(TocFile{
        .path = std::string(path),
        .uncompressedSize = uint32(inSize),
        .compressedSize = uint32(inSize),
        .chunkIndex = 0,
        .offset = wr.Tell(),
    });

    for (uint32 i = 0; i < numBlocks; i++) {
      rd.Read(outBuffer);
      wr.Write(outBuffer);
    }

    if (restBlock) {
      rd.ReadBuffer(outBuffer, restBlock);
      wr.WriteBuffer(outBuffer, restBlock);
    }
  }

  void Finish() override {
    HOGP hdr{.tocOffset = wr.Tell() - sizeof(HOGP)};
    wr.Push();
    wr.Seek(0);
    wr.Write(hdr);
    wr.Pop();
    wr.Write<uint32>(0);
    wr.WriteContainerWCount(files);
    wr.Write<uint32>(0);
    const size_t endOfFile = wr.Tell();
    wr.Pop();
    wr.Write<uint32>(endOfFile - wr.Tell());
  }
};

AppPackContext *AppNewArchive(const std::string &folder) {
  auto file = folder;
  while (file.back() == '/') {
    file.pop_back();
  }

  return new MakeContext(std::move(file));
}
