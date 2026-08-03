#include "ElfWriter.h"

#include <cstring>
#include <iostream>
#include <unordered_map>

namespace trypillia::aot::object
{

// =============================================================
// ELF64 constants (System V ABI, AMD64 supplement)
// =============================================================

// e_ident[EI_CLASS] / EI_DATA / EI_VERSION
static constexpr uint8_t ELFCLASS64 = 2;
static constexpr uint8_t ELFDATA2LSB = 1;
static constexpr uint8_t EV_CURRENT = 1;
static constexpr uint8_t ELFOSABI_NONE = 0;

// e_type
static constexpr uint16_t ET_REL = 1;

// e_machine
static constexpr uint16_t EM_X86_64 = 62;

// e_version
static constexpr uint32_t EV = 1;

// SHT_*
static constexpr uint32_t SHT_NULL = 0;
static constexpr uint32_t SHT_PROGBITS = 1;
static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint32_t SHT_RELA = 4;

// SHF_*
static constexpr uint64_t SHF_WRITE = 0x1;
static constexpr uint64_t SHF_ALLOC = 0x2;
static constexpr uint64_t SHF_EXECINSTR = 0x4;

// SHN_*
static constexpr uint16_t SHN_UNDEF = 0;

// STB_*
static constexpr uint8_t STB_LOCAL = 0;
static constexpr uint8_t STB_GLOBAL = 1;
static constexpr uint8_t STB_WEAK = 2;

// STT_*
static constexpr uint8_t STT_NOTYPE = 0;
static constexpr uint8_t STT_OBJECT = 1;
static constexpr uint8_t STT_FUNC = 2;
static constexpr uint8_t STT_SECTION = 3;
static constexpr uint8_t STT_FILE = 4;

static uint8_t stInfo(uint8_t binding, uint8_t type)
{
    return (binding << 4) | (type & 0xf);
}

struct Elf64Ehdr
{
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64Shdr
{
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Elf64Sym
{
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct Elf64Rela
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
};

// Encode r_info: high 32 = symbol index, low 32 = type.
static uint64_t rInfo(uint32_t symIdx, uint32_t type)
{
    return (static_cast<uint64_t>(symIdx) << 32) | type;
}

// R_X86_64_*
static constexpr uint32_t R_X86_64_64 = 1;
static constexpr uint32_t R_X86_64_PC32 = 2;

// =============================================================
// String table helpers
// =============================================================
struct Strtab
{
    std::vector<uint8_t> bytes;
    std::unordered_map<std::string, uint32_t> offsets;

    Strtab()
    {
        bytes.push_back(0); // leading NUL
    }

    // Returns the offset of `s` within the table (creating it if
    // necessary). Always includes a trailing NUL terminator.
    uint32_t intern(const std::string &s)
    {
        auto it = offsets.find(s);
        if (it != offsets.end())
            return it->second;
        uint32_t off = static_cast<uint32_t>(bytes.size());
        bytes.insert(bytes.end(), s.begin(), s.end());
        bytes.push_back(0);
        offsets[s] = off;
        return off;
    }

    size_t size() const
    {
        return bytes.size();
    }
};

// =============================================================
// Write helpers
// =============================================================
static void writeBytes(std::ofstream &out, const void *p, size_t n)
{
    out.write(reinterpret_cast<const char *>(p), n);
}

static void writeU16(std::ofstream &out, uint16_t v)
{
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
}

static void writeU32(std::ofstream &out, uint32_t v)
{
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
    out.put(static_cast<char>((v >> 16) & 0xff));
    out.put(static_cast<char>((v >> 24) & 0xff));
}

static void writeU64(std::ofstream &out, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        out.put(static_cast<char>((v >> (8 * i)) & 0xff));
}

static void writeEhdr(std::ofstream &out, const Elf64Ehdr &h)
{
    writeBytes(out, h.e_ident, 16);
    writeU16(out, h.e_type);
    writeU16(out, h.e_machine);
    writeU32(out, h.e_version);
    writeU64(out, h.e_entry);
    writeU64(out, h.e_phoff);
    writeU64(out, h.e_shoff);
    writeU32(out, h.e_flags);
    writeU16(out, h.e_ehsize);
    writeU16(out, h.e_phentsize);
    writeU16(out, h.e_phnum);
    writeU16(out, h.e_shentsize);
    writeU16(out, h.e_shnum);
    writeU16(out, h.e_shstrndx);
}

static void writeShdr(std::ofstream &out, const Elf64Shdr &s)
{
    writeU32(out, s.sh_name);
    writeU32(out, s.sh_type);
    writeU64(out, s.sh_flags);
    writeU64(out, s.sh_addr);
    writeU64(out, s.sh_offset);
    writeU64(out, s.sh_size);
    writeU32(out, s.sh_link);
    writeU32(out, s.sh_info);
    writeU64(out, s.sh_addralign);
    writeU64(out, s.sh_entsize);
}

static void writeSym(std::ofstream &out, const Elf64Sym &s)
{
    writeU32(out, s.st_name);
    out.put(static_cast<char>(s.st_info));
    out.put(static_cast<char>(s.st_other));
    writeU16(out, s.st_shndx);
    writeU64(out, s.st_value);
    writeU64(out, s.st_size);
}

static void writeRela(std::ofstream &out, const Elf64Rela &r)
{
    writeU64(out, r.r_offset);
    writeU64(out, r.r_info);
    writeU64(out, static_cast<uint64_t>(r.r_addend));
}

bool ElfWriter::write(const std::string &path, const x64::BackendResult &func, const std::vector<Symbol> &extraUndefined,
                      std::string &err)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        err = "could not open output: " + path;
        return false;
    }

    // ---- Build string tables ----
    Strtab shstrtab; // section names
    Strtab strtab;   // symbol names
    shstrtab.intern("");
    shstrtab.intern(".text");
    shstrtab.intern(".rela.text");
    shstrtab.intern(".rodata");
    shstrtab.intern(".symtab");
    shstrtab.intern(".strtab");
    shstrtab.intern(".shstrtab");

    strtab.intern("");

    // ---- Build the symbol table ----
    // Order is important: the first non-null local symbols must
    // come first (SHN_UNDEF at index 0, then locals, then globals).
    // We track indices to use in relocations.
    std::vector<Elf64Sym> symbols;
    std::unordered_map<std::string, uint32_t> symIdx;

    // Index 0: the required UNDEF null symbol
    {
        Elf64Sym s{};
        s.st_info = stInfo(STB_LOCAL, STT_NOTYPE);
        s.st_shndx = SHN_UNDEF;
        symbols.push_back(s);
        symIdx[""] = 0;
    }

    // Add a FILE symbol for diagnostics (some linkers want it).
    {
        Elf64Sym s{};
        s.st_name = strtab.intern("trypillia_aot.c");
        s.st_info = stInfo(STB_LOCAL, STT_FILE);
        s.st_shndx = SHN_UNDEF;
        symbols.push_back(s);
    }

    // Add .text and .rodata section symbols (must be local).
    // These have STT_SECTION and point to the section index.
    // We don't know the final section index yet; we patch later.
    // For now, record the symbol-table position so we can fill
    // shndx once we know the layout.
    // Actually, this is a chicken-and-egg: section indices depend
    // on the order of section headers, which we determine after
    // building the data. We allocate two "placeholder" entries
    // here and patch st_shndx after layout.
    uint32_t textSectionSymIdx = static_cast<uint32_t>(symbols.size());
    {
        Elf64Sym s{};
        s.st_name = shstrtab.intern(".text");
        s.st_info = stInfo(STB_LOCAL, STT_SECTION);
        s.st_shndx = 0; // patched after layout
        symbols.push_back(s);
    }
    uint32_t rodataSectionSymIdx = static_cast<uint32_t>(symbols.size());
    {
        Elf64Sym s{};
        s.st_name = shstrtab.intern(".rodata");
        s.st_info = stInfo(STB_LOCAL, STT_SECTION);
        s.st_shndx = 0; // patched after layout
        symbols.push_back(s);
    }

    // The function's public symbol.
    {
        Elf64Sym s{};
        s.st_name = strtab.intern(func.entrySymbol);
        s.st_info = stInfo(STB_GLOBAL, STT_FUNC);
        // shndx = .text section index (patched later); for now 0
        s.st_shndx = 0;
        s.st_value = 0;     // value within .text
        s.st_size = static_cast<uint64_t>(func.code.size());
        symbols.push_back(s);
        symIdx[func.entrySymbol] = static_cast<uint32_t>(symbols.size()) - 1;
    }

    // Undefined symbols (jit_*_helper etc.)
    // The caller (X64ObjectBackend / AOTModule) gives us a list of
    // helper symbols that the code references. We add them as
    // STB_GLOBAL STT_NOTYPE SHN_UNDEF.
    for (const auto &us : extraUndefined)
    {
        if (symIdx.count(us.name))
            continue;
        Elf64Sym s{};
        s.st_name = strtab.intern(us.name);
        s.st_info = stInfo(STB_GLOBAL, STT_NOTYPE);
        s.st_shndx = SHN_UNDEF;
        symbols.push_back(s);
        symIdx[us.name] = static_cast<uint32_t>(symbols.size()) - 1;
    }

    // ---- Build sections in their file order ----
    // Layout:
    //   [ELF header]
    //   [.text data]
    //   [.rela.text data]
    //   [.rodata data]
    //   [.symtab data]
    //   [.strtab data]
    //   [.shstrtab data]
    //   [section headers]
    std::vector<uint8_t> textData = func.code;
    std::vector<Elf64Rela> relaText;
    for (const auto &r : func.relocs)
    {
        auto it = symIdx.find(r.symbol);
        if (it == symIdx.end())
        {
            err = "relocation against unknown symbol: " + r.symbol;
            return false;
        }
        Elf64Rela er{};
        er.r_offset = r.offset;
        er.r_info = rInfo(it->second, static_cast<uint32_t>(r.kind));
        er.r_addend = r.addend;
        relaText.push_back(er);
    }
    // For Phase 1 we use no .rodata entries. The infrastructure is
    // there for future constants (string literals, class vtables).
    std::vector<uint8_t> rodataData;

    // Build .symtab data
    std::vector<uint8_t> symtabData;
    for (const auto &s : symbols)
    {
        size_t pos = symtabData.size();
        symtabData.resize(pos + sizeof(Elf64Sym));
        std::memcpy(&symtabData[pos], &s, sizeof(Elf64Sym));
    }
    // Build .rela.text data
    std::vector<uint8_t> relaData;
    for (const auto &r : relaText)
    {
        size_t pos = relaData.size();
        relaData.resize(pos + sizeof(Elf64Rela));
        std::memcpy(&relaData[pos], &r, sizeof(Elf64Rela));
    }

    // ---- Layout ----
    uint64_t off = 0;
    Elf64Ehdr ehdr{};
    std::memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV;
    ehdr.e_ehsize = sizeof(Elf64Ehdr);
    ehdr.e_shentsize = sizeof(Elf64Shdr);
    ehdr.e_phentsize = 0; // no program headers for ET_REL

    // We need to know section indices to patch symbols; let's
    // decide the order up front:
    //   0: SHN_UNDEF
    //   1: .text
    //   2: .rela.text
    //   3: .rodata
    //   4: .symtab
    //   5: .strtab
    //   6: .shstrtab
    const int idx_NULL = 0;
    const int idx_text = 1;
    const int idx_rela = 2;
    const int idx_rodata = 3;
    const int idx_symtab = 4;
    const int idx_strtab = 5;
    const int idx_shstrtab = 6;
    const int total_sections = 7;
    ehdr.e_shnum = total_sections;
    ehdr.e_shstrndx = idx_shstrtab;

    // Patch the section-symbols' shndx and the function symbol's shndx.
    // Index 0 of the symbol table is UNDEF; the section symbols
    // are at indices 1 and 2 in our pre-built table (textSectionSymIdx
    // and rodataSectionSymIdx, defined above).
    {
        Elf64Sym &st = symbols[textSectionSymIdx];
        st.st_shndx = static_cast<uint16_t>(idx_text);
    }
    {
        Elf64Sym &st = symbols[rodataSectionSymIdx];
        st.st_shndx = static_cast<uint16_t>(idx_rodata);
    }
    {
        // The function's GLOBAL symbol points to .text.
        // It was added right after the section symbols.
        // Find it: the symbol with name == func.entrySymbol.
        for (auto &s : symbols)
        {
            std::string nm = reinterpret_cast<char*>(strtab.bytes.data()) + s.st_name;
            if (nm == func.entrySymbol)
            {
                s.st_shndx = static_cast<uint16_t>(idx_text);
                s.st_value = 0;
                break;
            }
        }
    }

    // Rebuild symtabData with the patched symbols.
    symtabData.clear();
    for (const auto &s : symbols)
    {
        size_t pos = symtabData.size();
        symtabData.resize(pos + sizeof(Elf64Sym));
        std::memcpy(&symtabData[pos], &s, sizeof(Elf64Sym));
    }

    // Section header offsets
    off = sizeof(Elf64Ehdr);
    uint64_t off_text = off;
    off += textData.size();
    if (off % 8)
        off += 8 - (off % 8);
    uint64_t off_rela = off;
    off += relaData.size();
    if (off % 8)
        off += 8 - (off % 8);
    uint64_t off_rodata = off;
    off += rodataData.size();
    if (off % 8)
        off += 8 - (off % 8);
    uint64_t off_symtab = off;
    off += symtabData.size();
    if (off % 8)
        off += 8 - (off % 8);
    uint64_t off_strtab = off;
    off += strtab.bytes.size();
    if (off % 8)
        off += 8 - (off % 8);
    uint64_t off_shstrtab = off;
    off += shstrtab.bytes.size();
    if (off % 8)
        off += 8 - (off % 8);
    uint64_t off_shdrs = off;

    // ---- Write ELF header ----
    ehdr.e_shoff = off_shdrs;
    writeEhdr(out, ehdr);

    // ---- Write section data ----
    out.write(reinterpret_cast<const char *>(textData.data()), textData.size());
    if (textData.size() % 8)
    {
        uint8_t pad[8] = {0};
        out.write(reinterpret_cast<const char *>(pad), 8 - (textData.size() % 8));
    }
    out.write(reinterpret_cast<const char *>(relaData.data()), relaData.size());
    if (relaData.size() % 8)
    {
        uint8_t pad[8] = {0};
        out.write(reinterpret_cast<const char *>(pad), 8 - (relaData.size() % 8));
    }
    out.write(reinterpret_cast<const char *>(rodataData.data()), rodataData.size());
    if (rodataData.size() % 8)
    {
        uint8_t pad[8] = {0};
        out.write(reinterpret_cast<const char *>(pad), 8 - (rodataData.size() % 8));
    }
    out.write(reinterpret_cast<const char *>(symtabData.data()), symtabData.size());
    if (symtabData.size() % 8)
    {
        uint8_t pad[8] = {0};
        out.write(reinterpret_cast<const char *>(pad), 8 - (symtabData.size() % 8));
    }
    out.write(reinterpret_cast<const char *>(strtab.bytes.data()), strtab.bytes.size());
    if (strtab.bytes.size() % 8)
    {
        uint8_t pad[8] = {0};
        out.write(reinterpret_cast<const char *>(pad), 8 - (strtab.bytes.size() % 8));
    }
    out.write(reinterpret_cast<const char *>(shstrtab.bytes.data()), shstrtab.bytes.size());
    if (shstrtab.bytes.size() % 8)
    {
        uint8_t pad[8] = {0};
        out.write(reinterpret_cast<const char *>(pad), 8 - (shstrtab.bytes.size() % 8));
    }

    // ---- Write section headers ----
    Elf64Shdr shdr{};

    // SHT_NULL
    shdr = {};
    shdr.sh_name = 0;
    shdr.sh_type = SHT_NULL;
    writeShdr(out, shdr);

    // .text
    shdr = {};
    shdr.sh_name = shstrtab.intern(".text");
    shdr.sh_type = SHT_PROGBITS;
    shdr.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdr.sh_offset = off_text;
    shdr.sh_size = textData.size();
    shdr.sh_addralign = 16;
    writeShdr(out, shdr);

    // .rela.text
    shdr = {};
    shdr.sh_name = shstrtab.intern(".rela.text");
    shdr.sh_type = SHT_RELA;
    shdr.sh_offset = off_rela;
    shdr.sh_size = relaData.size();
    shdr.sh_link = idx_symtab;
    shdr.sh_info = idx_text;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(Elf64Rela);
    writeShdr(out, shdr);

    // .rodata
    shdr = {};
    shdr.sh_name = shstrtab.intern(".rodata");
    shdr.sh_type = SHT_PROGBITS;
    shdr.sh_flags = SHF_ALLOC;
    shdr.sh_offset = off_rodata;
    shdr.sh_size = rodataData.size();
    shdr.sh_addralign = 1;
    writeShdr(out, shdr);

    // .symtab
    shdr = {};
    shdr.sh_name = shstrtab.intern(".symtab");
    shdr.sh_type = SHT_SYMTAB;
    shdr.sh_offset = off_symtab;
    shdr.sh_size = symtabData.size();
    shdr.sh_link = idx_strtab;
    shdr.sh_info = 4; // index of first global symbol (after null, FILE, .text, .rodata)
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(Elf64Sym);
    writeShdr(out, shdr);

    // .strtab
    shdr = {};
    shdr.sh_name = shstrtab.intern(".strtab");
    shdr.sh_type = SHT_STRTAB;
    shdr.sh_offset = off_strtab;
    shdr.sh_size = strtab.bytes.size();
    shdr.sh_addralign = 1;
    writeShdr(out, shdr);

    // .shstrtab
    shdr = {};
    shdr.sh_name = shstrtab.intern(".shstrtab");
    shdr.sh_type = SHT_STRTAB;
    shdr.sh_offset = off_shstrtab;
    shdr.sh_size = shstrtab.bytes.size();
    shdr.sh_addralign = 1;
    writeShdr(out, shdr);

    out.close();
    return true;
}

} // namespace trypillia::aot::object
