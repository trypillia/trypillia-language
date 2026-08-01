#ifndef TRYPILLIA_NATIVE_TEST_H
#define TRYPILLIA_NATIVE_TEST_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace Test {
void registerAll(VM* vm);
void registerSymbols(SymbolTable* scope);
}  // namespace Test
}  // namespace StdLib

#endif
