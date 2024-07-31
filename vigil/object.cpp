#include "nlohmann/json.hpp"
#include "spike/io/binreader_stream.hpp"
#include <vector>

enum class NodeType : uint8 {
  None,
  Class_,
  Int,
  Float,
  Bool,
  String,
  Subclass = 7,
  PolyContainer = 9,
  PolyContainer2,
  Vector,
  HashedString = 0xF,
  Resource = 0xFC,
  Empty = 0xFE, // class end??
  Class = 0xFF,
};

std::string_view ReadHashString(BinReaderRef_e rd,
                                std::vector<std::string> &memberNames) {
  bool isFirstTime;
  rd.Read(isFirstTime);
  if (!isFirstTime) {
    uint32 id;
    rd.Read(id);
    return memberNames.at(id);
  }
  uint64 hash;
  rd.Read(hash);
  std::string retVal;
  rd.ReadContainer<uint16>(retVal);
  return memberNames.emplace_back(std::move(retVal));
}

void ReadObject(BinReaderRef_e rd, nlohmann::json &node,
                std::vector<std::string> &memberNames);

void ReadClass(BinReaderRef_e rd, nlohmann::json &node,
               std::vector<std::string> &memberNames) {
  uint8 type;
  rd.Read(type);
  assert(type == 1 || type == 4);
  uint32 bfData;
  if (type == 4) {
    rd.Read(bfData);
  }

  const std::string className(ReadHashString(rd, memberNames));
  uint32 numMembers;
  rd.Read(numMembers);

  nlohmann::json &memberNode = node[className];

  for (uint32 i = 0; i < numMembers; i++) {
    const std::string name(ReadHashString(rd, memberNames));
    ReadObject(rd, memberNode[name], memberNames);
  }
}

void ReadObject(BinReaderRef_e rd, nlohmann::json &node,
                std::vector<std::string> &memberNames) {
  NodeType type;
  rd.Read(type);

  switch (type) {
  case NodeType::Class:
    ReadClass(rd, node, memberNames);
    break;

  case NodeType::Int: {
    int32 value;
    rd.Read(value);
    node = value;
    break;
  }
  case NodeType::Float: {
    float value;
    rd.Read(value);
    node = value;
    break;
  }
  case NodeType::Bool: {
    bool value;
    rd.Read(value);
    node = value;
    break;
  }
  case NodeType::String: {
    uint8 unk;
    rd.Read(unk);
    auto at = rd.Tell() - 1;
    assert(unk == 0xff);
    std::string value;
    rd.ReadContainer<uint16>(value);
    node = value;
    break;
  }

  case NodeType::Subclass: {
    uint32 depth;
    rd.Read(depth);
    ReadClass(rd, node, memberNames);
    break;
  }

  case NodeType::HashedString: {
    node = ReadHashString(rd, memberNames);
    break;
  }

  case NodeType::PolyContainer:
  case NodeType::PolyContainer2: {
    uint32 numItems;
    bool isMap;
    rd.Read(numItems);
    rd.Read(isMap);

    if (isMap) {
      for (uint32 i = 0; i < numItems; i++) {
        nlohmann::json &mNode = node[i];
        ReadObject(rd, mNode["key"], memberNames);
        ReadObject(rd, mNode["value"], memberNames);
      }
    } else {
      for (uint32 i = 0; i < numItems; i++) {
        ReadObject(rd, node[i], memberNames);
      }
    }
    break;
  }

  case NodeType::Vector: {
    uint32 numItems;
    rd.Read(numItems);

    for (uint32 i = 0; i < numItems; i++) {
      ReadObject(rd, node[i], memberNames);
    }
    break;
  }

  case NodeType::Resource: {
    uint32 unk;
    uint64 hash;
    rd.Read(unk);
    rd.Read(hash);
    node["unk"] = unk;
    node["resource_hash"] = hash;
    break;
  }

  case NodeType::Empty: {
    break;
  }

  default: {
    auto at = rd.Tell() - 1;
    assert(false);
    break;
  }
  }
}
