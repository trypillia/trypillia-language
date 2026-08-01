#include "Serializer.h"

#include <iostream>

#define MAGIC "TRYC"
#define VERSION 1

enum ValueType
{
    VAL_NIL = 0,
    VAL_BOOL = 1,
    VAL_DOUBLE = 2,
    VAL_STRING = 3,
    VAL_FUNCTION = 4
};

// .tryc serialization removed in favor of standalone executables

bool Serializer::buildStandalone(ObjFunction *function, const std::string &trypilliaExePath,
                                 const std::string &outputPath)
{
    // 1. Copy the interpreter executable
    std::ifstream src(trypilliaExePath, std::ios::binary);
    std::ofstream dst(outputPath, std::ios::binary);
    if (!src.is_open() || !dst.is_open())
        return false;

    dst << src.rdbuf();
    src.close();

    // 2. Append bytecode
    uint32_t bytecodeStartPos = static_cast<uint32_t>(dst.tellp());

    dst.write(MAGIC, 4);
    uint32_t version = VERSION;
    dst.write(reinterpret_cast<const char *>(&version), sizeof(version));
    writeFunction(dst, function);

    uint32_t bytecodeEndPos = static_cast<uint32_t>(dst.tellp());
    uint32_t bytecodeSize = bytecodeEndPos - bytecodeStartPos;

    // 3. Write footer (Size + Magic TRYS)
    dst.write(reinterpret_cast<const char *>(&bytecodeSize), sizeof(bytecodeSize));
    dst.write("TRYS", 4);

    return true;
}

ObjFunction *Serializer::loadEmbeddedBytecode(const std::string &currentExePath)
{
    std::ifstream in(currentExePath, std::ios::binary);
    if (!in.is_open())
        return nullptr;

    in.seekg(-8, std::ios::end);
    if (in.tellg() < 0)
        return nullptr; // File too small

    uint32_t bytecodeSize;
    in.read(reinterpret_cast<char *>(&bytecodeSize), sizeof(bytecodeSize));

    char magic[4];
    in.read(magic, 4);
    if (std::string(magic, 4) != "TRYS")
        return nullptr;

    // Seek to start of bytecode
    std::streamoff offset = -8 - static_cast<std::streamoff>(bytecodeSize);
    in.seekg(offset, std::ios::end);

    // Read and verify TRYC magic
    in.read(magic, 4);
    if (std::string(magic, 4) != MAGIC)
        return nullptr;

    uint32_t version;
    in.read(reinterpret_cast<char *>(&version), sizeof(version));
    if (version != VERSION)
        return nullptr;

    return readFunction(in);
}

void Serializer::writeFunction(std::ofstream &out, ObjFunction *function)
{
    writeString(out, function->name);
    out.write(reinterpret_cast<const char *>(&function->arity), sizeof(function->arity));
    out.write(reinterpret_cast<const char *>(&function->maxArity), sizeof(function->maxArity));
    out.write(reinterpret_cast<const char *>(&function->isAbstract), sizeof(function->isAbstract));
    // statics is not needed for serialized bytecode functions (built at runtime
    // or empty)
    writeChunk(out, function->chunk);
}

ObjFunction *Serializer::readFunction(std::ifstream &in)
{
    auto function = new ObjFunction();
    function->name = readString(in);
    in.read(reinterpret_cast<char *>(&function->arity), sizeof(function->arity));
    in.read(reinterpret_cast<char *>(&function->maxArity), sizeof(function->maxArity));
    in.read(reinterpret_cast<char *>(&function->isAbstract), sizeof(function->isAbstract));
    function->chunk = readChunk(in);
    return function;
}

void Serializer::writeChunk(std::ofstream &out, Chunk *chunk)
{
    uint32_t codeSize = static_cast<uint32_t>(chunk->code.size());
    out.write(reinterpret_cast<const char *>(&codeSize), sizeof(codeSize));
    if (codeSize > 0)
        out.write(reinterpret_cast<const char *>(chunk->code.data()), codeSize);

    uint32_t linesSize = static_cast<uint32_t>(chunk->lines.size());
    out.write(reinterpret_cast<const char *>(&linesSize), sizeof(linesSize));
    if (linesSize > 0)
        out.write(reinterpret_cast<const char *>(chunk->lines.data()), linesSize * sizeof(int));

    uint32_t constSize = static_cast<uint32_t>(chunk->constants.size());
    out.write(reinterpret_cast<const char *>(&constSize), sizeof(constSize));
    for (const auto &val : chunk->constants)
    {
        writeValue(out, val);
    }
}

Chunk *Serializer::readChunk(std::ifstream &in)
{
    auto chunk = new Chunk();

    uint32_t codeSize;
    in.read(reinterpret_cast<char *>(&codeSize), sizeof(codeSize));
    if (codeSize > 0)
    {
        chunk->code.resize(codeSize);
        in.read(reinterpret_cast<char *>(chunk->code.data()), codeSize);
    }

    uint32_t linesSize;
    in.read(reinterpret_cast<char *>(&linesSize), sizeof(linesSize));
    if (linesSize > 0)
    {
        chunk->lines.resize(linesSize);
        in.read(reinterpret_cast<char *>(chunk->lines.data()), linesSize * sizeof(int));
    }

    uint32_t constSize;
    in.read(reinterpret_cast<char *>(&constSize), sizeof(constSize));
    for (uint32_t i = 0; i < constSize; ++i)
    {
        chunk->constants.push_back(readValue(in));
    }

    return chunk;
}

void Serializer::writeValue(std::ofstream &out, const VMValue &value)
{
    if (value.isNil())
    {
        uint8_t type = VAL_NIL;
        out.write(reinterpret_cast<const char *>(&type), 1);
    }
    else if (value.isBool())
    {
        uint8_t type = VAL_BOOL;
        out.write(reinterpret_cast<const char *>(&type), 1);
        bool b = value.asBool();
        out.write(reinterpret_cast<const char *>(&b), sizeof(b));
    }
    else if (value.isNumber())
    {
        uint8_t type = VAL_DOUBLE;
        out.write(reinterpret_cast<const char *>(&type), 1);
        double d = value.asNumber();
        out.write(reinterpret_cast<const char *>(&d), sizeof(d));
    }
    else if (value.isString())
    {
        uint8_t type = VAL_STRING;
        out.write(reinterpret_cast<const char *>(&type), 1);
        writeString(out, value.asString()->flatten());
    }
    else if (value.isFunction())
    {
        uint8_t type = VAL_FUNCTION;
        out.write(reinterpret_cast<const char *>(&type), 1);
        writeFunction(out, value.asFunction());
    }
    else
    {
        std::cerr << "Warning: Serializer encountered unsupported constant type, "
                     "writing nil instead."
                  << std::endl;
        uint8_t type = VAL_NIL;
        out.write(reinterpret_cast<const char *>(&type), 1);
    }
}

VMValue Serializer::readValue(std::ifstream &in)
{
    uint8_t type;
    in.read(reinterpret_cast<char *>(&type), 1);

    switch (type)
    {
    case VAL_NIL:
        return nullptr;
    case VAL_BOOL: {
        bool b;
        in.read(reinterpret_cast<char *>(&b), sizeof(b));
        return b;
    }
    case VAL_DOUBLE: {
        double d;
        in.read(reinterpret_cast<char *>(&d), sizeof(d));
        return d;
    }
    case VAL_STRING:
        return readString(in);
    case VAL_FUNCTION:
        return readFunction(in);
    default:
        return nullptr;
    }
}

void Serializer::writeString(std::ofstream &out, const std::string &str)
{
    uint32_t len = static_cast<uint32_t>(str.length());
    out.write(reinterpret_cast<const char *>(&len), sizeof(len));
    if (len > 0)
        out.write(str.c_str(), len);
}

std::string Serializer::readString(std::ifstream &in)
{
    uint32_t len;
    in.read(reinterpret_cast<char *>(&len), sizeof(len));
    if (len == 0)
        return "";
    std::string str(len, '\0');
    in.read(&str[0], len);
    return str;
}
