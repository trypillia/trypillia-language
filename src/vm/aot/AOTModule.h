#ifndef TRYPILLIA_AOT_MODULE_H
#define TRYPILLIA_AOT_MODULE_H

#include <string>

#include "../compiler/Chunk.h"

namespace trypillia::aot
{

// AOTModule — Phase-1 entry point.
//
// Walks an ObjFunction tree, lowers each function to IR, asks the
// x64 backend for code+relocs, writes a single .o file, generates
// a tiny C trampoline for entry-point installation, compiles it
// with the system `cc`, and links the final executable with
// libtrypillia_rt.
//
// Returns true on success. On failure, `outError` describes why
// (typically "this construct is not supported in Phase 1 AOT,
// fallback to interpreter"). The caller (BuildCommand) should
// then fall back to the legacy Serializer-based build.
class AOTModule
{
  public:
    struct Options
    {
        // Path to libtrypillia_rt (the static library that
        // contains jit_*_helper + AOTMain). The Linker will pass
        // -L<dir> -ltrypillia_rt.
        std::string rtLibPath;

        // Extra args appended to the cc command line (e.g. user
        // -L... -l...).
        std::vector<std::string> ccArgs;

        // Directory in which to drop intermediate files (.o, .c).
        // Defaults to a tempdir.
        std::string scratchDir;
    };

    static bool compileToExecutable(ObjFunction *root, const std::string &outPath, const Options &opt,
                                    std::string &outError);
};

} // namespace trypillia::aot

#endif // TRYPILLIA_AOT_MODULE_H
