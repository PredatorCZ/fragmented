/*  NutCracker
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

std::string_view filters[]{
    ".nut$",
};

static AppInfo_s appInfo{
    .header = NutCracker_DESC " v" NutCracker_VERSION ", " NutCracker_COPYRIGHT
                              "SydMontague, DamianXVI, Lukas Cone",
    //.filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

static const std::map<uint32, std::string_view> OPS{
    /**/ //
    {0x00, "LINE"},
    {0x01, "LOAD"},
    {0x02, "LOADINT"},
    {0x03, "LOADFLOAT"},
    {0x04, "DLOAD"},
    {0x05, "TAILCALL"},
    {0x06, "CALL"},
    {0x07, "PREPCALL"},
    {0x08, "PREPCALLK"},
    {0x09, "GETK"},
    {0x0A, "MOVE"},
    {0x0B, "NEWSLOT"},
    {0x0C, "DELETE"},
    {0x0D, "SET"},
    {0x0E, "GET"},
    {0x0F, "EQ"},
    {0x10, "NE"},
    {0x11, "ARITH"},
    {0x12, "BITW"},
    {0x13, "RETURN"},
    {0x14, "LOADNULLS"},
    {0x15, "LOADROOTTABLE"},
    {0x16, "LOADBOOL"},
    {0x17, "DMOVE"},
    {0x18, "JMP"},
    {0x19, "JNZ"},
    {0x1A, "JZ"},
    {0x1B, "LOADFREEVAR"},
    {0x1C, "VARGC"},
    {0x1D, "GETVARGV"},
    {0x1E, "NEWTABLE"},
    {0x1F, "NEWARRAY"},
    {0x20, "APPENDARRAY"},
    {0x21, "GETPARENT"},
    {0x22, "COMPARITH"},
    {0x23, "COMPARITHL"},
    {0x24, "INC"},
    {0x25, "INCL"},
    {0x26, "PINC"},
    {0x27, "PINCL"},
    {0x28, "CMP"},
    {0x29, "EXISTS"},
    {0x2A, "INSTANCEOF"},
    {0x2B, "AND"},
    {0x2C, "OR"},
    {0x2D, "NEG"},
    {0x2E, "NOT"},
    {0x2F, "BWNOT"},
    {0x30, "CLOSURE"},
    {0x31, "YIELD"},
    {0x32, "RESUME"},
    {0x33, "FOREACH"},
    {0x34, "POSTFOREACH"},
    {0x35, "DELEGATE"},
    {0x36, "CLONE"},
    {0x37, "TYPEOF"},
    {0x38, "PUSHTRAP"},
    {0x39, "POPTRAP"},
    {0x3A, "THROW"},
    {0x3B, "CLASS"},
    {0x3C, "NEWSLOTA"},
};

enum class Operator {
  LINE = 0x00,
  LOAD = 0x01,
  LOADINT = 0x02,
  LOADFLOAT = 0x03,
  DLOAD = 0x04,
  TAILCALL = 0x05,
  CALL = 0x06,
  PREPCALL = 0x07,
  PREPCALLK = 0x08,
  GETK = 0x09,
  MOVE = 0x0A,
  NEWSLOT = 0x0B,
  DELETE = 0x0C,
  SET = 0x0D,
  GET = 0x0E,
  EQ = 0x0F,
  NE = 0x10,
  ARITH = 0x11,
  BITW = 0x12,
  RETURN = 0x13,
  LOADNULLS = 0x14,
  LOADROOTTABLE = 0x15,
  LOADBOOL = 0x16,
  DMOVE = 0x17,
  JMP = 0x18,
  JNZ = 0x19,
  JZ = 0x1A,
  LOADFREEVAR = 0x1B,
  VARGC = 0x1C,
  GETVARGV = 0x1D,
  NEWTABLE = 0x1E,
  NEWARRAY = 0x1F,
  APPENDARRAY = 0x20,
  GETPARENT = 0x21,
  COMPARITH = 0x22,
  COMPARITHL = 0x23,
  INC = 0x24,
  INCL = 0x25,
  PINC = 0x26,
  PINCL = 0x27,
  CMP = 0x28,
  EXISTS = 0x29,
  INSTANCEOF = 0x2A,
  AND = 0x2B,
  OR = 0x2C,
  NEG = 0x2D,
  NOT = 0x2E,
  BWNOT = 0x2F,
  CLOSURE = 0x30,
  YIELD = 0x31,
  RESUME = 0x32,
  FOREACH = 0x33,
  POSTFOREACH = 0x34,
  DELEGATE = 0x35,
  CLONE = 0x36,
  TYPEOF = 0x37,
  PUSHTRAP = 0x38,
  POPTRAP = 0x39,
  THROW = 0x3A,
  CLASS = 0x3B,
  NEWSLOTA = 0x3C,
};
struct SQInstruction {
  int32 arg1;
  uint8 op;
  uint8 arg0;
  uint8 arg2;
  uint8 arg3;
};

std::string ReadString(BinReaderRef rd) {
  std::string retval;
  uint16 type;
  rd.Read(type);

  if (type == 0) {
    return retval;
  }

  if (type != 4) {
    throw std::runtime_error("Invalid type, expected string");
  }

  rd.ReadContainer(retval);
  return retval;
};

struct SQLocalVarInfo {
  std::string name;
  uint32 pos;
  uint32 startOp;
  uint32 endOp;

  void Read(BinReaderRef rd) {
    name = ReadString(rd);
    rd.Read(pos);
    rd.Read(startOp);
    rd.Read(endOp);
  }
};

struct SQLineInfo {
  uint32 line;
  uint32 op;
};

struct Nut32Closure {
  const uint32 PART = CompileFourCC("TRAP");

  std::string sourceName;
  std::string name;

  std::vector<std::string> literals;
  std::vector<std::string> parameters;
  std::vector<SQLocalVarInfo> localVars;
  std::vector<SQLineInfo> lineInfos;
  std::vector<int32> defaultParams;
  std::vector<SQInstruction> instructions;
  std::vector<Nut32Closure> functions;

  uint32 stackSize;
  bool isGenerator;
  bool varParams;

  void Read(BinReaderRef rd) {
    auto CheckPart = [&] {
      uint32 part;
      rd.Read(part);

      if (part != PART) {
        throw es::InvalidHeaderError(part);
      }
    };

    CheckPart();
    sourceName = ReadString(rd);
    name = ReadString(rd);

    CheckPart();
    uint32 numLiterals;
    uint32 numParameters;
    uint32 numOuterValues;
    uint32 numLocalVarInfos;
    uint32 numLineInfos;
    uint32 numDefaultParams;
    uint32 numInstructions;
    uint32 numFunctions;

    rd.Read(numLiterals);
    rd.Read(numParameters);
    rd.Read(numOuterValues);
    rd.Read(numLocalVarInfos);
    rd.Read(numLineInfos);
    rd.Read(numDefaultParams);
    rd.Read(numInstructions);
    rd.Read(numFunctions);

    CheckPart();
    for (uint32 i = 0; i < numLiterals; i++) {
      literals.emplace_back(ReadString(rd));
    }

    CheckPart();
    for (uint32 i = 0; i < numParameters; i++) {
      parameters.emplace_back(ReadString(rd));
    }

    CheckPart();
    if (numOuterValues) {
      throw std::logic_error("Implement outer values");
    }

    CheckPart();
    rd.ReadContainer(localVars, numLocalVarInfos);

    CheckPart();
    rd.ReadContainer(lineInfos, numLineInfos);

    CheckPart();
    rd.ReadContainer(defaultParams, numDefaultParams);

    CheckPart();
    rd.ReadContainer(instructions, numInstructions);

    CheckPart();
    rd.ReadContainer(functions, numFunctions);

    rd.Read(stackSize);
    rd.Read(isGenerator);
    rd.Read(varParams);
  }
};

struct Nut32Header {
  const uint32 BOM = 0xFAFA;
  const uint32 ID = CompileFourCC("RIQS");
  const uint32 TAIL = CompileFourCC("LIAT");

  Nut32Closure main;

  void Read(BinReaderRef rd) {
    uint16 bom;
    rd.Read(bom);

    if (bom != BOM) {
      throw es::InvalidHeaderError(bom);
    }

    uint32 id;
    rd.Read(id);

    if (id != ID) {
      throw es::InvalidHeaderError(id);
    }

    uint32 charSize;
    rd.Read(charSize);

    if (charSize != 1) {
      throw std::runtime_error("Character size is not 1 byte");
    }

    rd.Read(main);

    uint32 tail;
    rd.Read(tail);

    if (tail != TAIL) {
      throw es::InvalidHeaderError(tail);
    }
  }
};

void DumpClosure(std::ostream &str, Nut32Closure &clo, bool isMain) {
  for (auto &f : clo.functions) {
    DumpClosure(str, f, false);
  }

  if (!isMain) {
    str << "function " << clo.name << '(';

    for (auto &p : clo.parameters) {
      str << p << ", ";
    }

    if (clo.parameters.size()) {
      str.seekp(size_t(str.tellp()) - 2);
    }

    str << ") {\n";
  }

  /*str << "__literals = [\n";

  for (auto &l : clo.literals) {
    str << "  \"" << l << "\",\n";
  }

  str << "]\n";*/

  for (uint32 s = 0; s < clo.stackSize; s++) {
    str << "  local var" << s << " = null\n";
  }

  for (auto &i : clo.instructions) {
    auto Stack = [&](int idx) -> std::ostream & {
      str << "  var" << idx;
      return str;
    };

    auto MidStack = [&](int idx) -> std::ostream & {
      str << "var" << idx;
      return str;
    };

    switch (Operator(i.op)) {
    case Operator::LOAD:
      Stack(i.arg0) << " = \"" << clo.literals.at(i.arg1) << "\"\n";
      break;
    case Operator::DLOAD:
      Stack(i.arg0) << " = \"" << clo.literals.at(i.arg1) << "\"\n";
      Stack(i.arg2) << " = \"" << clo.literals.at(i.arg3) << "\"\n";
      break;
    case Operator::LOADFLOAT:
      Stack(i.arg0) << " = " << reinterpret_cast<float &>(i.arg1) << "\n";
      break;
    case Operator::LOADINT:
      Stack(i.arg0) << " = " << i.arg1 << "\n";
      break;
    case Operator::LOADBOOL:
      Stack(i.arg0) << " = " << (i.arg1 ? "true" : "false") << "\n";
      break;
    case Operator::GETK:
      Stack(i.arg0) << " = ";
      MidStack(i.arg2) << "[\"" << clo.literals.at(i.arg1) << "\"]\n";
      break;
    case Operator::NEWARRAY:
      Stack(i.arg0) << " = []\n";
      break;
    case Operator::NEWTABLE:
      Stack(i.arg0) << " = {}\n";
      break;
    case Operator::NEWSLOTA:
      Stack(i.arg1) << '[';
      MidStack(i.arg2) << "] = ";
      MidStack(i.arg3) << "\n";
      break;
    case Operator::LOADROOTTABLE:
      Stack(i.arg0) << " = getroottable()\n";
      break;
    case Operator::PREPCALLK:
      Stack(i.arg3) << " = ";
      MidStack(i.arg2) << "\n";
      Stack(i.arg0) << " = ";
      MidStack(i.arg2) << "[\"" << clo.literals.at(i.arg1) << "\"]\n";
      break;
    case Operator::APPENDARRAY:
      Stack(i.arg0) << ".append(";
      MidStack(i.arg1) << ")\n";
      break;
    case Operator::NEG:
      Stack(i.arg0) << " = -";
      MidStack(i.arg1) << "\n";
      break;

    case Operator::RETURN:
      if (i.arg0 != 0xff) {
        str << "  return ";
        MidStack(i.arg1) << "\n";
      }
      break;

    case Operator::CALL:
      Stack(i.arg0) << " = ";
      MidStack(i.arg1) << '(';

      for (uint32 p = 0; p < i.arg3; p++) {
        str << "var" << i.arg2 + p << ", ";
      }

      if (i.arg3) {
        str.seekp(size_t(str.tellp()) - 2);
      }

      str << ")\n";
      break;

    default:
      str << "  // " << OPS.at(i.op) << ' ' << i.arg1 << ' ' << int(i.arg0)
          << ' ' << int(i.arg2) << ' ' << int(i.arg3) << "\n";
      break;
    }
  }

  if (!isMain) {
    str << "}\n";
  }
}

void AppProcessFile(AppContext *ctx) {
  if (ctx->workingFile.GetFilenameExt().ends_with(".dec.nut")) {
    return;
  }

  BinReaderRef rd(ctx->GetStream());
  Nut32Header script;
  rd.Read(script);

  std::ostream &str =
      ctx->NewFile(std::string(ctx->workingFile.GetFullPath()) + ".dec.nut")
          .str;
  DumpClosure(str, script.main, true);
}
