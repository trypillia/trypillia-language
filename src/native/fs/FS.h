#ifndef TRYPILLIA_NATIVE_FS_H
#define TRYPILLIA_NATIVE_FS_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace FS {
void registerAll(VM *vm);
void registerSymbols(SymbolTable *scope);
} // namespace FS
} // namespace StdLib

#endif
