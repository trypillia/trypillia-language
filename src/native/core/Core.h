#ifndef TRYPILLIA_NATIVE_CORE_H
#define TRYPILLIA_NATIVE_CORE_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace Core {
void registerAll(VM *vm);
void registerSymbols(SymbolTable *scope);
} // namespace Core
} // namespace StdLib

#endif
