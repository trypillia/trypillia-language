#ifndef STD_TERMINAL_H
#define STD_TERMINAL_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib
{
namespace TerminalModule
{
void registerSymbols(SymbolTable *scope);
void registerAll(VM *vm);
} // namespace TerminalModule
} // namespace StdLib

#endif
