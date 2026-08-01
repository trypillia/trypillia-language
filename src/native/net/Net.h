#ifndef TRYPILLIA_NATIVE_NET_H
#define TRYPILLIA_NATIVE_NET_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib
{
namespace Net
{
void registerAll(VM *vm);
void registerSymbols(SymbolTable *scope);
} // namespace Net
} // namespace StdLib

#endif
