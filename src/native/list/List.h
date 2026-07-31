#ifndef STD_LIST_H
#define STD_LIST_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace ListModule {
void registerSymbols(SymbolTable *scope);
void registerAll(VM *vm);
} // namespace ListModule
} // namespace StdLib

#endif
