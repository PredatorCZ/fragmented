/*  SARCExtract
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
    ".fl$",
    ".nl$",
    ".bl$",
    ".ee$",
};

static AppInfo_s appInfo{
    .header = SARCExtract_DESC " v" SARCExtract_VERSION
                               ", " SARCExtract_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct SARFile {
  std::string_view Path() const {
    std::string_view sw(path, offset);
    return es::SkipEndWhitespace(sw);
  }
  uint32 Offset() const {
    return *reinterpret_cast<const uint32 *>(path + offset);
  }
  uint32 Size() const {
    return *reinterpret_cast<const uint32 *>(path + offset + 4);
  }
  const SARFile *Next() const {
    return reinterpret_cast<const SARFile *>(path + offset + 8);
  }
  operator bool() const { return offset; }

private:
  uint32 offset;
  char path[1];
};

static const uint32 SARID = CompileFourCC("SARC");

struct SARHeader {
  uint32 id0;
  uint32 id;
  uint32 version;
};

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  SARHeader hdr;
  rd.Read(hdr);

  if (hdr.id != SARID) {
    throw es::InvalidHeaderError(hdr.id);
  }

  if (hdr.version != 2) {
    throw es::InvalidVersionError(hdr.version);
  }

  std::string tocBuffer;
  rd.ReadContainer(tocBuffer);
  const SARFile *curFile = reinterpret_cast<const SARFile *>(tocBuffer.data());
  auto ectx = ctx->ExtractContext();

  if (ectx->RequiresFolders()) {
    while (*curFile) {
      AFileInfo finf(curFile->Path());
      ectx->AddFolderPath(std::string(finf.GetFolder()));
      curFile = curFile->Next();
    }

    ectx->GenerateFolders();
  }

  char streamOut[0x40000];

  while (*curFile) {
    rd.Seek(curFile->Offset());
    ectx->NewFile(std::string(curFile->Path()));

    const size_t numBlocks = curFile->Size() / sizeof(streamOut);
    const size_t restBlock = curFile->Size() % sizeof(streamOut);
    curFile = curFile->Next();

    for (size_t i = 0; i < numBlocks; i++) {
      rd.ReadBuffer(streamOut, sizeof(streamOut));
      ectx->SendData({streamOut, sizeof(streamOut)});
    }

    if (restBlock) {
      rd.ReadBuffer(streamOut, restBlock);
      ectx->SendData({streamOut, restBlock});
    }
  }
}
