#ifndef TRYPILLIA_AOT_MODULE_H
#define TRYPILLIA_AOT_MODULE_H

#include <string>

#include "../compiler/Chunk.h"

namespace trypillia::aot
{

// AOTModule — Phase-1 entry point.
//
// Walks an ObjFunction tree, lowers each function to IR, asks the
// x64 backend for code+relocs, and writes a single .o file.
// Returns true on success; on failure, `outError` describes why
// (typically "this construct is not supported in Phase 1 AOT,
// fallback to interpreter"). The caller (BuildCommand) should then
// fall back to the legacy Serializer-based build.
class AOTModule
{
  public:
    static bool compileToObjectFile(ObjFunction *root, const std::string &outPath, std::string &outError);
};

} // namespace trypillia::aot

#endif // TRYPILLIA_AOT_MODULE_H
