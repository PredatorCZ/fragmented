using SQInt = u32;

struct Object {
    u16 objType;
    
    if (objType == 6) {
        u16 len;
        char text[len];
    } else if (objType == 2) {
        SQInt number;
    } else if (objType == 4) {
        u32 len;
        char text[len];
    }
};

struct VarInfo {
    Object name;
    SQInt pos;
    SQInt startOp;
    SQInt endOp;
};

struct LineInfo {
    SQInt line;
    SQInt op;
};

struct Header {
    u16 bom;
    SQInt headTag;
    SQInt charSize;
    SQInt partTag;
    Object sourceName;
    Object name;
    
    SQInt partTag1;
    SQInt numLiterals;
    SQInt numParameters;
    SQInt numOuterValues;
    SQInt numLocalVarInfos;
    SQInt numLineInfos;
    SQInt numDefaultParams;
    SQInt numInstructions;
    SQInt numFunctions;
    
    SQInt partTag2;
    Object literals[numLiterals];
    
    SQInt partTag3;
    Object parameters[numParameters];
    
    SQInt partTag4;
    
    SQInt partTag5;
    VarInfo localVarInfos[numLocalVarInfos];
    
    SQInt partTag6;
    LineInfo lineInfos[numLineInfos];
    
    SQInt partTag7;
    
    SQInt partTag8;
    u64 instructions[numInstructions];
    
    SQInt partTag9;
};

Header hdr @0 [[inline]];
