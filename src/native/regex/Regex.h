#ifndef STD_REGEX_H
#define STD_REGEX_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib
{
namespace RegexModule
{
void registerSymbols(SymbolTable *scope);
void registerAll(VM *vm);
} // namespace RegexModule
} // namespace StdLib

#endif
