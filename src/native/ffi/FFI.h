#ifndef STD_FFI_H
#define STD_FFI_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib
{
namespace FFIModule
{
void registerSymbols(SymbolTable *scope);
void registerAll(VM *vm);
} // namespace FFIModule
} // namespace StdLib

#endif
