#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <fstream>
#include <memory>
#include <string>

#include "../compiler/Chunk.h"

class Serializer
{
  public:
    static bool buildStandalone(ObjFunction *function, const std::string &trypilliaExePath,
                                const std::string &outputPath);
    static ObjFunction *loadEmbeddedBytecode(const std::string &currentExePath);

  private:
    static void writeFunction(std::ofstream &out, ObjFunction *function);
    static ObjFunction *readFunction(std::ifstream &in);

    static void writeChunk(std::ofstream &out, Chunk *chunk);
    static Chunk *readChunk(std::ifstream &in);

    static void writeValue(std::ofstream &out, const VMValue &value);
    static VMValue readValue(std::ifstream &in);

    static void writeString(std::ofstream &out, const std::string &str);
    static std::string readString(std::ifstream &in);
};

#endif
