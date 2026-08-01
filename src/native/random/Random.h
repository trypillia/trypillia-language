#ifndef STD_RANDOM_H
#define STD_RANDOM_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace RandomModule {
void registerSymbols(SymbolTable* scope);
void registerAll(VM* vm);
}  // namespace RandomModule
}  // namespace StdLib

#endif
