#ifndef TRYPILLIA_AOT_TRAMPOLINE_H
#define TRYPILLIA_AOT_TRAMPOLINE_H

#include <string>

namespace trypillia::aot
{

// Writes a small C source file that, when compiled and linked
// with the AOT object + libtrypillia_rt, installs the AOT entry
// function pointer at program startup. The generated C uses
// __attribute__((constructor)) to register a global initializer.
bool writeEntryTrampolineC(const std::string &cPath, const std::string &entrySymbol, std::string &err);

} // namespace trypillia::aot

#endif
