#include "AOTModule.h"

#include <iostream>
#include <set>
#include <vector>

#include "../compiler/Chunk.h"
#include "IRLowering.h"
#include "object/ElfWriter.h"
#include "x64/X64ObjectBackend.h"

namespace trypillia::aot
{

bool AOTModule::compileToObjectFile(ObjFunction *root, const std::string &outPath, std::string &outError)
{
    if (!root)
    {
        outError = "null root function";
        return false;
    }

    // Phase 1: only the top-level (script entry) function is AOT-compiled.
    // Nested functions referenced via OP_CLOSURE are out of scope.
    IRFunction ir;
    if (!IRLowering::lower(root, ir, outError))
    {
        return false;
    }

    // Run the x64 backend.
    auto be = x64::X64ObjectBackend::compile(ir);
    if (!be.ok)
    {
        outError = "backend failed: " + be.error;
        return false;
    }

    // Collect undefined symbols referenced by the function's relocs.
    std::set<std::string> undef;
    for (const auto &r : be.relocs)
    {
        undef.insert(r.symbol);
    }
    std::vector<object::Symbol> undefList;
    for (const auto &name : undef)
    {
        object::Symbol s;
        s.name = name;
        s.definedHere = false;
        s.isLocal = false;
        undefList.push_back(s);
    }

    // Write the ELF .o
    return object::ElfWriter::write(outPath, be, undefList, outError);
}

} // namespace trypillia::aot
