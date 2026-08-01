#ifndef STD_MAP_H
#define STD_MAP_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace MapModule {
void registerSymbols(SymbolTable* scope);
void registerAll(VM* vm);
}  // namespace MapModule
}  // namespace StdLib

#endif
