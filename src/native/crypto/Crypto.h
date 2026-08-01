#ifndef TRYPILLIA_CRYPTO_H
#define TRYPILLIA_CRYPTO_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace CryptoModule {
void registerAll(VM* vm);
void registerSymbols(SymbolTable* scope);
}  // namespace CryptoModule
}  // namespace StdLib

#endif
