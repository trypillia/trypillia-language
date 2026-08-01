#ifndef STD_PROMISE_H
#define STD_PROMISE_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib
{
namespace PromiseModule
{
void registerSymbols(SymbolTable *scope);
void registerAll(VM *vm);
} // namespace PromiseModule
} // namespace StdLib

#endif