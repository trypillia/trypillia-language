#ifndef STD_STRING_H
#define STD_STRING_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace StringModule {
void registerSymbols(SymbolTable* scope);
void registerAll(VM* vm);
}  // namespace StringModule
}  // namespace StdLib

#endif
