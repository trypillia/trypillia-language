#ifndef STD_OS_H
#define STD_OS_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib
{
namespace OSModule
{
extern std::vector<std::string> commandLineArgs;
void registerSymbols(SymbolTable *scope);
void registerAll(VM *vm);
} // namespace OSModule
} // namespace StdLib

#endif
