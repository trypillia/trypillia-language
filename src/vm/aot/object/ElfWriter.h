#ifndef TRYPILLIA_ELF_WRITER_H
#define TRYPILLIA_ELF_WRITER_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "../x64/Encoder.h"
#include "../x64/X64ObjectBackend.h"

namespace trypillia::aot::object
{

// =============================================================
// ElfWriter — minimal ELF64 ET_REL (relocatable object) writer.
//
// We only emit a subset of ELF that is sufficient for "one function
// + a handful of undefined symbols" relocatable files that the
// system linker (`cc`/`ld`) can consume.  The sections we emit:
//
//   .text          SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR  (our code)
//   .rela.text     SHT_RELA,     SHF_INFO(.text)
//   .rodata        SHT_PROGBITS, SHF_ALLOC                (rodata entries)
//   .symtab        SHT_SYMTAB
//   .strtab        SHT_STRTAB
//   .shstrtab      SHT_STRTAB    (section names)
//
// We do not emit .data, .bss, .note.*, .comment, etc. — those are
// not needed for the AOT path.
//
// The file layout is the classic "many small sections, one
// string table per symbol-table side, contiguous in the file"
// approach. ELF is flexible; we pick the simplest layout that
// `cc`/`ld` accept on a standard Linux x86-64 system.
// =============================================================

struct Symbol
{
    std::string name;
    uint64_t value = 0;        // section-relative for LOCAL/GLOBAL FUNC
    uint64_t size = 0;         // for functions: 0; for UNDEF: 0
    uint8_t info = 0;          // ELF64_ST_INFO(binding, type)
    uint8_t other = 0;         // visibility
    uint16_t shndx = 0;        // section index (SHN_UNDEF etc.)
    bool definedHere = true;   // true => defined; false => undefined (UNDEF)
    bool isLocal = false;      // true => LOCAL binding
};

struct Relocation
{
    uint64_t offset;        // offset within the section being relocated
    std::string symbol;     // target symbol name (must exist in symbols[])
    int64_t addend;
    int64_t type;           // e.g. R_X86_64_PC32 = 2
};

struct SectionText
{
    std::vector<uint8_t> data;
    std::vector<Relocation> relocs;
    std::string symbol;   // public function symbol (one per .text)
    std::string name = ".text";
};

struct SectionRodata
{
    std::vector<uint8_t> data;
    std::vector<Relocation> relocs; // typically empty for rodata
    std::string name = ".rodata";
    // Each rodata "block" gets a local symbol pointing to it (for LEA/MOV).
    std::vector<std::pair<std::string, uint64_t>> localSymbols; // (name, offset)
};

class ElfWriter
{
  public:
    // Build a relocatable object file from a single function. For
    // Phase 1 we only support one function per .o; multi-function
    // support is straightforward (concatenate SectionText and add
    // more public symbols) and will be added in Phase 3.
    static bool write(const std::string &path, const x64::BackendResult &func, const std::vector<Symbol> &extraUndefined,
                      std::string &err);
};

} // namespace trypillia::aot::object

#endif // TRYPILLIA_ELF_WRITER_H
