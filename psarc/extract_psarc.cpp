/*  PsarcExtract
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

#include "LzmaDec.h"
#include "project.h"
#include "spike/app_context.hpp"
#include "spike/except.hpp"
#include "spike/io/binreader_stream.hpp"
#include "zlib.h"
#include <cctype>
#include <map>
#include <sstream>

static AppInfo_s appInfo{
    .header = PsarcExtract_DESC " v" PsarcExtract_VERSION
                                ", " PsarcExtract_COPYRIGHT "Lukas Cone",
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct MDDigest {
  uint32 dg[4];

  auto operator<=>(const MDDigest &other) const {
    int res = memcmp(dg, other.dg, sizeof(dg));

    static const std::strong_ordering omap[]{
        std::strong_ordering::less,
        std::strong_ordering::greater,
        std::strong_ordering::equal,
    };

    return omap[res > 0 + (res == 0 * 2)];
  }

  void NoSwap();
};

struct TocEntry {
  MDDigest digest;
  uint32 blockOffset;
  uint64 uncompressedSize;
  uint64 offset;

  void Read(BinReaderRef_e rd) {
    rd.Read(digest);
    rd.Read(blockOffset);
    rd.ReadBuffer(reinterpret_cast<char *>(&uncompressedSize), 5);
    rd.ReadBuffer(reinterpret_cast<char *>(&offset), 5);
    uncompressedSize <<= 24;
    offset <<= 24;
    FByteswapper(uncompressedSize);
    FByteswapper(offset);
  }
};

static constexpr uint32 PSARCID = CompileFourCC("RASP");
static constexpr uint32 COMP_LZMA = CompileFourCC("amzl");
static constexpr uint32 COMP_ZLIB = CompileFourCC("bilz");

struct Header {
  uint32 id;
  uint16 versionMinor;
  uint16 versionMajor;
  uint32 compressionType;
  uint32 tocSize;
  uint32 tocStride;
  uint32 numToc;
  uint32 blockSize;
  uint32 flags;
};

template <> void FByteswapper(Header &item, bool) { FArraySwapper(item); }

using StreamCb = std::function<void(const std::string &)>;

void StreamBlocksLzma(StreamCb cb, BinReaderRef rd, const TocEntry &entry,
                      const std::vector<uint32> &blocks, uint32 blocksizeOut) {
  static ISzAlloc lzmaAlloc{
      .Alloc = [](ISzAllocPtr, size_t num) { return malloc(num); },
      .Free =
          [](ISzAllocPtr, void *ptr) {
            if (ptr) [[likely]] {
              free(ptr);
            }
          },
  };
  std::string tmpInbuffer;
  std::string tmpOutBuffer;
  size_t curBlock = entry.blockOffset;
  size_t processedBytes = 0;
  size_t readBytes = 0;
  const bool isCompressed = blocks.at(curBlock) > 0;

  rd.Seek(entry.offset);

  while (processedBytes < entry.uncompressedSize) {
    const uint32 blockSize = blocks.at(curBlock++);
    if (!isCompressed) {
      const uint32 realBlockSize = blockSize ? blockSize : blocksizeOut;
      rd.ReadContainer(tmpInbuffer, realBlockSize);
      cb(tmpInbuffer);
      processedBytes += realBlockSize;
      continue;
    }

    rd.ReadContainer(tmpInbuffer, blockSize);
    if (blockSize == entry.uncompressedSize) {
      cb(tmpInbuffer);
      break;
    }

    auto inData = reinterpret_cast<const Byte *>(tmpInbuffer.data());
    SizeT destLen = 0;
    memcpy(&destLen, inData + LZMA_PROPS_SIZE, 4);
    tmpOutBuffer.resize(destLen);
    SizeT srcLen = tmpInbuffer.size();

    ELzmaStatus lzmaStatus;

    int status =
        LzmaDecode(reinterpret_cast<Byte *>(tmpOutBuffer.data()), &destLen,
                   inData + 13, &srcLen, inData, LZMA_PROPS_SIZE,
                   LZMA_FINISH_END, &lzmaStatus, &lzmaAlloc);

    if (status != SZ_OK) {
      throw std::runtime_error("Failed to decompress LZMA stream, code: " +
                               std::to_string(status));
    }

    processedBytes += tmpOutBuffer.size();
    readBytes += tmpInbuffer.size();
    cb(tmpOutBuffer);
  }
}

void StreamBlocksZlib(StreamCb cb, BinReaderRef rd, const TocEntry &entry,
                      const std::vector<uint32> &blocks, uint32 blocksizeOut) {
  std::string tmpInbuffer;
  std::string tmpOutBuffer;
  size_t curBlock = entry.blockOffset;
  size_t processedBytes = 0;
  size_t readBytes = 0;
  const bool isCompressed = blocks.at(curBlock) > 0;

  rd.Seek(entry.offset);

  while (processedBytes < entry.uncompressedSize) {
    const uint32 blockSize = blocks.at(curBlock++);
    if (!isCompressed) {
      const uint32 realBlockSize = blockSize ? blockSize : blocksizeOut;
      rd.ReadContainer(tmpInbuffer, realBlockSize);
      cb(tmpInbuffer);
      processedBytes += realBlockSize;
      continue;
    }

    rd.ReadContainer(tmpInbuffer, blockSize);
    if (blockSize == entry.uncompressedSize) {
      cb(tmpInbuffer);
      break;
    }

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = blockSize;
    infstream.next_in = reinterpret_cast<Bytef *>(&tmpInbuffer[0]);
    infstream.avail_out = blocksizeOut;
    infstream.next_out = reinterpret_cast<Bytef *>(&tmpOutBuffer[0]);
    inflateInit(&infstream);
    int state = inflate(&infstream, Z_FINISH);
    inflateEnd(&infstream);

    if (state < 0) {
      throw std::runtime_error(infstream.msg);
    }

    processedBytes += tmpOutBuffer.size();
    readBytes += tmpInbuffer.size();

    if (processedBytes > entry.uncompressedSize) {
      const uint32 diff = processedBytes - entry.uncompressedSize;
      tmpOutBuffer.resize(tmpOutBuffer.size() - diff);
    }

    cb(tmpOutBuffer);
  }
}

extern "C" void md5(const char *initial_msg, size_t initial_len,
                    MDDigest *digest);

void AppProcessFile(AppContext *ctx) {
  BinReaderRef_e rd(ctx->GetStream());
  rd.SwapEndian(true);
  Header hdr;
  rd.Read(hdr);

  if (hdr.id != PSARCID) {
    throw es::InvalidHeaderError(hdr.id);
  }

  if (hdr.versionMajor != 1) {
    throw es::InvalidVersionError(hdr.versionMajor);
  }

  if (hdr.versionMinor < 2 || hdr.versionMinor > 4) {
    throw es::InvalidVersionError(hdr.versionMajor);
  }

  if (hdr.tocStride != 30) {
    throw std::runtime_error("Invalid entry stride: " +
                             std::to_string(hdr.tocStride));
  }

  if (hdr.compressionType != COMP_ZLIB && hdr.compressionType != COMP_LZMA) {
    throw std::runtime_error("Invalid compression type");
  }

  std::vector<TocEntry> entries;
  rd.ReadContainer(entries, hdr.numToc);
  uint32 blockSize = 4;

  if (hdr.blockSize <= (1 << 16)) {
    blockSize = 2;
  } else if (hdr.blockSize <= (1 << 24)) {
    blockSize = 3;
  }

  const uint32 numBlocks =
      (hdr.tocSize - (30 * hdr.numToc + sizeof(Header))) / blockSize;

  std::vector<uint32> blockSizes;
  blockSizes.reserve(numBlocks);

  for (uint32 i = 0; i < numBlocks; i++) {
    uint32 block;
    rd.ReadBuffer(reinterpret_cast<char *>(&block), blockSize);
    block <<= 8 * (4 - blockSize);
    FByteswapper(block);
    blockSizes.emplace_back(block);
  }

  std::function<void(StreamCb, TocEntry &)> streamer;

  if (hdr.compressionType == COMP_LZMA) {
    streamer = [&](StreamCb cb, TocEntry &entry) {
      StreamBlocksLzma(cb, rd, entry, blockSizes, hdr.blockSize);
    };
  } else {
    streamer = [&](StreamCb cb, TocEntry &entry) {
      StreamBlocksZlib(cb, rd, entry, blockSizes, hdr.blockSize);
    };
  }

  /*std::map<MDDigest, std::string> files;

  {
    std::string fileStr;
    StreamCb cb = [&](const std::string &data) { fileStr.append(data); };
    streamer(cb, entries.front());
    size_t curEntry = 0;
    fileStr.resize(entries.front().uncompressedSize);

    for (uint32 i = 1; i < hdr.numToc; i++) {
      size_t foundIndex = fileStr.find('\n', curEntry);
      if (foundIndex == fileStr.npos) {
        foundIndex = fileStr.size();
      }
      std::string_view name(fileStr.data() + curEntry,
                            fileStr.data() + foundIndex);
      curEntry = foundIndex + 1;

      MDDigest digest;
      md5(name.data(), name.size(), &digest);

      files.emplace(digest, name);
      // void *v0 = digest;
      // void *v1 = entries.at(i).digest;
    }
  }

  auto ectx = ctx->ExtractContext();

  for (uint32 i = 1; i < hdr.numToc; i++) {
    auto &entry = entries.at(i).digest;
    auto &file = files.at(entry);
    ectx->NewFile(file);

    StreamCb cb = [&](const std::string &data) { ectx->SendData(data); };
    streamer(cb, entries.at(i));
  }*/

  std::stringstream files;
  {
    StreamCb cb = [&](const std::string &data) {
      files.write(data.data(), data.size());
    };
    streamer(cb, entries.front());
  }

  char fileBuffer[0x2000];
  auto ectx = ctx->ExtractContext();

  for (uint32 i = 1; i < hdr.numToc; i++) {
    files.getline(fileBuffer, sizeof(fileBuffer));
    /*const size_t fileSize =  strlen(fileBuffer);

    for (size_t f = 0; f < fileSize; f++) {
      fileBuffer[f] = std::toupper(fileBuffer[f]);
    }

    MDDigest digest;
    md5(fileBuffer,fileSize, &digest);

    if ((digest <=> entries.at(i).digest) != std::strong_ordering::equal) {
      throw std::runtime_error("Failed entry filename checksum");
    }*/

    const bool isRoot = fileBuffer[0] == '/';
    ectx->NewFile(fileBuffer + isRoot);

    StreamCb cb = [&](const std::string &data) { ectx->SendData(data); };
    streamer(cb, entries.at(i));
  }
}

size_t AppExtractStat(request_chunk requester) {
  auto data = requester(0, sizeof(Header));
  Header *hdr = reinterpret_cast<Header *>(data.data());
  FByteswapper(*hdr);

  if (hdr->id == PSARCID) {
    return hdr->numToc;
  }

  return 0;
}
