#include "AOTModule.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

#include "../compiler/Chunk.h"
#include "AOTTrampoline.h"
#include "IRLowering.h"
#include "Linker.h"
#include "object/ElfWriter.h"
#include "x64/X64ObjectBackend.h"

namespace trypillia::aot
{

bool AOTModule::compileToExecutable(ObjFunction *root, const std::string &outPath, const Options &opt,
                                    std::string &outError)
{
    if (!root)
    {
        outError = "null root function";
        return false;
    }

    // Phase 1: only the top-level (script entry) function is AOT-compiled.
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

    // Set up scratch dir for intermediate files.
    std::string scratch = opt.scratchDir;
    if (scratch.empty())
    {
        scratch = std::filesystem::temp_directory_path().string() + "/trypillia_aot";
    }
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);
    if (ec)
    {
        outError = "could not create scratch dir: " + scratch + ": " + ec.message();
        return false;
    }

    std::string objPath = scratch + "/aot.o";
    std::string trampolineC = scratch + "/trampoline.c";
    std::string trampolineO = scratch + "/trampoline.o";

    // Write the AOT .o
    if (!object::ElfWriter::write(objPath, be, undefList, outError))
    {
        return false;
    }

    // Write the C trampoline (constructor that calls
    // trypillia_aot_set_entry_fn(&entry))
    if (!writeEntryTrampolineC(trampolineC, ir.name, outError))
    {
        return false;
    }

    // Compile the trampoline C -> .o using the system cc.
    {
        std::string cmd = "cc -c -o '" + trampolineO + "' '" + trampolineC + "' 2>&1";
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            outError = "compiling trampoline C failed: " + cmd;
            return false;
        }
    }

    // Link everything into the final executable.
    std::vector<std::string> objects = {objPath, trampolineO};
    return Linker::link(outPath, objects, opt.rtLibPath, opt.ccArgs, outError);
}

} // namespace trypillia::aot
