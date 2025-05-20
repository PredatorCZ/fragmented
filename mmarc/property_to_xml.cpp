/*  Property2XML
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
#include "pugixml.hpp"
#include "spike/app_context.hpp"
#include "spike/crypto/jenkinshash3.hpp"
#include "spike/except.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/io/fileinfo.hpp"
#include "spike/io/stat.hpp"
#include "spike/master_printer.hpp"
#include <map>
#include <mutex>
#include <thread>

#include "spike/io/binreader.hpp"

std::string_view filters[]{
    ".bin$",
};

static AppInfo_s appInfo{
    .header = Property2XML_DESC " v" Property2XML_VERSION
                                ", " Property2XML_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

static es::MappedFile mappedFile;
std::map<uint32, std::string_view> NAMES;
static std::mutex wmtx;
std::map<uint32, std::string_view> NEWNAMES;

static es::MappedFile mappedFile2;
std::map<uint32, std::string_view> RAWNAMES;

void LoadStrings(es::MappedFile &mappedFile,
                 std::map<uint32, std::string_view> &names, std::string path) {
  mappedFile = es::MappedFile(path);
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
        if (names.contains(hash) && names.at(hash) != sub) {
          PrintError("String colision: ", names.at(hash), " vs: ", sub);
        } else {
          names.emplace(hash, sub);
        }
      }

      totalMap.remove_prefix(found + 1);

      if (totalMap.front() == '\n') {
        totalMap.remove_prefix(1);
      }
    }
  }
}

bool AppInitContext(const std::string &dataFolder) {
  LoadStrings(mappedFile, NAMES, dataFolder + "hc_params.txt");
  LoadStrings(mappedFile2, RAWNAMES, dataFolder + "hc_stringdump.txt");

  return true;
}

std::string_view LookupHash(uint32 hash) {
  static std::string exeBuffer = [] {
    std::string buff;
    BinReader rd("/home/lukas/Downloads/sdk/-./Hunter_Win32.exe");
    rd.Seek(18600960);
    rd.ReadContainer(exeBuffer, rd.GetSize() - 18600960);
    return buff;
  }();

  auto Search = [&](uint32 from, uint32 to) {
    for (size_t i = 0; i < exeBuffer.size() - to; i++) {
      for (size_t p = from; p < to; p++) {
        std::string_view sw{exeBuffer.data() + i, p};
        if (JenkinsHash3_(sw) == hash) {
          std::lock_guard<std::mutex> lg(wmtx);
          NEWNAMES.emplace(hash, sw);
          //PrintInfo(hash, " ", sw);
        }
      }
    }
  };

  auto found = NAMES.find(hash);

  if (found != NAMES.end()) {
    return found->second;
  }

  found = NEWNAMES.find(hash);

  if (found != NEWNAMES.end()) {
    return found->second;
  }

  found = RAWNAMES.find(hash);

  if (found != RAWNAMES.end()) {
    NEWNAMES.emplace(*found);
    //PrintInfo(found->second);
    return found->second;
  }

  if (0) {
    std::jthread th0(Search, 2, 8);
    std::jthread th00(Search, 8, 16);
    std::jthread th1(Search, 16, 24);
    std::jthread th01(Search, 24, 32);
    std::jthread th2(Search, 32, 40);
    std::jthread th02(Search, 40, 48);
    std::jthread th3(Search, 48, 56);
    std::jthread th03(Search, 56, 64);
    std::jthread th4(Search, 64, 72);
    std::jthread th04(Search, 72, 80);
    std::jthread th5(Search, 80, 88);
    std::jthread th05(Search, 88, 96);
    std::jthread th6(Search, 96, 104);
    std::jthread th06(Search, 104, 112);
    std::jthread th7(Search, 112, 120);
    std::jthread th07(Search, 120, 128);
  }

  found = NEWNAMES.find(hash);

  if (found != NEWNAMES.end()) {
    return found->second;
  }

  NEWNAMES.emplace(hash, std::string_view{});
  return {};
}

enum class DataType : uint16 {
  None,
  Container,
  Variants,
  RawData,
  HashedContainer,
  HashedVariants,
};

enum class VariantType : uint8 {
  None,
  Int,
  Float,
  String,
  Vector2,
  Vector3,
  Vector4,
  Matrix3x3,
  Matrix4x4,
  IntArray,
  FloatArray,
  UInt,
};

void LoadSections(BinReaderRef rd, pugi::xml_node node);

void LoadContainer(BinReaderRef rd, pugi::xml_node node) {
  uint16 numItems;
  rd.Read(numItems);

  for (uint16 i = 0; i < numItems; i++) {
    std::string name;
    rd.ReadContainer(name);
    pugi::xml_node child = node.append_child("object");
    child.append_attribute("name").set_value(name.c_str());
    LoadSections(rd, child);
  }
}

void LoadHashContainer(BinReaderRef rd, pugi::xml_node node) {
  uint16 numItems;
  rd.Read(numItems);

  for (uint16 i = 0; i < numItems; i++) {
    pugi::xml_node child = node.append_child("object");
    uint32 hash;
    rd.Read(hash);
    std::string_view nm(LookupHash(hash));

    if (nm.empty()) {
      char buffer[0x10]{};
      snprintf(buffer, sizeof(buffer), "h:%X", hash);
      child.append_attribute("name").set_value(buffer);
    } else {
      std::string astr(nm);
      child.append_attribute("name").set_value(astr.c_str());
    }

    LoadSections(rd, child);
  }
}

void LoadVariant(BinReaderRef rd, pugi::xml_node node) {
  VariantType type;
  rd.Read(type);
  char buffer[0x1000];

  switch (type) {
  case VariantType::None:
    break;
  case VariantType::Int: {
    node.append_attribute("type").set_value("int");
    int32 value;
    rd.Read(value);
    int written = snprintf(buffer, sizeof(buffer), "%d", value);
    node.append_buffer(buffer, written);
    break;
  }
  case VariantType::Float: {
    node.append_attribute("type").set_value("float");
    float value;
    rd.Read(value);
    int written = snprintf(buffer, sizeof(buffer), "%f", value);
    node.append_buffer(buffer, written);
    break;
  }
  case VariantType::String: {
    node.append_attribute("type").set_value("string");
    std::string valStr;
    rd.ReadContainer<uint16>(valStr);
    //PrintLine(valStr);
    node.append_buffer(valStr.data(), valStr.size());
    break;
  }
  case VariantType::Vector2: {
    node.append_attribute("type").set_value("vec2");
    float value[2];
    rd.Read(value);
    int written = snprintf(buffer, sizeof(buffer), "%f,%f", value[0], value[1]);
    node.append_buffer(buffer, written);
    break;
  }
  case VariantType::Vector3: {
    node.append_attribute("type").set_value("vec3");
    float value[3];
    rd.Read(value);
    int written = snprintf(buffer, sizeof(buffer), "%f,%f,%f", value[0],
                           value[1], value[2]);
    node.append_buffer(buffer, written);
    break;
  }
  case VariantType::Vector4: {
    node.append_attribute("type").set_value("vec4");
    float value[4];
    rd.Read(value);
    int written = snprintf(buffer, sizeof(buffer), "%f,%f,%f,%f", value[0],
                           value[1], value[2], value[3]);
    node.append_buffer(buffer, written);
    break;
  }
  case VariantType::Matrix4x4: {
    node.append_attribute("type").set_value("mat");
    float value[12];
    rd.Read(value);
    int written = snprintf(
        buffer, sizeof(buffer), "%f,%f,%f, %f,%f,%f, %f,%f,%f, %f,%f,%f",
        value[0], value[1], value[2], value[3], value[4], value[5], value[6],
        value[7], value[8], value[9], value[10], value[11]);
    node.append_buffer(buffer, written);
    break;
  }
  case VariantType::IntArray: {
    node.append_attribute("type").set_value("vec_int");
    uint32 numItems;
    rd.Read(numItems);
    std::string outBuffer;

    for (uint32 i = 0; i < numItems; i++) {
      int32 value;
      rd.Read(value);
      snprintf(buffer, sizeof(buffer), "%d,", value);
      outBuffer.append(buffer);
    }

    node.append_buffer(outBuffer.data(), outBuffer.size());
    break;
  }
  case VariantType::FloatArray: {
    node.append_attribute("type").set_value("vec_float");
    uint32 numItems;
    rd.Read(numItems);
    std::string outBuffer;

    for (uint32 i = 0; i < numItems; i++) {
      float value;
      rd.Read(value);
      snprintf(buffer, sizeof(buffer), "%f,", value);
      outBuffer.append(buffer);
    }

    node.append_buffer(outBuffer.data(), outBuffer.size());
    break;
  }
  case VariantType::UInt: {
    node.append_attribute("type").set_value("int");
    uint32 value;
    rd.Read(value);
    int written = snprintf(buffer, sizeof(buffer), "%d", value);
    node.append_buffer(buffer, written);
    break;
  }
  default:
    throw std::runtime_error("Undefined variant type at: " +
                             std::to_string(rd.Tell() - sizeof(type)));
  }
}

void LoadVariants(BinReaderRef rd, pugi::xml_node node) {
  uint16 numItems;
  rd.Read(numItems);

  for (uint16 i = 0; i < numItems; i++) {
    std::string name;
    rd.ReadContainer(name);
    pugi::xml_node child = node.append_child("value");
    child.append_attribute("name").set_value(name.c_str());
    LoadVariant(rd, child);
  }
}

void LoadHashVariants(BinReaderRef rd, pugi::xml_node node) {
  uint16 numItems;
  rd.Read(numItems);

  for (uint16 i = 0; i < numItems; i++) {
    pugi::xml_node child = node.append_child("value");
    uint32 hash;
    rd.Read(hash);
    std::string_view nm(LookupHash(hash));

    if (nm.empty()) {
      char buffer[0x10]{};
      snprintf(buffer, sizeof(buffer), "h:%X", hash);
      child.append_attribute("name").set_value(buffer);
    } else {
      std::string astr(nm);
      child.append_attribute("name").set_value(astr.c_str());
    }
    LoadVariant(rd, child);
  }
}

void LoadSections(BinReaderRef rd, pugi::xml_node node) {
  uint8 numSections;
  rd.Read(numSections);

  for (uint8 s = 0; s < numSections; s++) {
    DataType type;
    rd.Read(type);

    switch (type) {
    case DataType::Container:
      LoadContainer(rd, node);
      break;
    case DataType::HashedContainer:
      LoadHashContainer(rd, node);
      break;
    case DataType::Variants:
      LoadVariants(rd, node);
      break;
    case DataType::HashedVariants:
      LoadHashVariants(rd, node);
      break;
    default:
      throw std::runtime_error("Undefined section type");
    }
  }
}

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  pugi::xml_document doc;
  LoadSections(rd, doc.append_child("object"));
  doc.save(ctx->NewFile(ctx->workingFile.ChangeExtension2("xml")).str);
}

// [rest y] [rest x] zzxx
unsigned int LinearToSwizzle(int x, int z)
{
  return (132 * (z & 0xFFFFFFFC)) + (4 * (z & 3)) + (x & 3) + (4 * (x & 0xFFFFFFFC));
}
