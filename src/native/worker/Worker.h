#ifndef TRYPILLIA_WORKER_H
#define TRYPILLIA_WORKER_H

#include "../../frontend/symbol/SymbolTable.h"
#include "../../vm/core/VM.h"

namespace StdLib {
namespace WorkerModule {
void registerAll(VM* vm);
void registerSymbols(SymbolTable* scope);
}  // namespace WorkerModule
}  // namespace StdLib

#endif
